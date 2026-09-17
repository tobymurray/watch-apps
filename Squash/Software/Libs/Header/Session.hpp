/**
 ******************************************************************************
 * @file    Session.hpp
 * @brief   What a recording session is doing, and what the screen is told.
 ******************************************************************************
 */

#ifndef SESSION_HPP
#define SESSION_HPP

#include <cstdint>

namespace Session
{

/// Where the session is. The Service has no "finished" state -- it returns to
/// INACTIVE, which is also what "never started" looks like -- so the GUI keeps
/// the bit that tells them apart.
enum class State : uint8_t {
    INACTIVE = 0,
    ACTIVE,
    PAUSED
};

/// What the wearer says they are doing.
///
/// These values ARE the `kind` column of `imu_<stamp>_events.csv`, so they are
/// a wire format, not a display detail: a recording is read back by mapping
/// this number onto `effortkit::fixture::Label`. NONE stayed 0 because that is
/// what `ImuMarkerLog::Kind::MANUAL` has always written, so every recording
/// made before on-watch labelling reads back as unlabelled markers rather than
/// as rallies.
enum class Label : uint8_t {
    NONE = 0,
    RALLY,
    REST,
    OFF_COURT,
    WARMUP,
    DRILL,
    IDLE,
    /// A whole game, rallies and the gaps between them together.
    ///
    /// What the user-facing app marks, because a press per rally is 15-25
    /// presses a game and nobody does that. Deliberately not RALLY: a game is a
    /// mixture whose rest fraction is unknown, and labelling one as the other
    /// would calibrate a rally-level segmenter against a mixture.
    GAME,
    COUNT
};

/// Everything the screen draws that the Service owns, sent once a second.
///
/// Kept flat and integral because it crosses a process boundary into a Rust
/// renderer that takes one POD struct; the widths here are the widths there.
struct Status {
    uint32_t elapsedS    = 0;  ///< active session seconds
    uint32_t recS        = 0;  ///< seconds of IMU written; diverges once a cap trips
    uint32_t recCapS     = 0;  ///< duration cap in force
    uint32_t recKb       = 0;  ///< KiB written
    uint32_t recCapKb    = 0;  ///< size cap in force
    uint32_t gyroMag     = 0;  ///< last epoch's mean gyro magnitude, raw LSB
    uint32_t accelVarK   = 0;  ///< last epoch's accel magnitude variance, LSB^2/1000
    /// Seconds the wearer spent in each of the three states that describe a
    /// squash session. These are what the wearer pressed, not what anything
    /// inferred -- no segmenter runs on this watch -- so they are ground truth
    /// rather than a measurement, and worth more than one for exactly that
    /// reason until a calibration exists to check against them.
    uint32_t rallyS      = 0;
    uint32_t gameS       = 0;
    uint32_t restS       = 0;
    uint32_t offCourtS   = 0;
    uint16_t markers     = 0;  ///< markers written, label changes included
    uint16_t hrBpm       = 0;  ///< bpm; 0 = nothing believable right now
    uint16_t labelS      = 0;  ///< seconds held in the current label
    uint8_t  satAccelPct = 0;  ///< % of last epoch's samples with an accel axis railed
    uint8_t  satGyroPct  = 0;  ///< % of last epoch's samples with a gyro axis railed
    uint8_t  hrTrust     = 0;  ///< the kernel's 0..3 confidence in hrBpm
    uint8_t  hrSource    = 0;  ///< HeartRateEx::Source: 0 none, 1 optical, 2 external
    uint8_t  recording   = 0;  ///< 1 = samples are being written
    uint8_t  recStop     = 0;  ///< ImuCsvRecorder::Stop
    uint8_t  armed       = 0;  ///< 1 = recordImu was true, so starting will record
    uint8_t  label       = 0;  ///< Session::Label
};

} // namespace Session

#endif // SESSION_HPP
