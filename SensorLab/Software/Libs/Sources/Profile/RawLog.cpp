/**
 ******************************************************************************
 * @file    RawLog.cpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   raw/<run>-<seq>.bin. NORMATIVE FORMAT is in the header.
 ******************************************************************************
 */

#include "Profile/RawLog.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

#define LOG_MODULE_PRX      "RawLog"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace SensorLab::Profile
{

namespace
{

constexpr char kRawDir[] = "raw";

/// Little-endian stores, written byte by byte rather than by casting a pointer.
///
/// This platform is little-endian and a `memcpy` of a `uint32_t` would work, but
/// the *format* is little-endian by specification rather than by accident, and a
/// reader of this file should be able to see that without knowing the target.
/// It costs four stores per header field, twelve times per batch.
void put32(uint8_t *p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

void put16(uint8_t *p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

void put64(uint8_t *p, uint64_t v)
{
    put32(p, static_cast<uint32_t>(v));
    put32(p + 4, static_cast<uint32_t>(v >> 32));
}

} // namespace

RawLog::RawLog(const SDK::Kernel &kernel)
    : mKernel(kernel)
{
}

void RawLog::path(char *out, size_t outSize, uint32_t seq) const
{
    std::snprintf(out, outSize, "%s/%lu-%lu.bin", kRawDir,
                  static_cast<unsigned long>(mRunId),
                  static_cast<unsigned long>(seq));
}

void RawLog::begin(uint32_t runId, uint64_t maxBytes, uint32_t chunkBytes,
                   uint32_t uptimeMs, int64_t wallUtc)
{
    mRunId        = runId;
    mSeq          = 0;
    mMaxBytes     = maxBytes;
    mChunkBytes   = (chunkBytes > kRawChunkHeader) ? chunkBytes : 0;
    mChunkUsed    = 0;
    mUsed         = 0;
    mBytes        = 0;
    mBatches      = 0;
    mSamples      = 0;
    mDropped      = 0;
    mFailures     = 0;
    mCapReached   = false;
    mLastUptimeMs = uptimeMs;
    mLastWallUtc  = wallUtc;

    if (maxBytes == 0 || mChunkBytes == 0) {
        // Capture off. A valid run -- it measures the sensor layer without the
        // flash traffic raw capture adds -- and recorded as such rather than
        // looking like a failure.
        mCapturing = false;
        LOG_INFO("raw capture off for run %lu\n",
                 static_cast<unsigned long>(runId));
        return;
    }

    mKernel.fs.mkdir(kRawDir);
    mCapturing = openChunk(uptimeMs, wallUtc);
    if (!mCapturing) {
        LOG_WARNING("raw capture could not open its first chunk; continuing "
                    "without it\n");
    } else {
        LOG_INFO("raw capture on for run %lu: cap %llu MB, chunk %lu KB\n",
                 static_cast<unsigned long>(runId),
                 static_cast<unsigned long long>(maxBytes / (1024ull * 1024ull)),
                 static_cast<unsigned long>(mChunkBytes / 1024u));
    }
}

bool RawLog::openChunk(uint32_t uptimeMs, int64_t wallUtc)
{
    char p[40];
    path(p, sizeof(p), mSeq);

    uint8_t header[kRawChunkHeader] {};
    std::memcpy(header, kRawMagic, sizeof(kRawMagic));
    put32(header + 4,  kRawSchema);
    put32(header + 8,  mRunId);
    put32(header + 12, mSeq);
    put32(header + 16, uptimeMs);
    put64(header + 20, static_cast<uint64_t>(wallUtc));
    put32(header + 28, 0);

    // A chunk is created fresh, so `override = true`. Unlike the run log there
    // is no append-to-existing case: a chunk sequence number is never reused
    // within a run, and a run id is never reused at all.
    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(p);
    if (!file || !file->open(true, true)) {
        mFailures++;
        return false;
    }

    size_t     written = 0;
    const bool ok      = file->write(reinterpret_cast<const char *>(header),
                                     sizeof(header), written)
                         && written == sizeof(header);
    file->flush();
    file->close();

    if (!ok) {
        mFailures++;
        return false;
    }

    mBytes     += written;
    mChunkUsed  = written;
    return true;
}

bool RawLog::appendBuffer()
{
    if (mUsed == 0) {
        return true;
    }

    char p[40];
    path(p, sizeof(p), mSeq);

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(p);
    if (!file || !file->open(true, false)) {
        mFailures++;
        mUsed = 0;   // the buffer is gone either way; do not re-write it later
        return false;
    }

    // `open(write, override=false)` positions at offset 0, not end of file
    // (ledger row P6). The seek is what makes this an append -- the same trap
    // the run log has, and the same fix.
    if (!file->seek(file->size())) {
        file->close();
        mFailures++;
        mUsed = 0;
        return false;
    }

    size_t     written = 0;
    const bool ok      = file->write(reinterpret_cast<const char *>(mBuffer),
                                     mUsed, written)
                         && written == mUsed;
    // Flush before close, not instead of it: bytes in the FAT cache when the
    // cable goes in are bytes that never happened.
    file->flush();
    file->close();

    if (!ok) {
        mFailures++;
        mUsed = 0;
        return false;
    }

    mBytes     += written;
    mChunkUsed += written;
    mUsed       = 0;
    return true;
}

bool RawLog::write(uint32_t typeValue, uint32_t handle, uint32_t arrivalMs,
                   uint16_t count, uint16_t stride,
                   const SDK::Sensor::Data *base)
{
    if (!mCapturing || base == nullptr || count == 0 || stride == 0) {
        return false;
    }

    mLastUptimeMs = arrivalMs;

    const size_t payload = static_cast<size_t>(count) * stride;
    const size_t need    = kRawRecordHeader + payload;

    // A batch larger than the whole buffer cannot be written atomically, and a
    // record split across two flushes would still decode -- but it would also
    // mean the buffer size silently bounded what the app can record. Counted as
    // a drop instead, loudly, because a batch this large is itself a finding.
    if (need > kRawBufferBytes) {
        mDropped++;
        LOG_WARNING("batch of %u samples x %u bytes exceeds the raw buffer; "
                    "dropped and counted\n",
                    static_cast<unsigned>(count), static_cast<unsigned>(stride));
        return false;
    }

    // The byte cap. Checked against what is already on storage plus what is
    // buffered plus this record, so the cap is a real ceiling rather than an
    // approximate one.
    if (mBytes + mUsed + need > mMaxBytes) {
        if (!mCapReached) {
            mCapReached = true;
            flush();
            LOG_INFO("raw capture reached its %llu MB cap after %lu batches; "
                     "stopping capture and counting what follows\n",
                     static_cast<unsigned long long>(mMaxBytes / (1024ull * 1024ull)),
                     static_cast<unsigned long>(mBatches));
        }
        mDropped++;
        return false;
    }

    if (mUsed + need > kRawBufferBytes) {
        appendBuffer();
    }

    // Chunk rotation. After the flush above, so a chunk boundary never falls in
    // the middle of a buffered record.
    if (mChunkBytes > 0 && mChunkUsed + need > mChunkBytes) {
        mSeq++;
        mChunkUsed = 0;
        if (!openChunk(mLastUptimeMs, mLastWallUtc)) {
            mCapturing = false;
            mDropped++;
            LOG_WARNING("raw capture could not rotate to chunk %lu; stopping\n",
                        static_cast<unsigned long>(mSeq));
            return false;
        }
    }

    uint8_t *p = mBuffer + mUsed;
    put32(p,      typeValue);
    put32(p + 4,  handle);
    put32(p + 8,  arrivalMs);
    put16(p + 12, count);
    put16(p + 14, stride);

    // The frame, verbatim. Nothing is interpreted here -- not the field count,
    // not the timestamps, not the field types. A stride that is not a whole
    // number of fields is copied; a timestamp that goes backwards is copied.
    // The app's opinion of the frame is in the profile; the frame is here.
    std::memcpy(p + kRawRecordHeader, base, payload);

    mUsed    += need;
    mBatches += 1;
    mSamples += count;
    return true;
}

bool RawLog::flush()
{
    if (!mCapturing && mUsed == 0) {
        return true;
    }
    return appendBuffer();
}

void RawLog::end()
{
    flush();
    if (mCapturing) {
        LOG_INFO("raw capture closed: %lu chunks, %llu bytes, %lu batches, "
                 "%llu samples, %lu dropped, %lu write failures\n",
                 static_cast<unsigned long>(chunks()),
                 static_cast<unsigned long long>(mBytes),
                 static_cast<unsigned long>(mBatches),
                 static_cast<unsigned long long>(mSamples),
                 static_cast<unsigned long>(mDropped),
                 static_cast<unsigned long>(mFailures));
    }
    mCapturing = false;
}

} // namespace SensorLab::Profile
