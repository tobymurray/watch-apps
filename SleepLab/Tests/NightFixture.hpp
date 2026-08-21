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

#include "Engine/NightSegmenter.hpp"
#include "Engine/NightSummary.hpp"
#include "Engine/SleepWakeScorer.hpp"
#include "Engine/WornGate.hpp"

namespace Fixture {

/// Counts chosen against EpochCounter's scale and SleepWakeScorer::kCountScale
/// so that a "quiet" epoch lands well inside sleep and an "active" one well
/// inside wake, with the boundary nowhere near either. Tests that need to
/// probe the boundary set counts explicitly instead.
///
/// These were 20 and 2500, chosen against constants that were guesses. When
/// those constants moved to measured values on 2026-08-20 the two literals
/// silently landed on the wrong side of four different thresholds and eleven
/// tests failed at once, none of them naming the reason. So the values are now
/// **asserted against the constants they have to sit between**, and a constant
/// that moves far enough to invalidate them is a build failure with a message
/// rather than a morning of debugging.
///
/// The numbers themselves are the real ones: 700 is the median 60 s scoring
/// epoch of `Recordings/2026-08-19-worn` (695) and 40 000 is the median of the
/// hour that night's wearer spent getting up (39 851).
constexpr uint32_t kQuiet  = 700;    ///< A settled sleeper.
constexpr uint32_t kActive = 40000;  ///< Unambiguously awake and moving.

/// The Cole-Kripke boundary, in counts per scoring epoch held across the whole
/// window: the count at which D = 1. Everything below scores sleep.
constexpr float kScorerBoundary =
    1.0f / (Engine::SleepWakeScorer::kP *
            (Engine::SleepWakeScorer::kWeights[0] + Engine::SleepWakeScorer::kWeights[1] +
             Engine::SleepWakeScorer::kWeights[2] + Engine::SleepWakeScorer::kWeights[3] +
             Engine::SleepWakeScorer::kWeights[4] + Engine::SleepWakeScorer::kWeights[5] +
             Engine::SleepWakeScorer::kWeights[6]) *
            Engine::SleepWakeScorer::kCountScale);

/// Kernel trust for a heart rate a fixture means to be believed.
///
/// 30 is the median of the one real worn night there is, and the gate requires
/// 20. Fixtures default to a *trusted* pulse because "this epoch had a heart
/// rate" is what they nearly all mean; a fixture that means the other thing --
/// a plausible number the kernel does not stand behind, which is what a watch on
/// a pillow produces in every epoch -- sets it explicitly.
constexpr int16_t kTrustedHr = 30;
/// What six hours face down on a pillow measured.
constexpr int16_t kUntrustedHr = 8;

static_assert(kQuiet < kScorerBoundary / 2.0f,
              "kQuiet must score as sleep with room to spare");
static_assert(kActive > kScorerBoundary * 2.0f,
              "kActive must score as wake with room to spare");
static_assert(kQuiet > Engine::WornGate::kMicroMovementFloor,
              "kQuiet must look alive to the worn gate, or every fixture night "
              "is a nightstand");
static_assert(kQuiet <= Engine::SegmenterConfig{}.stillnessCountMax,
              "a run of kQuiet epochs must be still enough to open a night");
static_assert(kActive >= Engine::SegmenterConfig{}.activityCountMin,
              "a run of kActive epochs must be active enough to close one");
static_assert(kTrustedHr >= Engine::WornGate::kMinHrTrustX10,
              "a fixture's default pulse must be one the gate believes");
static_assert(kUntrustedHr < Engine::WornGate::kMinHrTrustX10,
              "the pillow's measured trust must fall below the gate's bar, or "
              "the fixture no longer models the case the bar exists for");

static_assert(kActive > Engine::NightAnalyser::kMovementFloor &&
                  kQuiet < Engine::NightAnalyser::kMovementFloor,
              "the movement index has to be able to tell the two apart");

/// Enough samples and worn fraction that nothing is Unscorable by accident.
constexpr uint16_t kGoodSamples = 600;
constexpr uint8_t  kWorn        = 100;

/// One epoch at a given activity level.
inline Engine::ScoringInput epoch(uint32_t count,
                                  int16_t hrX10 = 550,
                                  uint16_t samples = kGoodSamples,
                                  uint8_t wornPct = kWorn,
                                  int16_t hrTrustX10 = kTrustedHr)
{
    Engine::ScoringInput e;
    e.count      = count;
    e.samples    = samples;
    e.wornPct    = wornPct;
    e.hrMeanX10  = hrX10;
    e.hrTrustX10 = hrTrustX10;
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
