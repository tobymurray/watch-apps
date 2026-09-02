/**
 ******************************************************************************
 * @file    ImuMarkerLog.hpp
 * @brief   Research-mode marker sidecar: button-press timestamps for a recording.
 ******************************************************************************
 */

#ifndef IMU_MARKER_LOG_HPP
#define IMU_MARKER_LOG_HPP

#include <cstddef>
#include <cstdint>

#include "ImuCsvRecorder.hpp"

/**
 * @class ImuMarkerLog
 * @brief Exact timestamps for the moments a human marked during a recording.
 *
 * Labelling a recording needs delimiters — where a set starts and ends, and
 * which reps to throw away. Doing that with a gesture means inferring the
 * marker from the same signal it is marking, which does not survive contact
 * with the data: the obvious candidate, tapping the racquet frame, lands at
 * 4-8 g, exactly where a gently played drop lands. A marker that can be
 * confused with a shot is not a marker.
 *
 * A button press is a different kind of thing. It is an exact tick in the same
 * timebase as the samples, so nothing has to be detected, nothing collides with
 * the signal, and it stays valid whatever the sensor range is doing.
 *
 * Written as a sidecar rather than a column on the CSV on purpose. The
 * recording's format is byte-compatible with @c Sensor::ImuFusionSource's
 * playback parser, which is what lets a session recorded on court replay
 * through the simulator and land in host tests as a fixture. A seventh column
 * would break that round trip for the sake of a field that is empty on
 * 99.99% of rows.
 *
 * Format, alongside `imu_<stamp>.csv` as `imu_<stamp>_events.csv`:
 * @code
 * t_ms,seq,kind
 * 3210,1,0
 * 47780,2,0
 * @endcode
 *
 * @c t_ms shares the recording's origin exactly — both are begun from the same
 * sensor tick — so a marker row and a sample row with the same @c t_ms refer to
 * the same instant, and no clock correlation is needed when processing.
 *
 * @c seq is 1-based and gap-free, so a truncated file is obvious and the first
 * and last markers are identifiable without scanning for the file's extent.
 *
 * @c kind is reserved. Today the watch has one marker button and emits only
 * @c Kind::MANUAL; the semantics of a given press (start bookend, void the last
 * rep, end bookend) come from the session protocol, not the device, and the
 * usual convention is that the first and last markers of a file are its
 * bookends and everything between voids the rep before it. The column exists so
 * that a future build with more than one marker gesture does not have to change
 * the format.
 */
class ImuMarkerLog {
public:
    /// Same destination contract as the sample recorder; a marker log is just
    /// another thing that needs bytes put somewhere.
    using ISink = ImuCsvRecorder::ISink;

    /// What a marker meant. Reserved for future use — see the class docs.
    enum class Kind : uint8_t {
        MANUAL = 0, ///< A press of the watch's marker button.
    };

    /// Why the log is no longer accepting markers.
    enum class Stop : uint8_t {
        NONE = 0,   ///< Still running, or never started.
        REQUESTED,  ///< end() was called.
        LIMIT,      ///< Hit skMaxMarkers.
        SINK_ERROR, ///< The sink reported a write/flush failure.
    };

    /// Ceiling on markers in one session.
    ///
    /// Not a storage concern — 512 markers is ~6 KB against the recording's
    /// megabytes. It is a stuck-button guard: the failure this prevents is a
    /// held or bouncing key filling the sidecar with junk that has to be
    /// stripped out later. A protocol session drops fewer than 20.
    static constexpr uint16_t skMaxMarkers = 512;

    /// Longest row: 10 digits of uint32 ms + 5 of seq + 3 of kind + 2 commas
    /// + newline = 21, rounded up (static_assert'd in the .cpp).
    static constexpr size_t skMaxRowBytes = 32;

    ImuMarkerLog() = default;

    ImuMarkerLog(const ImuMarkerLog&)            = delete;
    ImuMarkerLog& operator=(const ImuMarkerLog&) = delete;

    /**
     * @brief Start a marker log and emit the header row.
     * @param sink  Destination; must outlive the log.
     * @param nowMs Monotonic tick that becomes t=0. Must be the same tick the
     *              sample recorder was begun with, or the two files will not
     *              share an origin and every marker will be offset.
     * @return false if the header could not be written.
     */
    bool begin(ISink& sink, uint32_t nowMs);

    /**
     * @brief Record one marker.
     * @param nowMs Monotonic tick of the press, on the recording's clock.
     * @param kind  Reserved; pass Kind::MANUAL.
     * @return true if the marker was written and flushed.
     *
     * Flushed immediately rather than buffered. Markers are tiny, rare, and
     * unreproducible — the session cannot be re-marked afterwards — so the
     * write cost is irrelevant next to losing one to a battery pull or a crash
     * before the next flush. This is the opposite trade to the sample stream,
     * which buffers precisely because its rows are many and individually cheap
     * to lose.
     */
    bool mark(uint32_t nowMs, Kind kind = Kind::MANUAL);

    /// Flush and close. Safe when not running; see ImuCsvRecorder::end() for
    /// the same "is there a usable file?" return contract.
    bool end();

    bool     isRecording()  const { return mRunning; }
    Stop     stopReason()   const { return mStop; }
    /// Markers actually written.
    uint16_t markerCount()  const { return mCount; }
    uint32_t bytesWritten() const { return mBytesWritten; }

private:
    static constexpr const char* skHeader = "t_ms,seq,kind\n";

    /// Append @p value in decimal to @p out; returns the number of chars written.
    static size_t formatUint(uint32_t value, char* out);

    bool finish(Stop reason);

    ISink*   mSink         = nullptr;
    bool     mRunning      = false;
    Stop     mStop         = Stop::NONE;
    uint32_t mStartMs      = 0;
    uint32_t mBytesWritten = 0;
    uint16_t mCount        = 0;
};

#endif // IMU_MARKER_LOG_HPP
