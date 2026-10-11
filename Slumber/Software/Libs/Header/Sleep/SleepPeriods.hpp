#ifndef SLUMBER_SLEEP_PERIODS_HPP
#define SLUMBER_SLEEP_PERIODS_HPP

#include <cstddef>
#include <cstdint>

/// Finds sleep periods in a stretch of 30 s epochs, at any hour.
///
/// No bedtime window: a period is wherever sustained rest is, and a late night
/// that runs past noon is one period, not one cut at a clock time. Short
/// awakenings stay inside the night they interrupt. The shape is van Hees et
/// al. 2018's (Sci Rep 8:12975): a 5-minute rolling median, rest blocks of at
/// least 30 minutes, and blocks less than 60 minutes apart merged into one.
///
/// Pure C++17: no SDK, no allocation, no clock. `Slumber/README.md` records the
/// nights these constants were measured against.
namespace SleepPeriods
{

constexpr uint32_t kEpochS = 30;

/// Half-width of the rolling median, in epochs: 11 epochs is 5.5 minutes.
constexpr size_t kMedianHalfWidth = 5;

/// SleepLab's movement floor. Measured over 33 recorded sessions: worn epochs
/// sit below it 88% of the time (p75 460, p90 1222), and walking runs to
/// tens of thousands.
constexpr uint32_t kRestCountCeiling = 1100;

/// Steps in one epoch that make it awake whatever the count says. Inside 21
/// recorded worn sessions, 13 703 of 13 714 epochs had none; walking to close a
/// session showed 20-26.
constexpr uint16_t kStepsAwake = 10;

constexpr uint32_t kMinBlockS = 30u * 60u;
constexpr uint32_t kMergeGapS = 60u * 60u;

/// Neighbours further apart than this are separated by missing data.
constexpr uint32_t kMaxStepS = 3u * kEpochS;

/// SleepLab's `gate_min_hr_trust_x10`. Measured: 86% of worn epochs reach it,
/// 2.4% of epochs from a watch on a nightstand.
constexpr uint8_t kWornTrustX10 = 20;
/// Above this count a low-trust reading is an arm pressed on the sensor, not a
/// table: over the SleepLab sessions, quiet low-trust epochs on a nightstand or
/// pillow exceeded it 0.28% of the time (p99 224) and those in worn sessions 92%
/// (median 455); the floor is the BMI270's own noise, 183-187 on any surface.
constexpr uint32_t kSensorFloorCount = 250;
/// Per recorded session, worn nights scored 77-98% and nightstand nights 0-3%.
constexpr uint8_t kWornMinPct    = 70;
constexpr uint8_t kNotWornMaxPct = 30;
/// Below this share of epochs with heart rate asked for, the wrist question has
/// no answer: half the evidence is missing.
constexpr uint8_t kMinHrCoveragePct = 50;

struct Epoch {
    uint32_t utc        = 0;  ///< s, start of the epoch
    uint32_t count      = 0;  ///< activity count, SleepLab's EpochCounter scale
    uint16_t steps      = 0;
    uint8_t  hrTrustX10 = 0;  ///< meaningful only when hasHr
    bool     hasHr      = false;
    /// HEART_RATE was subscribed, so an epoch without a reading is evidence
    /// against the wrist rather than no evidence: SleepLab's `WornGate` rule.
    /// Measured: a watch on a table gave no reading in any of 354 epochs, and
    /// one on a pillow read in 98% with trust never reaching kWornTrustX10.
    bool     hrAsked    = false;
};

enum class Worn : uint8_t { Worn, NotWorn, Uncertain };

struct Period {
    uint32_t startUtc     = 0;
    uint32_t endUtc       = 0;  ///< s, end of the last rest epoch
    uint16_t restMin      = 0;  ///< minutes of rest epochs inside it
    uint16_t missingMin   = 0;  ///< minutes inside it with no epoch at all
    uint8_t  wornPct      = 0;  ///< of epochs whose heart rate is evidence
    uint8_t  hrCoverPct   = 0;  ///< of epochs present, those whose heart rate is evidence
    Worn     worn         = Worn::Uncertain;
    /// A period is final once kMergeGapS has passed after it, since nothing
    /// later could still be merged into it.
    bool     settled      = false;
};

namespace detail
{

inline uint32_t activity(const Epoch &e)
{
    return e.steps >= kStepsAwake ? UINT32_MAX : e.count;
}

inline bool lowTrustAboveFloor(const Epoch &e)
{
    return e.hasHr && e.hrTrustX10 < kWornTrustX10 && e.count > kSensorFloorCount;
}

/// A low-trust reading above the sensor floor says nothing about the wrist.
inline bool hrEvidence(const Epoch &e)
{
    return (e.hrAsked || e.hasHr) && !lowTrustAboveFloor(e);
}

inline bool hrSaysWorn(const Epoch &e)
{
    return e.hasHr && e.hrTrustX10 >= kWornTrustX10;
}

/// Median of the epochs within kMedianHalfWidth of `i` in time, not merely in
/// index, so a gap in the data does not borrow from the far side of it.
inline bool isRest(const Epoch *e, size_t n, size_t i)
{
    uint32_t window[2 * kMedianHalfWidth + 1];
    size_t   w     = 0;
    const uint32_t reach = static_cast<uint32_t>(kMedianHalfWidth) * kEpochS;
    const size_t lo = i >= kMedianHalfWidth ? i - kMedianHalfWidth : 0;
    const size_t hi = (i + kMedianHalfWidth < n) ? i + kMedianHalfWidth : n - 1;
    for (size_t j = lo; j <= hi; ++j) {
        const uint32_t d = e[j].utc > e[i].utc ? e[j].utc - e[i].utc : e[i].utc - e[j].utc;
        if (d <= reach) {
            window[w++] = activity(e[j]);
        }
    }
    for (size_t a = 1; a < w; ++a) {
        const uint32_t v = window[a];
        size_t b = a;
        for (; b > 0 && window[b - 1] > v; --b) {
            window[b] = window[b - 1];
        }
        window[b] = v;
    }
    return window[w / 2] < kRestCountCeiling;
}

enum class Wrist : uint8_t { On, Off, Unknown };

/// Two thirds of the heart-rate evidence within the median's reach, so neither
/// one low-trust reading nor trust hovering at the threshold moves the wrist.
inline Wrist wrist(const Epoch *e, size_t n, size_t i)
{
    const uint32_t reach = static_cast<uint32_t>(kMedianHalfWidth) * kEpochS;
    const size_t lo = i >= kMedianHalfWidth ? i - kMedianHalfWidth : 0;
    const size_t hi = (i + kMedianHalfWidth < n) ? i + kMedianHalfWidth : n - 1;
    uint32_t on = 0, off = 0;
    for (size_t j = lo; j <= hi; ++j) {
        const uint32_t d = e[j].utc > e[i].utc ? e[j].utc - e[i].utc : e[i].utc - e[j].utc;
        if (d <= reach && hrEvidence(e[j])) {
            (hrSaysWorn(e[j]) ? on : off) += 1;
        }
    }
    if (on >= 2 * off && on > 0) {
        return Wrist::On;
    }
    if (off >= 2 * on && off > 0) {
        return Wrist::Off;
    }
    return Wrist::Unknown;
}

inline void score(const Epoch *e, size_t n, Period &p)
{
    uint32_t present = 0, asked = 0, worn = 0, rest = 0;
    uint32_t prevUtc = 0;
    uint32_t missingS = 0;
    for (size_t i = 0; i < n; ++i) {
        if (e[i].utc < p.startUtc || e[i].utc >= p.endUtc) {
            continue;
        }
        if (present > 0 && e[i].utc - prevUtc > kMaxStepS) {
            missingS += e[i].utc - prevUtc - kEpochS;
        }
        prevUtc = e[i].utc;
        ++present;
        if (isRest(e, n, i)) {
            ++rest;
        }
        if (hrEvidence(e[i])) {
            ++asked;
            if (hrSaysWorn(e[i])) {
                ++worn;
            }
        }
    }
    p.restMin    = static_cast<uint16_t>(rest * kEpochS / 60u);
    p.missingMin = static_cast<uint16_t>(missingS / 60u);
    p.hrCoverPct = static_cast<uint8_t>(present ? asked * 100u / present : 0u);
    p.wornPct    = static_cast<uint8_t>(asked ? worn * 100u / asked : 0u);
    if (p.hrCoverPct < kMinHrCoveragePct) {
        p.worn = Worn::Uncertain;
    } else if (p.wornPct >= kWornMinPct) {
        p.worn = Worn::Worn;
    } else if (p.wornPct <= kNotWornMaxPct) {
        p.worn = Worn::NotWorn;
    } else {
        p.worn = Worn::Uncertain;
    }
}

} // namespace detail

/// Finds the periods in `e`, which must be sorted by `utc`. Writes at most
/// `maxOut` and returns how many were written; `nowUtc` decides `settled`.
inline size_t find(const Epoch *e, size_t n, Period *out, size_t maxOut, uint32_t nowUtc)
{
    if (e == nullptr || out == nullptr || maxOut == 0) {
        return 0;
    }
    using detail::Wrist;
    size_t   written  = 0;
    bool     inRun    = false;
    uint32_t runStart = 0;
    uint32_t runEnd   = 0;
    Wrist    runWrist = Wrist::Unknown;
    // Which side of the wrist each written period was built on, so a night is
    // never merged with the table the watch was left on after it.
    Wrist    lastWrist = Wrist::Unknown;

    auto closeRun = [&]() {
        inRun = false;
        if (runEnd - runStart < kMinBlockS) {
            return;
        }
        if (written > 0 && runWrist == lastWrist &&
            runStart - out[written - 1].endUtc < kMergeGapS) {
            out[written - 1].endUtc = runEnd;
            return;
        }
        if (written < maxOut) {
            out[written].startUtc = runStart;
            out[written].endUtc   = runEnd;
            lastWrist             = runWrist;
            ++written;
        }
    };

    for (size_t i = 0; i < n; ++i) {
        const bool gapBefore = i > 0 && e[i].utc - e[i - 1].utc > kMaxStepS;
        if (inRun && gapBefore) {
            closeRun();
        }
        if (!detail::isRest(e, n, i)) {
            if (inRun) {
                closeRun();
            }
            continue;
        }
        const Wrist w = detail::wrist(e, n, i);
        if (inRun && w != Wrist::Unknown && runWrist != Wrist::Unknown && w != runWrist) {
            closeRun();
        }
        if (!inRun) {
            inRun    = true;
            runStart = e[i].utc;
            runWrist = w;
        } else if (runWrist == Wrist::Unknown) {
            runWrist = w;
        }
        runEnd = e[i].utc + kEpochS;
    }
    if (inRun) {
        closeRun();
    }

    for (size_t k = 0; k < written; ++k) {
        detail::score(e, n, out[k]);
        out[k].settled = nowUtc >= out[k].endUtc && nowUtc - out[k].endUtc >= kMergeGapS;
    }
    return written;
}

} // namespace SleepPeriods

#endif // SLUMBER_SLEEP_PERIODS_HPP
