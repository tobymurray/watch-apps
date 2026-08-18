/**
 ******************************************************************************
 * @file    FlashDumper.cpp
 * @brief   The chunk/CRC/manifest/resume loop.
 ******************************************************************************
 */

#include "FlashDumper.hpp"

#include <cstdio>
#include <cstring>

#include "Crc32.hpp"

#define LOG_MODULE_PRX      "Dumper"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

FlashDumper::FlashDumper(const SDK::Kernel& kernel, const DumpRegion& region,
                         const uint8_t* window)
    : mKernel(kernel), mRegion(region), mWindow(window)
{
}

bool FlashDumper::regionPrefix(const DumpRegion& region, char* out, size_t outLen)
{
    const DumpRegion defaults;
    if (region.base == defaults.base && region.size == defaults.size) {
        out[0] = '\0'; // Default region: flat, and byte-compatible with before.
        return true;
    }
    const int n = std::snprintf(out, outLen, "region_%08lX/",
                                static_cast<unsigned long>(region.base));
    return n > 0 && static_cast<size_t>(n) < outLen;
}

void FlashDumper::chunkFileName(uint32_t off, char* out, size_t outLen)
{
    // Six uppercase hex digits, matching the reassembler's `dump_{off:06X}.bin`
    // exactly. Wider offsets still format correctly (%06lX is a minimum, not a
    // truncation) which matters for a config-file region whose size exceeds
    // 16 MB -- Python's :06X behaves the same way, so the two stay in step.
    std::snprintf(out, outLen, "dump_%06lX.bin", static_cast<unsigned long>(off));
}

void FlashDumper::chunkPath(uint32_t off, char* out, size_t outLen) const
{
    char prefix[kPathLen];
    if (!regionPrefix(mRegion, prefix, sizeof(prefix))) {
        prefix[0] = '\0';
    }
    char name[32];
    chunkFileName(off, name, sizeof(name));
    std::snprintf(out, outLen, "%s%s", prefix, name);
}

void FlashDumper::manifestPath(char* out, size_t outLen) const
{
    char prefix[kPathLen];
    if (!regionPrefix(mRegion, prefix, sizeof(prefix))) {
        prefix[0] = '\0';
    }
    std::snprintf(out, outLen, "%s%s", prefix, kManifestName);
}

uint64_t FlashDumper::bytesDone() const
{
    // Only the Hashing phase has advanced through the region; a chunk being
    // rewritten was already fully hashed, so its bytes are counted whole.
    const uint64_t whole = static_cast<uint64_t>(mChunkIndex) * mRegion.chunk;
    if (mState != State::Dumping) {
        return mState == State::Done ? bytesTotal() : whole;
    }
    return whole + (mPhase == Phase::Hashing ? mChunkPos : mRegion.chunk);
}

void FlashDumper::fail(Error error)
{
    mError      = error;
    mErrorChunk = mChunkIndex;
    mState      = State::Error;
    if (mFile) {
        mFile->close();
        mFile.reset();
    }
    LOG_INFO("dump failed on chunk %u/%u, error %u\n", mChunkIndex, mRegion.nchunks(),
             static_cast<unsigned>(error));
}

void FlashDumper::cancel()
{
    if (mFile) {
        mFile->close();
        mFile.reset();
    }
    mState        = State::Idle;
    mScanComplete = false;
}

// -----------------------------------------------------------------------------
// Presence scan
// -----------------------------------------------------------------------------

void FlashDumper::beginScan()
{
    if (mState == State::Checking || mState == State::Dumping) {
        return;
    }
    if (!mRegion.valid()) {
        fail(Error::BadRegion);
        return;
    }
    mScanIndex     = 0;
    mChunksPresent = 0;
    mScanComplete  = false;
    mState         = State::Checking;
    LOG_INFO("checking for existing chunks of %u\n", mRegion.nchunks());
}

