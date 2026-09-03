/**
 ******************************************************************************
 * @file    HrCsvLog.hpp
 * @brief   Research-mode heart-rate sidecar, on the recording's own clock.
 ******************************************************************************
 */

#ifndef HR_CSV_LOG_HPP
#define HR_CSV_LOG_HPP

#include <cstddef>
#include <cstdint>

#include "ImuCsvRecorder.hpp"

/**
 * @class HrCsvLog
 * @brief The heart rate that went with a recording, sample-aligned to it.
 *
 * The recording and its marker sidecar answer what the body was doing; nothing
 * so far answers what the heart was doing while it did it, and that is the
 * whole of the first phase of a recovery metric. Two questions need it and
 * neither can be answered from the `.fit` file: what the signal's own settling
 * time is when effort stops, and whether the wrist optical sensor is usable
 * during play at all. Both need the heart rate on the same clock as the
 * markers, so a labelled effort transition and the readings around it line up
 * exactly.
 *
 * Written as a third file rather than a column on either of the other two, for
 * the same reason the marker log is separate: the recording's format is
 * byte-compatible with @c Sensor::ImuFusionSource's playback parser, and a
 * seventh column would break the round trip that lets a session recorded on
 * court replay through the simulator.
 *
 * Format, alongside `imu_<stamp>.csv` as `imu_<stamp>_hr.csv`:
 * @code
 * t_ms,bpm_x100,trust,source,optical_x100,external_x100
 * 1000,14250,2,2,14100,14250
 * 2000,14232,2,2,14075,14232
 * @endcode
 *
 * Hundredths of a bpm rather than a decimal, because there is no float
 * formatter here and a fixed-point integer is exact where a hand-rolled one
 * would not be. The resolution matters: consecutive readings have been measured
 * differing by 0.50 and 0.18 bpm over two real rides (`CLAUDE.md`), so rounding
 * to whole bpm would discard exactly the evidence of kernel smoothing that the
 * first phase exists to measure.
 *
 * Both per-source columns are kept alongside the arbitrated reading, so a later
 * question about optical against strap can be answered from the file rather
 * than from a second recording. This is the same reason the FIT file carries
 * @c hr_source, @c hr_optical and @c hr_external rather than only the beat.
 *
 * @c t_ms shares the recording's origin exactly — both are begun from the same
 * tick — so no clock correlation is needed when processing.
 */
class HrCsvLog {
public:
    /// Same destination contract as the sample recorder.
    using ISink = ImuCsvRecorder::ISink;

    /// Which sensor produced a reading; the values are `SDK::HeartRate::Source`.
    enum class Source : uint8_t {
        UNKNOWN  = 0,
        OPTICAL  = 1,
        EXTERNAL = 2,
    };

    /// One arbitrated reading and the per-source readings behind it.
    struct Sample {
        float   bpm         = 0.0f;
        float   opticalBpm  = 0.0f;
        float   externalBpm = 0.0f;
        uint8_t trust       = 0;
        Source  source      = Source::UNKNOWN;
    };

    /// Why the log is no longer accepting readings.
    enum class Stop : uint8_t {
        NONE = 0,   ///< Still running, or never started.
        REQUESTED,  ///< end() was called.
        LIMIT,      ///< Hit skMaxSamples.
        SINK_ERROR, ///< The sink reported a write/flush failure.
    };

    /// Ceiling on readings in one session.
    ///
    /// At the 1 Hz the sensor delivers, 8192 is two hours and sixteen minutes —
    /// past the recorder's own 30-minute default cap, so in an ordinary session
    /// the recording stops first. It is a runaway guard, not a budget.
    static constexpr uint16_t skMaxSamples = 8192;

    /// Longest row: 10 digits of uint32 ms + three 6-digit fixed-point fields +
    /// 3 of trust + 3 of source + 5 commas + newline = 41, rounded up
    /// (static_assert'd in the .cpp).
    static constexpr size_t skMaxRowBytes = 48;

    HrCsvLog() = default;

    HrCsvLog(const HrCsvLog&)            = delete;
    HrCsvLog& operator=(const HrCsvLog&) = delete;

    /**
     * @brief Start a heart-rate log and emit the header row.
     * @param sink  Destination; must outlive the log.
     * @param nowMs Monotonic tick that becomes t=0. Must be the same tick the
     *              sample recorder was begun with, or the files will not share
     *              an origin and every reading will be offset.
     * @return false if the header could not be written.
     */
    bool begin(ISink& sink, uint32_t nowMs);

    /**
     * @brief Record one reading.
     * @return true if it was written and flushed.
     *
     * Flushed per row rather than buffered. At 1 Hz the cost is irrelevant, and
     * a reading is as unreproducible as a marker: the session cannot be
     * re-lived to recover the beats a crash lost.
     */
    bool onSample(uint32_t nowMs, const Sample& sample);

    /// Flush and close. Safe when not running; same "is there a usable file?"
    /// return contract as ImuCsvRecorder::end().
    bool end();

    bool     isRecording() const { return mRunning; }
    Stop     stopReason()  const { return mStop; }
    uint16_t sampleCount() const { return mCount; }
    uint32_t bytesWritten() const { return mBytesWritten; }

private:
    static constexpr const char* skHeader =
        "t_ms,bpm_x100,trust,source,optical_x100,external_x100\n";

    /// Append @p value in decimal to @p out; returns the number of chars written.
    static size_t formatInt(int64_t value, char* out);

    /// Hundredths of a bpm, clamped to what the formatter's width allows.
    static int32_t hundredths(float bpm);

    bool finish(Stop reason);

    ISink*   mSink         = nullptr;
    bool     mRunning      = false;
    Stop     mStop         = Stop::NONE;
    uint32_t mStartMs      = 0;
    uint32_t mBytesWritten = 0;
    uint16_t mCount        = 0;
};

#endif // HR_CSV_LOG_HPP
