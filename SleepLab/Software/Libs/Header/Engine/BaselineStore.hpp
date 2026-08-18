/**
 ******************************************************************************
 * @file    BaselineStore.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The wearer's own normal, earned from their own nights.
 ******************************************************************************
 *
 * Pure C++17. No SDK header, no allocation, no I/O. The caller loads and saves
 * it; this only decides what it means.
 *
 * ---------------------------------------------------------------------------
 * Why there are no absolute thresholds anywhere in this app
 *
 * "A resting heart rate below 60 is good." "Sleep efficiency above 85 % is
 * normal." Both are real findings from real populations, and neither is a
 * statement about the person wearing this watch. Population norms describe
 * distributions; an individual is one draw from one, and the spread is wide
 * enough that a number at the population mean can be far from a given person's
 * own normal in either direction.
 *
 * What *is* meaningful is a person against themselves. Nocturnal heart-rate
 * minimum, the time it takes to reach it, and the morning rise are real,
 * useful and highly individual. So this app reports **deltas from the wearer's
 * own recorded nights** and never an absolute judgement.
 *
 * ---------------------------------------------------------------------------
 * And why it refuses to report anything for a while
 *
 * A baseline of one night is that night. A baseline of two is a coin flip.
 * Until there are enough nights for "unusual" to mean something, the honest
 * output is not a small delta -- it is no delta, and a line saying how many
 * more nights are needed. A number shown with a caveat is a number that gets
 * remembered without the caveat.
 *
 * `kMinNights` is 5, which is the smallest number that is not one bad night.
 * It is a judgement, not a measurement, and it is tagged as one in the ledger.
 *
 ******************************************************************************
 */

#ifndef ENGINE_BASELINESTORE_HPP
#define ENGINE_BASELINESTORE_HPP

#include <cstddef>
#include <cstdint>

#include "Engine/Epoch.hpp"

namespace Engine
{

/**
 * @brief A rolling window of a wearer's own nights.
 *
 * Fixed-size and POD, so the caller can persist it by writing the struct's
 * fields out and reading them back -- there is no allocation and nothing to
 * serialise that is not a number.
 */
class BaselineStore
{
public:
    /// Nights required before any delta is reported.
    ///
    /// Below this, `hrDelta()` and friends return absent and the UI says how
    /// many nights remain. Five: the smallest number where one atypical night
    /// cannot dominate. A judgement rather than a measurement -- ledger row A4.
    static constexpr size_t kMinNights = 5;

    /// Nights kept.
    ///
    /// Twenty-eight, four weeks. Long enough to average out a bad week, short
    /// enough that a baseline still tracks a person whose sleep is genuinely
    /// changing -- an all-time average would take months to notice a new job or
    /// a new baby, which is exactly when the number matters most.
    static constexpr size_t kWindowNights = 28;

    /// One night's contribution. Only what a baseline is actually built from.
    struct Sample
    {
        int32_t hrMinX10       = kAbsent; ///< Nocturnal HR minimum.
        int32_t efficiencyPct  = kAbsent;
        int32_t totalSleepMin  = kAbsent;
        /// Epoch index of the HR minimum, as a fraction of the night in
        /// percent. Absolute epoch index would not compare across nights of
        /// different lengths.
        int32_t hrMinAtPct     = kAbsent;
    };

    /// A value against the wearer's own normal.
    struct Delta
    {
        /// True when there were enough nights to say anything at all. When
        /// false every field below is absent and the caller must show the
        /// "need N more nights" line rather than a zero.
        bool    available = false;
        int32_t value     = kAbsent; ///< Tonight's value.
        int32_t baseline  = kAbsent; ///< The window's median.
        int32_t delta     = kAbsent; ///< value - baseline.
        size_t  nights    = 0;       ///< Nights the baseline is built from.
        /// Nights still needed before `available` can become true. Zero when
        /// it already is.
        size_t  nightsNeeded = 0;
    };

    /// Add tonight. Oldest falls out of the window.
    ///
    /// A night that failed the worn gate must never reach here: the caller
    /// checks, because a nightstand's flawless efficiency would poison the
    /// baseline for four weeks and make every real night afterwards look bad.
    void add(const Sample &s);

    size_t nights() const { return mCount; }
    bool   ready()  const { return mCount >= kMinNights; }

    /// Nocturnal HR minimum against the wearer's own median.
    Delta hrMin(int32_t tonightX10) const;
    /// Sleep efficiency against the wearer's own median.
    Delta efficiency(int32_t tonightPct) const;
    /// Total sleep time against the wearer's own median.
    Delta totalSleep(int32_t tonightMin) const;

    // -- Persistence ---------------------------------------------------------
    //
    // Exposed as plain arrays so the caller can write them to a file without
    // this class knowing what a file is.

    const Sample *samples()   const { return mSamples; }
    size_t        count()     const { return mCount; }
    size_t        nextSlot()  const { return mNext; }

    /// Restore a persisted window. Values outside the array are ignored rather
    /// than rejected: a truncated or corrupt store should cost the baseline,
    /// not the night.
    void restore(const Sample *samples, size_t count, size_t nextSlot);

private:
    /// Median of the present values of one field, or absent if there are none.
    ///
    /// Median rather than mean, throughout. One night on a plane, one night
    /// with a fever, one night the watch half-fell-off -- a mean carries all
    /// three into the baseline for four weeks, and the median does not. With a
    /// window this short that is the difference between a baseline that
    /// describes the wearer and one that describes their worst week.
    static int32_t median(const int32_t *values, size_t n);

    Delta build(int32_t tonight, int32_t Sample::*field) const;

    Sample mSamples[kWindowNights] = {};
    size_t mCount = 0;  ///< Valid entries, saturating at kWindowNights.
    size_t mNext  = 0;  ///< Next slot to write. Ring buffer.
};

} // namespace Engine

#endif // ENGINE_BASELINESTORE_HPP
