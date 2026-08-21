/**
 ******************************************************************************
 * @file    RawLog.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   raw/<run>-<seq>.bin: every sample, as the wire carried it.
 ******************************************************************************
 *
 * NORMATIVE FORMAT. `Tools/raw_decode.py` parses exactly what is described
 * here, and a ctest decodes a file written by the real writer, so this comment
 * and that script are the two halves of one contract.
 *
 * ---------------------------------------------------------------------------
 * Why this exists, when there is already a run log and a profile
 *
 * Both of those are **derived**. `profile.json` holds statistics; the run log
 * holds per-interval statistics. A statistic embeds the question it was
 * computed to answer, and on a first profile of an undocumented platform the
 * question will be wrong. **An analysis without its inputs cannot be
 * corrected**, and every summary in this app is a lossy answer to a question
 * somebody will want to re-ask.
 *
 * Concretely, things this file can answer and the aggregates cannot:
 *
 *   - the *shape* of a delivery gap -- did the stream stop, or thin, or arrive
 *     in one burst? A `longest_gap_ms` of 4020 says none of that;
 *   - whether two sensors' samples are simultaneous, which is layer 8's whole
 *     subject and needs both streams' timestamps side by side;
 *   - a quantile nobody thought to compute, or a filter nobody thought to run;
 *   - what a frame from one of the five undocumented types actually contains,
 *     field by field, over time -- the only description of those frames that
 *     will ever exist;
 *   - whether a statistic in the profile is *right*, which is otherwise a
 *     matter of trusting this app.
 *
 * ---------------------------------------------------------------------------
 * Verbatim, and that word is load-bearing
 *
 * A record is the `EventData` message's own header plus **the frame bytes
 * exactly as they arrived**. Nothing is interpreted on the way in: not the
 * field count, not the timestamps, not the field types. A frame whose stride is
 * not a whole number of fields is written; a timestamp that goes backwards is
 * written; a `mTimeStampUs` of 60000 is written. The app's opinion of the frame
 * is in the profile, and the frame is here, and the two can be checked against
 * each other.
 *
 * That is why the format is binary rather than CSV. Not for size, though it is
 * a third of the size: a CSV writer has to decide how to render a field, and
 * deciding is interpreting. Copying `count * stride` bytes decides nothing.
 *
 * ---------------------------------------------------------------------------
 * What it costs, measured where possible and stated where not
 *
 * A 3-field sample is `sizeof(Data) + 2 * sizeof(Field)` = 24 bytes on the
 * wire. At the accelerometer's measured ~48 Hz (SleepLab ledger row S3) that is
 * **1 152 B/s, or 4.15 MB an hour, for one sensor**. A dozen subscribed types,
 * most of them far slower, comes to roughly 6-10 MB an hour; a twelve-hour soak
 * is therefore **70-120 MB**.
 *
 * `MapManager` CRC-verified 160.5 MiB of map packs on this volume, so that is
 * the right order of magnitude to fit and the wrong order to be casual about.
 * Hence: a byte cap, chunk rotation, and a **count of what was dropped when the
 * cap was reached**, in the run manifest. A raw log that silently stopped would
 * be worse than no raw log, because the file would still look complete.
 *
 * **Write throughput is inferred, not confirmed.** Row S9 confirmed one
 * open-seek-write-flush-close a minute for 8.45 h. This buffers to 8 KB and
 * flushes when full, which at ~10 MB/h is roughly one cycle every three
 * seconds -- about 20x the confirmed rate. The first soak measures it: the run
 * manifest carries the bytes written and the run's own duration.
 *
 * **And capture is not free of the thing it measures.** Writing ~1 MB/s to
 * flash costs power and CPU, so a dt distribution measured with raw capture on
 * is not the same measurement as one taken with it off. The manifest records
 * `raw_capture` for exactly that reason, and turning it off is a legitimate
 * experiment rather than a degraded mode.
 *
 * ---------------------------------------------------------------------------
 * File layout
 *
 *   raw/<run_id>-<seq>.bin       seq starts at 0 and increments per chunk
 *
 * Chunked, following `FwDump`: a chunk that is interrupted by the cable loses
 * itself and not the run, and a host can decode chunk 3 without chunk 4 having
 * ever been written. Little-endian throughout, which is this platform's.
 *
 * Chunk header, 32 bytes, once per file:
 *
 *   off  size  field
 *   0    4     magic         "SLRW"
 *   4    4     schema        kRawSchema
 *   8    4     runId
 *   12   4     seq
 *   16   4     startUptimeMs kernel.sys.getTimeMs() when the chunk opened
 *   20   8     startWallUtc  time(nullptr), or -1 when unreadable
 *   28   4     reserved      zero
 *
 * Record, 16-byte header then payload, repeated to end of file:
 *
 *   off  size  field
 *   0    4     typeValue     the sensor type, e.g. 0x10
 *   4    4     handle        full 32 bits, because a truncated handle is a
 *                            finding and this file is the evidence
 *   8    4     arrivalMs     uptime when the batch arrived -- the loop's clock,
 *                            which is a different quantity from the sample
 *                            timestamps inside the payload
 *   12   2     count         samples in this batch
 *   14   2     stride        bytes per sample
 *   16   count*stride        the frame bytes, verbatim
 *
 * `typeValue` rather than only the handle, because a handle means nothing
 * outside the run that issued it and a file should be decodable on its own.
 * Both are written because they are both evidence.
 *
 * A truncated final record is possible -- the cable does not wait for a flush.
 * `raw_decode.py` stops at the first short record and says how many bytes it
 * ignored, rather than guessing.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_RAWLOG_HPP
#define SENSORLAB_RAWLOG_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/SensorLayer/SensorData.hpp"

namespace SensorLab::Profile
{

/// Bumped when the record or chunk layout changes. `raw_decode.py` refuses a
/// schema it does not know rather than misreading a field.
constexpr uint32_t kRawSchema = 1;

constexpr char     kRawMagic[4]     = { 'S', 'L', 'R', 'W' };
constexpr size_t   kRawChunkHeader  = 32;
constexpr size_t   kRawRecordHeader = 16;

/// Write buffer. 8 KB: at the ~10 MB/h a dozen subscribed types produce, this
/// is one flush every three seconds, against the one-a-minute that row S9
/// confirmed. Larger would reduce the write rate further and lose more to a
/// cable event; smaller would push the write rate somewhere nothing has
/// measured. It is a static member of the service, not a heap allocation.
constexpr size_t kRawBufferBytes = 8192;

/**
 * @brief Append-only binary writer for the format above.
 *
 * Buffers, rotates chunks, caps by bytes, and **counts what it dropped**. The
 * counting is the part that matters: a capture that stopped without saying so
 * would leave a file that still looked complete, which is the failure this app
 * exists to make impossible.
 */