void FlashDumper::advanceScan(unsigned budgetEntries)
{
    const unsigned total = mRegion.nchunks();

    for (unsigned done = 0; done < budgetEntries && mScanIndex < total; ++done, ++mScanIndex) {
        char name[kPathLen];
        chunkPath(mScanIndex * mRegion.chunk, name, sizeof(name));

        // objectInfo rather than exist(): a file of the wrong size is not a
        // chunk that can be skipped, so the size is the whole question and
        // asking twice would be two round trips to say it.
        SDK::Interface::IFileSystem::ObjectInfo info{};
        if (mKernel.fs.objectInfo(name, info) && !info.isDir
                && info.size == static_cast<size_t>(mRegion.chunk)) {
            ++mChunksPresent;
        }
    }

    if (mScanIndex >= total) {
        mScanComplete = true;
        mState        = State::Idle;
        LOG_INFO("%u/%u chunks already on disk\n", mChunksPresent, total);
    }
}

// -----------------------------------------------------------------------------
// The pass
// -----------------------------------------------------------------------------

void FlashDumper::beginDump()
{
    if (mState == State::Dumping || mState == State::Checking) {
        return; // Guard against a double-start; see beginDump()'s contract.
    }
    if (!mRegion.valid()) {
        fail(Error::BadRegion);
        return;
    }

    mChunkIndex     = 0;
    mChunksDone     = 0;
    mChunksVerified = 0;
    mChunkPos       = 0;
    mChunkBw        = 0;
    mChunkCrc       = Crc32::kInit;
    mFileCrc        = Crc32::kInit;
    mWholeCrc       = Crc32::kInit;
    mWholeCrcFinal  = 0;
    mMode           = Mode::Writing;
    mPhase          = Phase::Hashing;
    mError          = Error::None;
    mErrorChunk     = 0;
    mVerifyReadFailed = false;
    mFile.reset();

    // A non-default region writes into its own subdirectory; make it before the
    // first file lands in it. mkdir is create-or-exists, so a resumed run is
    // fine, and a failure here surfaces as the OpenFailed that follows rather
    // than being guessed at now.
    char prefix[kPathLen];
    if (regionPrefix(mRegion, prefix, sizeof(prefix)) && prefix[0] != '\0') {
        char dir[kPathLen];
        std::snprintf(dir, sizeof(dir), "%s", prefix);
        const size_t len = std::strlen(dir);
        if (len > 0 && dir[len - 1] == '/') {
            dir[len - 1] = '\0'; // mkdir wants the directory, not a trailing slash.
        }
        mKernel.fs.mkdir(dir);
    }

    mManifest.reset();
    mManifest.addHeader(mRegion.base, mRegion.size, mRegion.chunk, mRegion.subwrite,
                        mRegion.nchunks());

    // Written before any chunk, so an interruption in the first chunk still
    // leaves a manifest whose header tells the host the geometry -- without it
    // the reassembler cannot do anything at all with whatever chunk files it
    // finds.
    if (!flushManifest()) {
        return;
    }

    mState = State::Dumping;
    LOG_INFO("dump started: base=%08lX size=%08lX chunk=%08lX in %u chunks\n",
             static_cast<unsigned long>(mRegion.base), static_cast<unsigned long>(mRegion.size),
             static_cast<unsigned long>(mRegion.chunk), mRegion.nchunks());

    if (!openChunk()) {
        return;
    }
}

void FlashDumper::step(size_t budgetBytes)
{
    switch (mState) {
        case State::Checking:
            // One entry per 4 kB of nominal budget, so a caller that passes its
            // usual byte budget gets a scan slice of comparable duration
            // without needing to know this state exists. Always at least one,
            // or a small budget would never finish the scan.
            advanceScan(budgetBytes >= 4096 ? static_cast<unsigned>(budgetBytes / 4096) : 1u);
            break;

        case State::Dumping:
            advanceDump(budgetBytes);
            break;

        case State::Idle:
        case State::Done:
        case State::Error:
            break;
    }
}

bool FlashDumper::openChunk()
{
    const uint32_t off = mChunkIndex * mRegion.chunk;

    char name[kPathLen];
    chunkPath(off, name, sizeof(name));

    mChunkPos         = 0;
    mChunkBw          = 0;
    mChunkCrc         = Crc32::kInit;
    mFileCrc          = Crc32::kInit;
    mPhase            = Phase::Hashing;
    mVerifyReadFailed = false;

    // Try the file read-only first. A right-sized file is a candidate for
    // re-verification; anything else -- absent, wrong size, unreadable -- goes
    // straight to being rewritten, which is always correct and never worse.
    std::unique_ptr<SDK::Interface::IFile> existing = mKernel.fs.file(name);
    if (existing && existing->open(false, false)) {
        if (existing->size() == static_cast<size_t>(mRegion.chunk)) {
            mFile = std::move(existing);
            mMode = Mode::Verifying;
            return true;
        }
        existing->close();
    }

    mMode = Mode::Writing;
    mFile = mKernel.fs.file(name);
    if (!mFile || !mFile->open(true, true)) {
        mFile.reset();
        fail(Error::OpenFailed);
        return false;
    }
    return true;
}

