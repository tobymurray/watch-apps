/**
 ******************************************************************************
 * @file    Epoch.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   One epoch of a night: the unit everything downstream works in.
 ******************************************************************************
 *
 * Pure C++17. No SDK header, no allocation, no I/O -- this type crosses from
 * the recorder to the engine to the CSV writer to the host tests, and the
 * whole engine is exercisable from a fixture because nothing in here knows
 * what a kernel is.
 *
 ******************************************************************************
 */

#ifndef ENGINE_EPOCH_HPP
#define ENGINE_EPOCH_HPP

#include <cstdint>

namespace Engine
{

/**
 * @brief Recording epoch length, milliseconds.
 *
 * 30 s. Chosen for the *record*, not for the scorer: a finer grid loses
 * nothing, costs a night ~110 KB, and lets a future scorer with a different
 * epoch length be run against nights already recorded. Published actigraphy
 * scorers are defined on 30 s or 60 s epochs and this is one of them.
 *
 * The scorer does NOT run at this length -- see kScoringEpochMs.
 */
constexpr uint32_t kEpochMs = 30u * 1000u;

/**
 * @brief Scoring epoch length, milliseconds.
 *
 * 60 s, because that is the epoch length Cole-Kripke's coefficients were
 * derived for and coefficients do not transfer across epoch lengths. Two
 * recording epochs are summed into one scoring epoch (see EpochCounter's
 * note on why summing is the right combination for an integrated count).
 *
 * Recording at 30 s and scoring at 60 s is deliberate rather than a
 * compromise: the file keeps the finer grid, and the algorithm runs at the
 * resolution it was validated at instead of being quietly reused at half of
 * it.
 */
constexpr uint32_t kScoringEpochMs = 60u * 1000u;

static_assert(kScoringEpochMs % kEpochMs == 0,
              "a scoring epoch must be a whole number of recording epochs");

/// Recording epochs per scoring epoch.
constexpr uint32_t kEpochsPerScoringEpoch = kScoringEpochMs / kEpochMs;

/// A field at this value was never measured, as distinct from measured as
/// zero. The distinction runs through the whole app: a heart rate of zero is a
/// finding, an unsubscribed sensor is not, and folding the second into the
/// first is how an average lies.
constexpr int32_t kAbsent = -1;

/**
 * @brief Where an epoch's heart rate came from.
 *
 * Recorded rather than assumed. `HEART_RATE_EX` reports which source the
 * kernel arbitrated to, and an epoch whose HR came from a chest strap is a
 * different measurement from one whose HR came from the wrist -- the strap is
 * electrical and the wrist is optical, and only the wrist degrades with a
 * loose band.
 */
enum class HrSource : uint8_t {
    None     = 0, ///< No valid HR in this epoch.
    Optical  = 1, ///< Wrist PPG.
    External = 2, ///< BLE strap.
    Mixed    = 3, ///< Both, within one epoch. Rare; recorded rather than hidden.
};

/**
 * @brief One recorded epoch.
 *
 * Deliberately flat, fixed-size and POD. It is written to CSV a row at a time,
 * read back by the engine, and held in a fixed array for a night -- so it must
 * not own anything.
 *
 * 30 s epochs, 960 of them in an eight-hour night.
 *
 * This struct packs into ~100 bytes, but the number that matters is the *CSV
 * row*, because that is what lands on the volume -- and it was previously quoted
 * here as ~46 KB a night, which was this struct's old size rather than a
 * measurement of the file. Measured on a generated eight-hour night at schema 2:
 * **117 bytes a row, ~110 KB for the epoch log, ~121 KB for everything the night
 * writes** including the summary, the index row, the idle record and the
 * diagnostic log. So ~44 MB per decade, on a volume that has carried 160 MiB of
 * map packs.
 *
 * Raw accelerometer at the ~48 Hz actually delivered would be ~60 MB for eight
 * hours, which is why epochs are always recorded and raw never is by default.
 */
struct Epoch
{
    // -- Clocks ---------------------------------------------------------------
    //
    // Both, always. Uptime is monotonic and survives an app restart but knows
    // nothing about "23:00"; the wall clock knows what 23:00 means but jumps on
    // a timezone change, a host sync or DST. Every duration is derived from
    // uptime and every time-of-day from the wall clock, and the pair is what
    // lets a night resumed after a restart be stitched honestly.

    uint32_t uptimeMs = 0;   ///< Uptime at the epoch's *end*.
    int64_t  wallUtc  = 0;   ///< time(nullptr) at the epoch's end; -1 unknown.

    /// Real uptime span this epoch covers. Not assumed to be kEpochMs: a loop
    /// that wakes late overshoots, and a rate computed against a nominal epoch
    /// would silently absorb the difference.
    uint32_t spanMs   = 0;

    // -- Activity -------------------------------------------------------------

    /// Integrated band-limited acceleration over the epoch, in the count units
    /// EpochCounter defines. The measurement everything else rests on.
    uint32_t count    = 0;

    /// Largest single-sample contribution in the epoch, same units per second.
    /// Separates "moved once, hard" from "fidgeted throughout", which sum to
    /// the same count and are not the same thing.
    uint32_t peak     = 0;

    /// The three per-axis integrals `count` is the vector magnitude of, in the
    /// same units. Diagnostic: broadband sensor noise arrives on all three axes
    /// at similar amplitude, a moving wrist does not, and the axis holding
    /// gravity gives the resting orientation. See EpochCounter::closeEpoch.
    uint32_t countX   = 0;
    uint32_t countY   = 0;
    uint32_t countZ   = 0;

