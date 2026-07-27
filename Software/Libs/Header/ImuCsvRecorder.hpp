/**
 ******************************************************************************
 * @file    ImuCsvRecorder.hpp
 * @brief   Research-mode recorder: raw 100 Hz FUSION_RAW stream to CSV.
 ******************************************************************************
 */

#ifndef IMU_CSV_RECORDER_HPP
#define IMU_CSV_RECORDER_HPP

#include <cstddef>
#include <cstdint>

/**
 * @class ImuCsvRecorder
 * @brief Streams the untouched IMU sample stream to a CSV file.
 *
 * Shot detection and stroke classification can only be tuned against real,
 * labelled squash recordings, so the app ships the means to collect them
 * first. A recorded file replays through both the simulator
 * (@c Sensor::ImuFusion CSV playback) and the host tests, which is the whole
 * development loop for every later tier.
 *
 * The class is deliberately SDK-free: the destination is an injected @c ISink,
 * so the same code writes to the watch file system on device and to a memory
 * buffer in host tests. There is no allocation after construction, no
 * exceptions, a single fixed internal buffer, and rows are formatted without
 * @c printf (deterministic cost, no locale, no float formatting).
 *
 * Format — byte-compatible with @c Sensor::ImuFusionSource's playback parser,
 * which tolerates one leading header row:
 * @code
 * t_ms,ax,ay,az,gx,gy,gz
 * 0,12,-8,4096,3,-1,0
 * 10,15,-9,4090,120,-64,17
 * @endcode
 *
 * Values are RAW sensor units as delivered by the BMI270 through
 * @c SDK::SensorDataParser::FusionRaw (accel 4096 LSB/g at +/-8 g, gyro
 * 16.4 LSB/dps at +/-2000 dps). Raw units are kept on purpose: scaling here
 * would bake this app's assumptions into the recording, and clipped samples
 * must stay visible as the range limits (a saturated stroke is signal, not
 * noise).
 *
 * Timestamps are milliseconds relative to @c begin(), so every file starts at
 * 0. The subtraction is unsigned, so it stays correct across the 32-bit
 * millisecond tick wrap (~49.7 days).
 */
class ImuCsvRecorder {
public:
    /// One 6-axis sample in raw sensor units.
    struct Sample {
        int16_t ax = 0;
        int16_t ay = 0;
        int16_t az = 0;
        int16_t gx = 0;
        int16_t gy = 0;
        int16_t gz = 0;
    };

    /// Write destination. Implemented over SDK::Interface::IFile on device and
    /// over a memory buffer in host tests.
    class ISink {
    public:
        virtual ~ISink() = default;

        /// Append @p len bytes. Return false on any short write or error.
        virtual bool write(const char* data, size_t len) = 0;

        /// Push buffered bytes to storage. Return false on error.
        virtual bool flush() = 0;
    };

    /// Why a recording is no longer running.
    enum class Stop : uint8_t {
        NONE = 0,        ///< Still recording, or never started.
        REQUESTED,       ///< end() was called.
        SIZE_LIMIT,      ///< Hit Limits::maxBytes.
        DURATION_LIMIT,  ///< Hit Limits::maxDurationMs.
        SINK_ERROR,      ///< The sink reported a write/flush failure.
    };

    /// Recording caps. Both are enforced; whichever trips first stops the run.
    struct Limits {
        uint32_t maxBytes      = skDefaultMaxBytes;
        uint32_t maxDurationMs = skDefaultMaxDurationMs;
    };

    // -- Sizing ---------------------------------------------------------------

    /// Longest row this class can emit: 10 digits of uint32 ms + 6 fields of
    /// "-32768" + 6 separators + newline = 53; rounded up for headroom. Used to
    /// decide when the buffer must be flushed, so it must never be an
    /// underestimate (static_assert'd against the formatter in the .cpp).
    static constexpr size_t skMaxRowBytes = 64;

    /// Write staging buffer. Chosen so a 10-sample sensor batch (~440 B) is
    /// absorbed without a mid-batch flush, while costing 1 KB of the service's
    /// 500 KB budget.
    static constexpr size_t skBufferBytes = 1024;