void FlashDumper::advanceDump(size_t budgetBytes)
{
    // Honoured in whole sub-writes: a partial sub-write would have to be
    // resumed mid-block, and the block size is already the unit everything
    // else here is expressed in.
    size_t spent = 0;

    while (spent < budgetBytes && mState == State::Dumping) {
        const uint32_t remaining = mRegion.chunk - mChunkPos;
        const uint32_t take = remaining < mRegion.subwrite ? remaining : mRegion.subwrite;

        const uint8_t* src = chunkPtr(mChunkIndex) + mChunkPos;

        if (mPhase == Phase::Hashing) {
            // The one dereference of the region. Everything downstream is
            // arithmetic on bytes already in hand.
            mChunkCrc = Crc32::update(mChunkCrc, src, take);
            mWholeCrc = Crc32::update(mWholeCrc, src, take);

            if (mMode == Mode::Writing) {
                size_t bw = 0;
                mFile->write(reinterpret_cast<const char*>(src), take, bw);
                mChunkBw += static_cast<uint32_t>(bw);
                if (bw != take) {
                    // A short write is silent on this storage: write() can
                    // report success having moved fewer bytes. Catching it here
                    // rather than only at the end of the chunk means the
                    // manifest names the chunk that failed instead of a size
                    // mismatch discovered later.
                    LOG_INFO("short write on chunk %u: %u of %u bytes\n", mChunkIndex,
                             static_cast<unsigned>(bw), static_cast<unsigned>(take));
                    fail(Error::ShortWrite);
                    return;
                }
            } else {
                size_t br = 0;
                if (!mFile->read(reinterpret_cast<char*>(mReadBuf), take, br) || br != take) {
                    // The file is shorter or less readable than its size
                    // claimed. Not an error: it just means this chunk cannot be
                    // trusted, which is what the rewrite path is for.
                    mVerifyReadFailed = true;
                    mChunkPos         = mRegion.chunk;
                    // Fold the rest of the chunk's memory in, or the running
                    // whole-image CRC would be missing those bytes.
                    const uint32_t skipped = remaining - take;
                    if (skipped > 0) {
                        mChunkCrc = Crc32::update(mChunkCrc, src + take, skipped);
                        mWholeCrc = Crc32::update(mWholeCrc, src + take, skipped);
                    }
                    if (!finishChunk()) {
                        return;
                    }
                    spent += take;
                    continue;
                }
                mFileCrc = Crc32::update(mFileCrc, mReadBuf, take);
            }
        } else {
            // Phase::Rewriting: the CRCs for this chunk are final. Write only.
            size_t bw = 0;
            mFile->write(reinterpret_cast<const char*>(src), take, bw);
            mChunkBw += static_cast<uint32_t>(bw);
            if (bw != take) {
                LOG_INFO("short write rewriting chunk %u: %u of %u bytes\n", mChunkIndex,
                         static_cast<unsigned>(bw), static_cast<unsigned>(take));
                fail(Error::ShortWrite);
                return;
            }
        }

        mChunkPos += take;
        spent += take;

        if (mChunkPos >= mRegion.chunk) {
            if (!finishChunk()) {
                return;
            }
        }
    }
}

