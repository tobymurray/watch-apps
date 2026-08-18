/**
 * Synthetic nights with answers known by construction.
 *
 * This is the primary evidence in §6 of the implementation brief: there is no
 * polysomnography here and never will be, so the arithmetic is pinned against
 * nights built to contain a known onset, known awakenings of known length, and
 * a known final wake. A fixture proves the code computes what it claims to
 * compute. It proves nothing about sleep, and no test in this suite is allowed
 * to read as though it does.
 */

#ifndef SLEEPLAB_TEST_NIGHTFIXTURE_HPP
#define SLEEPLAB_TEST_NIGHTFIXTURE_HPP

#include <cstdint>
#include <vector>

#include "Engine/SleepWakeScorer.hpp"

namespace Fixture {

/// Counts chosen against EpochCounter's scale and SleepWakeScorer::kCountScale
/// so that a "quiet" epoch lands well inside sleep and an "active" one well
/// inside wake, with the boundary nowhere near either. Tests that need to
/// probe the boundary set counts explicitly instead.
constexpr uint32_t kQuiet  = 20;    ///< A settled sleeper.
constexpr uint32_t kActive = 2500;  ///< Unambiguously awake and moving.

/// Enough samples and worn fraction that nothing is Unscorable by accident.
constexpr uint16_t kGoodSamples = 600;
constexpr uint8_t  kWorn        = 100;

/// One epoch at a given activity level.
inline Engine::ScoringInput epoch(uint32_t count,
                                  int16_t hrX10 = 550,
                                  uint16_t samples = kGoodSamples,
                                  uint8_t wornPct = kWorn)
{
    Engine::ScoringInput e;
    e.count     = count;
    e.samples   = samples;
    e.wornPct   = wornPct;
    e.hrMeanX10 = hrX10;
    return e;
}

/// Append @p n epochs at @p count.
inline void run(std::vector<Engine::ScoringInput> &v, size_t n, uint32_t count,
                int16_t hrX10 = 550)
{
    for (size_t i = 0; i < n; ++i) {
        v.push_back(epoch(count, hrX10));
    }
}

/**
 * @brief A night with a known shape.
 *
 * Awake for @p awakeBefore minutes, then asleep, with an awakening of
 * @p wakeLen minutes starting @p wakeAt minutes after sleep begins, then
 * asleep again, then awake for @p awakeAfter minutes.
 *
 * Returns the epoch index where sleep genuinely begins, which is what the
 * onset tests assert against -- minus the ten-minute run the onset definition
 * requires before it will call it.
 */
struct Night {
    std::vector<Engine::ScoringInput> epochs;
    size_t trueSleepStart = 0;   ///< First genuinely-asleep epoch.
    size_t trueWakeStart  = 0;   ///< First epoch of the mid-night awakening.
    size_t trueWakeLen    = 0;
    size_t trueFinalSleep = 0;   ///< Last genuinely-asleep epoch.
};

inline Night makeNight(size_t awakeBefore, size_t sleepA, size_t wakeLen,
                       size_t sleepB, size_t awakeAfter)
{
    Night n;
    run(n.epochs, awakeBefore, kActive);
    n.trueSleepStart = n.epochs.size();
    run(n.epochs, sleepA, kQuiet);
    n.trueWakeStart = n.epochs.size();
    n.trueWakeLen   = wakeLen;
    run(n.epochs, wakeLen, kActive);
    run(n.epochs, sleepB, kQuiet);
    n.trueFinalSleep = n.epochs.size() - 1;
    run(n.epochs, awakeAfter, kActive);
    return n;
}

} // namespace Fixture

#endif // SLEEPLAB_TEST_NIGHTFIXTURE_HPP