    /// Accelerometer samples the epoch was actually built from. An epoch built
    /// from four samples is not evidence about anything, and the scorer needs
    /// to be able to say so rather than average it in.
    uint16_t samples  = 0;

    // -- Kernel-side corroboration --------------------------------------------

    uint16_t motionEvents = 0; ///< MOTION events in the epoch.
    uint16_t sigMotion    = 0; ///< SIG_MOTION events. Rare and meaningful.
    int32_t  stepDelta    = kAbsent; ///< Steps taken. Strong out-of-bed signal.

    // -- Cardio ---------------------------------------------------------------

    int16_t  hrMeanX10 = static_cast<int16_t>(kAbsent); ///< Mean bpm x10.
    int16_t  hrMinX10  = static_cast<int16_t>(kAbsent); ///< Minimum bpm x10.
    uint16_t hrSamples = 0;   ///< Valid HR samples the means are built from.
    HrSource hrSource  = HrSource::None;

    // -- Worn -----------------------------------------------------------------

    /// Fraction of the epoch reported worn, 0..100. The gate in WornGate is
    /// built on this plus a plausibility check, never on this alone.
    uint8_t  wornPct   = 0;
    /// Worn/not-worn transitions within the epoch. A flickering sensor and a
    /// genuine removal produce the same wornPct and must not be confused.
    uint8_t  wornEdges = 0;

    // -- Power ----------------------------------------------------------------

    int16_t  battPctX10 = static_cast<int16_t>(kAbsent);
    /// Any charging at all during the epoch. One epoch of this marks the whole
    /// night interrupted: plugging in terminates the app, so a night that saw
    /// the charger is a night with a hole in it.
    bool     charging   = false;

    // -- Delivery and power ---------------------------------------------------
    //
    // What the epoch was built from, rather than what it measured. Absent
    // (kAbsent) or zero when the diagnostics setting is off.
    //
    // These are here because the Tier 0 probe recorded them and SleepLab did not,
    // which made every question about delivery or power cost a separate night with
    // a different app installed. A count is only comparable with another count
    // taken at a similar delivered rate, and the rate is neither the requested one
    // (ledger rows S3, S17) nor constant -- so a night that cannot say what it was
    // built from cannot be compared with another night at all.

    uint16_t accBatches   = 0;  ///< Sensor-layer messages carrying accelerometer.
    /// Worst gap between consecutive accelerometer timestamps, ms. Distinguishes
    /// "delivery thinned" from "delivery stopped and restarted", which the sample
    /// count alone cannot.
    uint16_t accMaxGapMs  = 0;

    /// TOUCH_DETECT samples *delivered*, as distinct from what they said.
    ///
    /// The one column that separates "the sensor reported worn" from "the sensor
    /// said nothing and the recorder carried the last state forward". It is an
    /// event sensor: measured across a whole night, it delivered one sample in 507
    /// minutes (ledger row S7). `wornPct` cannot show that and `wornEdges` cannot
    /// either.
    uint16_t touchSamples = 0;

    /// Kernel's own confidence in the heart rate, x10. Optical HR against a hard
    /// surface returns a number with the trust collapsed, and the worn gate's
    /// second half is "was there a pulse" -- so a reading's trust is what separates
    /// a struggling sensor from a genuinely low heart rate.
    int16_t  hrTrustX10   = static_cast<int16_t>(kAbsent);

    /// HEART_RATE_EX readings by arbitrated source. `hrSource` collapses these to
    /// one enum for the night; the counts are what show contention.
    uint16_t hrexOptical  = 0;
    uint16_t hrexExternal = 0;
    uint16_t hrexUnknown  = 0;

    /// Battery voltage, current and remaining capacity from BATTERY_METRICS.
    ///
    /// `battPctX10` above is not a substitute: measured across a whole 8.45 h
    /// night it read 100.0 % at both ends while the remaining capacity fell by
    /// 10 mAh (ledger row S18). The percent gauge is not slow, it is
    /// non-functional for this, and these are the columns that work.
    int32_t  battMv       = kAbsent;
    int32_t  battMaX10    = kAbsent;
    int32_t  battAvgMaX10 = kAbsent;
    int32_t  battMah      = kAbsent;

    /// Loop wakes and messages handled during the epoch. A wake without a message
    /// is a spurious wake; the two together are what tell a fed loop from a
    /// spinning one.
    uint16_t wakes        = 0;
    uint16_t msgs         = 0;

    // -- Reserved: heart-rate variability -------------------------------------
    //
    // Written as absent, always, today. `HEART_BEAT` (0x40) emits no events at
    // all -- HR detection is a frequency-domain algorithm rather than per-beat
    // detection -- so there are no RR intervals to build HRV from, and
    // RR_INTERVAL (SDK PR #220) has no firmware producer, so even a chest strap
    // worn overnight yields none.
    //
    // They exist anyway, and are named and documented, because UNA's answer had
    // a "not today" shape: a higher-rate PPG mode and on-chip HRV are both
    // being explored, and overnight rest is precisely the case optical HRV is
    // good for ("Overnight or resting morning HRV is perfect for training
    // readiness/recovery"). When a producer appears, filling these in is a
    // recorder change and a scorer input -- not a file format change, not a
    // schema bump, and not a reason to re-record the nights already on disk.
    //
    // The scorer already takes an optional HRV channel and currently never
    // receives one. See SleepWakeScorer::score().

    int32_t  rmssdX10 = kAbsent;  ///< RMSSD in ms x10.
    int32_t  sdnnX10  = kAbsent;  ///< SDNN in ms x10.
    uint16_t rrCount  = 0;        ///< RR intervals the above were built from.
};

} // namespace Engine

#endif // ENGINE_EPOCH_HPP