class RawLog
{
public:
    explicit RawLog(const SDK::Kernel &kernel);

    /// Open capture for a run. @p maxBytes is the total across every chunk;
    /// @p chunkBytes rotates the file. Zero @p maxBytes disables capture, which
    /// is a valid run and is recorded as such rather than looking like a
    /// failure.
    void begin(uint32_t runId, uint64_t maxBytes, uint32_t chunkBytes,
               uint32_t uptimeMs, int64_t wallUtc);

    /**
     * @brief One batch, verbatim.
     *
     * Called from the sample path, so it does no work beyond a bounds check and
     * a memcpy into the buffer. Returns false when the batch was dropped --
     * cap reached, or a write failed -- and the drop is counted either way.
     */
    bool write(uint32_t typeValue, uint32_t handle, uint32_t arrivalMs,
               uint16_t count, uint16_t stride, const SDK::Sensor::Data *base);

    /// Push the buffer to storage. Called at each interval boundary and at run
    /// close, so a cable event loses at most one interval's tail rather than
    /// the buffer's whole contents.
    bool flush();

    /// Flush and stop. Safe to call twice.
    void end();

    bool     capturing()  const { return mCapturing; }
    uint64_t bytes()      const { return mBytes; }
    uint32_t chunks()     const { return mCapturing || mBytes > 0 ? mSeq + 1 : 0; }
    uint32_t batches()    const { return mBatches; }
    uint64_t samples()    const { return mSamples; }
    /// Batches that never reached storage. Non-zero means the raw log is
    /// incomplete, and the manifest says so.
    uint32_t dropped()    const { return mDropped; }
    uint32_t failures()   const { return mFailures; }
    /// True once the byte cap stopped capture. Distinct from a write failure,
    /// and the report distinguishes them: one is a decision and one is a fault.
    bool     capReached() const { return mCapReached; }

private:
    bool openChunk(uint32_t uptimeMs, int64_t wallUtc);
    bool appendBuffer();
    void path(char *out, size_t outSize, uint32_t seq) const;

    const SDK::Kernel &mKernel;

    uint8_t  mBuffer[kRawBufferBytes] {};
    size_t   mUsed        = 0;

    uint32_t mRunId       = 0;
    uint32_t mSeq         = 0;
    uint64_t mMaxBytes    = 0;
    uint32_t mChunkBytes  = 0;
    uint64_t mChunkUsed   = 0;

    uint64_t mBytes       = 0;
    uint32_t mBatches     = 0;
    uint64_t mSamples     = 0;
    uint32_t mDropped     = 0;
    uint32_t mFailures    = 0;

    bool     mCapturing   = false;
    bool     mCapReached  = false;

    /// Carried so a rotation can stamp the new chunk's header without the
    /// caller passing the clocks on every batch.
    uint32_t mLastUptimeMs = 0;
    int64_t  mLastWallUtc  = -1;
};

} // namespace SensorLab::Profile

#endif // SENSORLAB_RAWLOG_HPP