bool FlashDumper::finishChunk()
{
    const uint32_t off      = mChunkIndex * mRegion.chunk;
    const uint32_t chunkCrc = Crc32::finalise(mChunkCrc);

    if (mPhase == Phase::Hashing && mMode == Mode::Verifying) {
        const bool matched = !mVerifyReadFailed && Crc32::finalise(mFileCrc) == chunkCrc;
        mFile->close();
        mFile.reset();

        if (!matched) {
            // The file was the right size but not the right bytes -- a
            // truncated copy, a torn write, or a chunk from a different
            // firmware. Rewrite it, without re-hashing: mChunkCrc and mWholeCrc
            // already describe this chunk's memory.
            LOG_INFO("chunk %u on disk does not match memory, rewriting\n", mChunkIndex);
            char name[kPathLen];
            chunkPath(off, name, sizeof(name));
            mFile = mKernel.fs.file(name);
            if (!mFile || !mFile->open(true, true)) {
                mFile.reset();
                fail(Error::OpenFailed);
                return false;
            }
            mPhase    = Phase::Rewriting;
            mChunkPos = 0;
            mChunkBw  = 0;
            return true;
        }

        // Skipped the write, so report the size the file actually has as the
        // bytes "written" -- the host checks bw against the chunk size, and the
        // file genuinely holds that many correct bytes.
        mChunkBw = mRegion.chunk;
        ++mChunksVerified;
    } else {
        // Written (or rewritten) this pass. flush() before close() so the
        // bytes are on the medium and not only in a cache: an interrupted dump
        // has to leave complete chunk files behind, which is the whole basis of
        // resuming.
        mFile->flush();
        mFile->close();
        mFile.reset();

        if (mChunkBw != mRegion.chunk) {
            fail(Error::ShortWrite);
            return false;
        }

        // A rewritten chunk is deliberately NOT read back to confirm it. That
        // would cost a second full read of every rewritten chunk to catch a
        // failure mode the short-write check above already covers, and the host
        // re-verifies every chunk from the bytes it actually received -- which
        // is the check that decides whether the dump is good. Re-reading here
        // would also make a rewrite loop possible, which is worse than the
        // uncertainty it removes.
    }

    const bool ok = (mChunkBw == mRegion.chunk);
    mManifest.addChunk(mChunkIndex, mRegion.nchunks(), off, mRegion.chunk, chunkCrc, mChunkBw, ok);

    // Rewritten after every chunk, not once at the end: an interrupted run
    // still leaves an honest, parseable record of exactly how far it got, and
    // each completed chunk is independently verifiable from its own CRC without
    // the rest of the dump.
    if (!flushManifest()) {
        return false;
    }

    ++mChunksDone;
    LOG_INFO("chunk %u/%u off=%08lX crc32=%08lX %s\n", mChunkIndex, mRegion.nchunks(),
             static_cast<unsigned long>(off), static_cast<unsigned long>(chunkCrc),
             mMode == Mode::Verifying && mPhase == Phase::Hashing ? "verified" : "written");

    ++mChunkIndex;
    if (mChunkIndex >= mRegion.nchunks()) {
        finishPass();
        return false; // Not a failure: there is simply no more work.
    }

    return openChunk();
}

bool FlashDumper::flushManifest()
{
    if (mManifest.overflowed()) {
        fail(Error::ManifestOverflow);
        return false;
    }

    char path[kPathLen];
    manifestPath(path, sizeof(path));

    std::unique_ptr<SDK::Interface::IFile> f = mKernel.fs.file(path);
    if (!f || !f->open(true, true)) {
        fail(Error::ManifestFailed);
        return false;
    }

    size_t bw = 0;
    f->write(mManifest.text(), mManifest.length(), bw);
    f->flush();
    f->close();

    if (bw != mManifest.length()) {
        fail(Error::ManifestFailed);
        return false;
    }
    return true;
}

void FlashDumper::finishPass()
{
    mWholeCrcFinal = Crc32::finalise(mWholeCrc);
    mManifest.addWhole(mWholeCrcFinal);

    // Spot bytes at three absolute addresses: the region's first bytes, its
    // midpoint and its last bytes. Independent of the CRC by construction --
    // they are anchored to addresses rather than to a checksum of the content
    // -- so they catch a dump that is self-consistent but misaligned, which is
    // exactly what a CRC cannot see.
    const uint32_t spots[] = {
        0,
        mRegion.size / 2,
        mRegion.size - static_cast<uint32_t>(DumpManifest::kSpotBytes),
    };
    for (const uint32_t at : spots) {
        mManifest.addSpot(mRegion.base + at, mWindow + at, DumpManifest::kSpotBytes);
    }

    if (!flushManifest()) {
        return;
    }

    mState = State::Done;
    LOG_INFO("dump complete: %u chunks (%u re-verified), whole_image_crc32=%08lX\n", mChunksDone,
             mChunksVerified, static_cast<unsigned long>(mWholeCrcFinal));
}
