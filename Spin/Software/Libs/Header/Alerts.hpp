/**
 ******************************************************************************
 * @file    Alerts.hpp
 * @brief   Every pattern the wrist can feel in one ride, in one table.
 ******************************************************************************
 *
 * The rider mostly cannot see the screen, so these are the whole interface for
 * a ride with a session in it. **Every pattern here must be tellable from every
 * other one under effort**, which is a property of the set rather than of any
 * entry, so they live together and `kAllDistinct` below is that claim as a
 * compile-time assertion.
 *
 * Two things the SDK's message structs settle, and they shape everything here:
 *
 * 1. `RequestBuzzerPlay::Note` is `{ time_ms, volume }` -- **there is no
 *    frequency**. A rising two-tone for "harder" and a falling one for "easier"
 *    is not available, so the buzzer can only vary length and rhythm.
 * 2. `RequestVibroPlay` carries about twenty named haptic effects. Those are
 *    *textures*, not counts, and they are the real expressive range on this
 *    watch. So the vibro is the primary channel and the buzzer agrees with it.
 *
 * The rule the set is built on is **differ in kind, not in count**: two pips
 * against three pips is the wrong axis, because a rider at 150 bpm does not
 * count reliably and a missed first pip turns one pattern into the other. Each
 * entry is still itself if you catch only its back half.
 *
 * Nothing here is measured. Nobody has felt these on a wrist at 150 bpm, and
 * the only test that exists is riding it -- see Spin/README.md.
 *
 * No SDK types, so this builds against nothing and Alerts_test.cpp can cover
 * it without a kernel. Service.cpp owns the one map from Texture to the SDK's
 * own Effect enum.
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
    Alert,          ///< a smooth 750 ms ramp; what a lap has always been
    StrongBuzz,     ///< a rough sustained buzz
    SoftBump,       ///< one light tap
    PulsingStrong,  ///< a pulse train, unlike anything else here
};

/// Beeps one alert may have. The buzzer's own cap is five (`skMaxNotes` 10,
/// two notes a beep); three is what any pattern here needs.
constexpr size_t kMaxBeeps = 3;

/// One alert, on both channels.
struct Alert {
    Texture  texture;
    uint8_t  vibroCount;             ///< repeats of `texture`; the vibro caps at 4
    uint16_t vibroGapMs;
    uint16_t beepMs[kMaxBeeps];      ///< ms per beep; 0 ends the list
    uint16_t beepGapMs;
};

/// A lap the wearer asked for, and a step boundary with nothing to say about
/// effort. Unchanged from before there were sessions: a rider knows it.
constexpr Alert kLap = { Texture::Alert, 1, 100, { 120, 120, 0 }, 100 };

/// The ride reached `targetMinutes`. Unchanged.
constexpr Alert kTarget = { Texture::Alert, 2, 100, { 200, 200, 200 }, 100 };

/// The step just entered asks for more than the one that ended: one long,
/// strong, sustained *go*. The signal's magnitude tracks the effort asked for,
/// which is the only mapping available with no pitch to work with.
constexpr Alert kStepHarder = { Texture::StrongBuzz, 1, 0, { 500, 0, 0 }, 0 };

/// The step just entered asks for less: two light taps, obviously smaller. The
/// beeps are 60 ms at 40 ms apart rather than the lap's 120 at 100, so the two
/// differ in rhythm and not only in length -- they are the nearest pair in this
/// table on the buzzer, and they are far apart on the vibro.
constexpr Alert kStepEasier = { Texture::SoftBump, 2, 120, { 60, 60, 0 }, 40 };

/// The last step ended and the ride carries on as an ordinary ride: long, then
/// two short, over a texture nothing else here uses.
constexpr Alert kSessionEnd = { Texture::PulsingStrong, 2, 200, { 400, 100, 100 }, 100 };

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

/// Whether two alerts are told apart by the vibro alone, which is the channel
/// a rider with hands on the bars in a gym actually reads.
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