    /// Default size cap.
    ///
    /// MEASURED, not assumed: a row averages ~44 bytes and is at most 53, so
    /// 100 Hz costs ~4.3 KiB/s and 30 minutes of recording is ~7.9 MB — not the
    /// ~2.9 MB a 16 B/row estimate would suggest. 8 MB therefore buys just over
    /// 30 minutes.
    ///
    /// TODO: this is a self-defence limit, not a device-aware one. The SDK file
    /// system interface exposes no free-space query, so the recorder cannot size
    /// itself to the sandbox. Confirm the activity partition's free space on
    /// real hardware and either lower this default or add a free-space API.
    static constexpr uint32_t skDefaultMaxBytes = 8u * 1024u * 1024u;

    /// Default duration cap: 30 minutes, about one squash match's worth of
    /// play, and the horizon the size cap is matched to.
    static constexpr uint32_t skDefaultMaxDurationMs = 30u * 60u * 1000u;

    ImuCsvRecorder() = default;

    ImuCsvRecorder(const ImuCsvRecorder&)            = delete;
    ImuCsvRecorder& operator=(const ImuCsvRecorder&) = delete;

    /**
     * @brief Start a recording and emit the header row.
     * @param sink   Destination; must outlive the recording.
     * @param nowMs  Monotonic tick that becomes t=0.
     * @param limits Size and duration caps.
     * @return false if the header could not be written (sink error); the
     *         recorder is then left stopped with Stop::SINK_ERROR.
     */
    bool begin(ISink& sink, uint32_t nowMs, const Limits& limits);

    /// Start with the default caps. (Separate overload rather than a defaulted
    /// argument: a nested class's default member initializers are not available
    /// inside the enclosing class body, so `= Limits{}` there is ill-formed.)
    bool begin(ISink& sink, uint32_t nowMs);

    /**
     * @brief Append one sample.
     * @param nowMs  Monotonic tick of the sample.
     * @param sample Raw 6-axis values.
     * @return true if the sample was accepted; false if the recorder is not
     *         running, or if this sample tripped a cap or a sink error (the
     *         run is then closed out and stopReason() says why).
     *
     * A cap is checked before the row is emitted, so the file never exceeds
     * maxBytes and never carries a sample past maxDurationMs.
     */
    bool onSample(uint32_t nowMs, const Sample& sample);

    /**
     * @brief Flush and close out a running recording.
     * @return true iff the recording is intact on storage.
     *
     * Safe to call when not recording, so the service can call it
     * unconditionally on stop/pause/discard. The result answers "is there a
     * usable recording?", not "did this call do work":
     *
     *  - never started, or already ended cleanly -> true
     *  - stopped by a size or duration cap       -> true (complete, just short;
     *                                               stopReason() says which cap)
     *  - stopped by a sink error                 -> false, on this and every
     *                                               later call
     */
    bool end();

    bool     isRecording() const { return mRecording; }
    Stop     stopReason()  const { return mStop; }
    /// Bytes handed to the sink, including the header and any buffered-then-flushed rows.
    uint32_t bytesWritten() const { return mBytesWritten; }
    /// Samples actually written as rows.
    uint32_t sampleCount() const { return mSampleCount; }

private:
    /// Header row, without which a file is ambiguous about column order.
    static constexpr const char* skHeader = "t_ms,ax,ay,az,gx,gy,gz\n";

    /// Append @p value in decimal to @p out; returns the number of chars written.
    static size_t formatInt(int32_t value, char* out);

    /// Copy @p len bytes into the staging buffer, flushing it first if needed.
    bool stage(const char* data, size_t len);

    /// Hand the staging buffer to the sink and reset it.
    bool drain();

    /// Close the run, recording @p reason. Flushes best-effort unless the sink
    /// has already failed.
    bool finish(Stop reason);

    ISink*   mSink         = nullptr;
    Limits   mLimits;
    bool     mRecording    = false;
    Stop     mStop         = Stop::NONE;
    uint32_t mStartMs      = 0;
    uint32_t mBytesWritten = 0;
    uint32_t mSampleCount  = 0;
    size_t   mBufLen       = 0;
    char     mBuf[skBufferBytes]{};
};

#endif // IMU_CSV_RECORDER_HPP
