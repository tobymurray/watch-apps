/**
 ******************************************************************************
 * @file    PackCrcVerifier.cpp
 * @brief   Background, resumable CRC-32 verifier for one file.
 ******************************************************************************
 */

#include "PackCrcVerifier.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include "PackTrustMarker.hpp"

namespace {

constexpr size_t kFooterSize = 4; // trailing u32 LE CRC-32.
constexpr const char* kTrustSuffix = ".trust";

/// CRC-32/ISO-HDLC table. Sanity-checkable against the well-known check value
/// 0xCBF43926 for ASCII "123456789" (the standard test vector for this
/// polynomial/variant, used by PNG/zlib and most language standard libraries).
const uint32_t* crc32Table()
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    return table.data();
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length)
{
    const uint32_t* table = crc32Table();
    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

uint32_t readU32LE(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

PackCrcVerifier::PackCrcVerifier(const SDK::Kernel& kernel, std::string path)
    : mKernel(kernel), mLog(kernel), mPath(std::move(path))
{
}

std::string PackCrcVerifier::markerPath() const
{
    return mPath + kTrustSuffix;
}

PackCrcVerifier::Status PackCrcVerifier::start()
{
    if (mStatus == Status::InProgress) {
        return mStatus; // no-op per doc comment
    }

    mFile = mKernel.fs.file(mPath.c_str());
    if (!mFile || !mFile->open(false, false)) {
        mFile.reset();
        mStatus = Status::IoError;
        mLog.logf("PackCrcVerifier::start() failed to open %s\n", mPath.c_str());
        return mStatus;
    }

    mFileSize = mFile->size();
    if (mFileSize < kFooterSize) {
        mFile->close();
        mFile.reset();
        mStatus = Status::IoError;
        mLog.logf("PackCrcVerifier::start() %s too short (%llu bytes)\n",
                  mPath.c_str(), static_cast<unsigned long long>(mFileSize));
        return mStatus;
    }
    mCrcStart = mFileSize - kFooterSize;

    if (!readFooterCrc(mDeclaredCrc)) {
        mFile->close();
        mFile.reset();
        mStatus = Status::IoError;
        mLog.logf("PackCrcVerifier::start() failed to read footer CRC of %s\n", mPath.c_str());
        return mStatus;
    }

    // Named local, not markerPath().c_str() inline: PackTrustMarker only
    // stores the raw pointer it's given, so the string backing it must
    // outlive marker's use, not just the one expression that produced it.
    const std::string markerPathStr = markerPath();
    PackTrustMarker marker(mKernel, markerPathStr.c_str());
    uint64_t markedSize = 0;
    uint32_t markedCrc  = 0;
    PackTrustMarker::Trust trust = marker.read(markedSize, markedCrc);

    // A marker of either kind is a cached verdict for exactly this (size,
    // crc), so both kinds short-circuit the scan. The (size, crc) guard is
    // what makes that safe: any file that is not byte-for-byte the same
    // length AND self-declaring the same CRC falls through to a real scan.
    if (markedSize == mFileSize && markedCrc == mDeclaredCrc
            && (trust == PackTrustMarker::Trust::Good || trust == PackTrustMarker::Trust::Bad)) {
        const bool good = (trust == PackTrustMarker::Trust::Good);
        mFile->close();
        mFile.reset();
        mStatus = good ? Status::Verified : Status::Mismatched;
        mLog.logf("PackCrcVerifier::start() %s verdict already cached in marker as %s "
                  "(size=%llu crc=0x%08lX) -- skipping scan\n",
                  mPath.c_str(), good ? "Good" : "Bad",
                  static_cast<unsigned long long>(mFileSize),
                  static_cast<unsigned long>(mDeclaredCrc));
        return mStatus;
    }

    // No marker matching this exact file: begin (or restart) the scan from
    // byte 0. Covers Absent (never checked) and a Good-or-Bad marker whose
    // (size, crc) no longer match this file -- i.e. it was replaced since
    // the marker was written, so the cached verdict is about other bytes.
    mFile->seek(0);
    mBytesDone       = 0;
    mCrc             = 0xFFFFFFFFu;
    mLastLoggedBytes = 0;
    mStartedAtMs     = mKernel.sys.getTimeMs();
    mLastLoggedAtMs  = mStartedAtMs;
    mStatus          = Status::InProgress;

    // Reaching here with a marker present means its (size, crc) did not match
    // this file -- both cached verdicts are short-circuited above -- so any
    // marker we saw is by definition stale.
    const char* trustDesc = trust == PackTrustMarker::Trust::Bad ? "Bad-but-stale"
                           : trust == PackTrustMarker::Trust::Good ? "Good-but-stale"
                                                                    : "Absent";
    mLog.logf("PackCrcVerifier::start() %s size=%llu declaredCrc=0x%08lX "
              "priorMarker=%s -> beginning scan from 0\n",
              mPath.c_str(), static_cast<unsigned long long>(mFileSize),
              static_cast<unsigned long>(mDeclaredCrc), trustDesc);
    return mStatus;
}

void PackCrcVerifier::reset()
{
    if (mFile) {
        mFile->close();
        mFile.reset();
    }
    mFileSize    = 0;
    mCrcStart    = 0;
    mBytesDone   = 0;
    mCrc         = 0xFFFFFFFFu;
    mDeclaredCrc = 0;
    mStatus      = Status::Idle;
}

PackCrcVerifier::Status PackCrcVerifier::step(size_t maxBytes)
{
    if (mStatus != Status::InProgress) {
        return mStatus;
    }

    uint8_t buf[kIoChunkBytes];
    size_t budgetLeft = maxBytes;

    // Spend the whole budget before returning, in kIoChunkBytes reads. The
    // loop always terminates: every iteration either consumes at least one
    // byte of budget, or take==0 (only reachable when the scannable region is
    // empty) which finishes the pass and clears InProgress.
    while (mStatus == Status::InProgress && budgetLeft > 0) {
        const size_t take = static_cast<size_t>(
            std::min<uint64_t>(std::min<size_t>(budgetLeft, kIoChunkBytes), mCrcStart - mBytesDone));

        size_t got = 0;
        if (!mFile->read(reinterpret_cast<char*>(buf), take, got) || got != take) {
            mFile->close();
            mFile.reset();
            mStatus = Status::IoError;
            mLog.logf("PackCrcVerifier::step() read failed at offset %llu of %s\n",
                      static_cast<unsigned long long>(mBytesDone), mPath.c_str());
            return mStatus;
        }
        mCrc = crc32Update(mCrc, buf, take);
        mBytesDone += take;
        budgetLeft -= take;

        if (mBytesDone >= mCrcStart) {
            finish((mCrc ^ 0xFFFFFFFFu) == mDeclaredCrc, mDeclaredCrc);
            break;
        }
        if (take == 0) {
            break; // Defensive: no progress possible, don't spin on the budget.
        }
    }

    // Progress logging only while still scanning: finish() emits its own
    // terminal line, and a mid-scan progress line after it would be noise.
    if (mStatus != Status::InProgress) {
        return mStatus;
    }

    const uint32_t nowMs = mKernel.sys.getTimeMs();
    constexpr uint64_t kLogEveryBytes = 4 * 1024 * 1024; // ~4MB
    if ((mBytesDone - mLastLoggedBytes) >= kLogEveryBytes || (nowMs - mLastLoggedAtMs) >= 5000) {
        const uint32_t elapsedMs = nowMs - mStartedAtMs;
        const double throughputKBs = elapsedMs > 0
            ? (static_cast<double>(mBytesDone) / 1024.0) / (static_cast<double>(elapsedMs) / 1000.0)
            : 0.0;
        mLog.logf("PackCrcVerifier::step() %s %llu/%llu bytes elapsed=%lums throughput=%.1fKB/s\n",
                  mPath.c_str(), static_cast<unsigned long long>(mBytesDone),
                  static_cast<unsigned long long>(mCrcStart),
                  static_cast<unsigned long>(elapsedMs), throughputKBs);
        mLastLoggedBytes = mBytesDone;
        mLastLoggedAtMs  = nowMs;
    }

    return mStatus;
}

bool PackCrcVerifier::readFooterCrc(uint32_t& out)
{
    if (!mFile->seek(static_cast<size_t>(mCrcStart))) {
        return false;
    }
    uint8_t footer[kFooterSize];
    size_t got = 0;
    if (!mFile->read(reinterpret_cast<char*>(footer), kFooterSize, got) || got != kFooterSize) {
        return false;
    }
    out = readU32LE(footer);
    return true;
}

void PackCrcVerifier::finish(bool matched, uint32_t declaredCrc)
{
    const uint32_t computedCrc = mCrc ^ 0xFFFFFFFFu;
    const uint32_t elapsedMs   = mKernel.sys.getTimeMs() - mStartedAtMs;

    const std::string markerPathStr = markerPath(); // see start()'s comment on why this must be named
    PackTrustMarker marker(mKernel, markerPathStr.c_str());

    mFile->close();
    mFile.reset();

    if (matched) {
        marker.writeGood(mFileSize, declaredCrc);
        mStatus = Status::Verified;
        mLog.logf("PackCrcVerifier::step() DONE %s elapsed=%lums declaredCrc=0x%08lX "
                  "computedCrc=0x%08lX -> Verified, marker written\n",
                  mPath.c_str(), static_cast<unsigned long>(elapsedMs),
                  static_cast<unsigned long>(declaredCrc), static_cast<unsigned long>(computedCrc));
    } else {
        marker.writeBad(mFileSize, declaredCrc);
        mStatus = Status::Mismatched;
        mLog.logf("PackCrcVerifier::step() DONE %s elapsed=%lums declaredCrc=0x%08lX "
                  "computedCrc=0x%08lX -> Mismatched, Bad marker written\n",
                  mPath.c_str(), static_cast<unsigned long>(elapsedMs),
                  static_cast<unsigned long>(declaredCrc), static_cast<unsigned long>(computedCrc));
    }
}
