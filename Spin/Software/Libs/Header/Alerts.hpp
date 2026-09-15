/**
 ******************************************************************************
 * @file    Alerts.hpp
 * @brief   Every pattern the wrist can feel in one ride, in one table.
 ******************************************************************************
 *
 * Being tellable apart under effort is a property of the SET rather than of any
 * entry, which is why they are one table and why `allPairwiseDistinct()` below
 * is a compile-time assertion.
 *
 * HARDWARE, and it shapes everything here: `RequestBuzzerPlay::Note` is
 * `{ time_ms, volume }` -- there is NO FREQUENCY, so a rising two-tone for
 * "harder" and a falling one for "easier" cannot be built and the buzzer can
 * only vary length and rhythm. `RequestVibroPlay` instead carries about twenty
 * named haptic effects, which are textures rather than counts, so the vibro
 * leads and the buzzer agrees with it. Falsified by either struct gaining a
 * pitch field.
 *
 * The rule the set is built on is **differ in kind, not in count**: a rider at
 * 150 bpm does not count reliably, and a missed first pip turns one pattern
 * into the other.
 *
 * NOTHING HERE IS MEASURED -- nobody has felt these on a wrist at 150 bpm.
 * Riding a session is the only thing that can falsify any of it.
 *
 * No SDK types, so this builds against nothing.
 *
 ******************************************************************************
 */

#ifndef ALERTS_HPP
#define ALERTS_HPP

#include <cstddef>
#include <cstdint>

namespace Alerts {

/// The vibro waveform an alert uses, named by what it feels like.
enum class Texture : uint8_t {
    Alert,          ///< a smooth 750 ms ramp
    StrongBuzz,     ///< a rough sustained buzz
    SoftBump,       ///< one light tap
    PulsingStrong,  ///< a pulse train, unlike anything else here
};

/// Beeps one alert may have; the buzzer's own cap is five (`skMaxNotes` 10,
/// two notes a beep).
constexpr size_t kMaxBeeps = 3;

/// One alert, on both channels.
struct Alert {
    Texture     texture;
    uint8_t     vibroCount;          ///< repeats of `texture`; the vibro caps at 4
    uint16_t    vibroGapMs;
    uint16_t    beepMs[kMaxBeeps];   ///< ms per beep; 0 ends the list
    uint16_t    beepGapMs;
    const char *name;                ///< what `recovery.log` calls this firing
};

/// A lap the wearer asked for, and a step boundary with no effort either side.
constexpr Alert kLap = { Texture::Alert, 1, 100, { 120, 120, 0 }, 100, "lap" };

/// The ride reached its configured target time.
constexpr Alert kTarget = { Texture::Alert, 2, 100, { 200, 200, 200 }, 100, "target" };

/// The step just entered asks for more than the one that ended: one long,
/// strong, sustained go, because magnitude tracking effort is the only mapping
/// left once pitch is gone.
constexpr Alert kStepHarder = { Texture::StrongBuzz, 1, 0, { 500, 0, 0 }, 0, "harder" };

/// The step just entered asks for less: two light taps, obviously smaller.
///
/// The nearest pair in this table on the buzzer is this against `kLap`, both
/// two beeps, so these are 60 ms at 40 apart against 120 at 100 -- a difference
/// in rhythm and not only in length.
constexpr Alert kStepEasier = { Texture::SoftBump, 2, 120, { 60, 60, 0 }, 40, "easier" };

/// The last step ended: long, then two short, over a texture nothing else uses.
constexpr Alert kSessionEnd = { Texture::PulsingStrong, 2, 200, { 400, 100, 100 }, 100, "session_end" };

/// Every alert that can fire in one ride.
constexpr Alert kAll[] = { kLap, kTarget, kStepHarder, kStepEasier, kSessionEnd };
constexpr size_t kAllCount = sizeof(kAll) / sizeof(kAll[0]);

/// Beeps an alert actually has.
constexpr size_t beepCount(const Alert& a)
{
    size_t n = 0;
    while (n < kMaxBeeps && a.beepMs[n] != 0) {
        ++n;
    }
    return n;
}

/// Whether two alerts are told apart by the vibro alone.
constexpr bool distinctOnVibro(const Alert& a, const Alert& b)
{
    return a.texture != b.texture || a.vibroCount != b.vibroCount;
}

/// Whether two alerts are told apart by the buzzer alone.
constexpr bool distinctOnBuzzer(const Alert& a, const Alert& b)
{
    if (beepCount(a) != beepCount(b)) {
        return true;
    }
    for (size_t i = 0; i < beepCount(a); ++i) {
        if (a.beepMs[i] != b.beepMs[i]) {
            return true;
        }
    }
    return a.beepGapMs != b.beepGapMs;
}

/// The property the whole table exists to hold.
constexpr bool allPairwiseDistinct()
{
    for (size_t i = 0; i < kAllCount; ++i) {
        for (size_t j = i + 1; j < kAllCount; ++j) {
            if (!distinctOnVibro(kAll[i], kAll[j]) || !distinctOnBuzzer(kAll[i], kAll[j])) {
                return false;
            }
        }
    }
    return true;
}

static_assert(allPairwiseDistinct(),
              "two alerts that can fire in one ride feel the same");

} // namespace Alerts

#endif // ALERTS_HPP
