/**
 ******************************************************************************
 * @file    ClaimStore.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Every claim in the catalogue, and the rules that promote one.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * Why the whole catalogue lives in RAM
 *
 * The completeness fraction is `answered / applicable` over a denominator that
 * was fixed before any measurement was taken. Computing it needs every claim's
 * verdict at once, and displaying it on the roster needs that on every screen
 * refresh. A store that held only the claims this run touched could not
 * produce the denominator, and a store that read it back off the volume would
 * do file I/O on a screen refresh.
 *
 * The arithmetic, because this is the app's largest allocation by an order of
 * magnitude:
 *
 *     claims        37 types x 26 per-type metrics            =    962
 *                 + 116 field slots x 8 per-field metrics     =    928
 *                 + the layer 7/8 bespoke list                 ~    85
 *                                                              -------
 *                                                              ~  1975
 *     x sizeof(Claim)                                    28 B  ~ 55 KB
 *     + spreads     kMaxSpreads x sizeof(Spread)         12 B  =  3 KB
 *     + descriptors 37 x 33 B                                  = 1.2 KB
 *                                                              -------
 *                                                              ~ 60 KB
 *
 * against the service's 500 KB. It is static, so it is in .bss at link time and
 * a build that no longer fits fails to link rather than failing at 03:00.
 *
 * ---------------------------------------------------------------------------
 * What `record()` will not let a probe do
 *
 * The rules from `Claim.hpp` live here, in one function, because a rule
 * enforced at each of a dozen call sites is a rule that has eleven chances to
 * be forgotten:
 *
 *   1. A row with no run id is refused outright.
 *   2. A claim below its metric's minimum n stays UNVERIFIED, and the note says
 *      BelowMinimumN so the screen can say how much longer is needed. The value
 *      is still stored, so the report can show progress -- but the *verdict* is
 *      not promoted, which is the only thing a reader acts on.
 *   3. `Source::SpecRead` can produce REFUTED or leave UNVERIFIED and can never
 *      produce CONFIRMED. `Source::Derived` tops out at LIKELY.
 *   4. INAPPLICABLE is sticky in one direction only: it can be set by a probe
 *      and cleared by a probe (a firmware update that resolves a driver turns
 *      a whole protocol back on), but it never *becomes* a measurement without
 *      one.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_CLAIMSTORE_HPP
#define SENSORLAB_CLAIMSTORE_HPP

#include <cstddef>
#include <cstdint>

#include "Catalogue/Catalogue.hpp"
#include "Evidence/Claim.hpp"

namespace SensorLab::Evidence
{

/// How many distribution claims can carry a spread.
///
/// Distribution metrics are `samples_per_batch`, `dt_ms` and `batch_jitter_ms`
/// -- three per type, so 111 -- plus headroom for the bespoke protocols that
/// report an error spread across runs. Rounded up rather than computed, because
/// the bespoke list grows and a static_assert on the exact figure would fail
/// every time somebody adds a protocol.
constexpr size_t kMaxSpreads = 192;

/// The 32-char driver descriptor plus its terminator. `RequestGetDesc` has
/// never been used by any app in either repository; this is where its answer
/// goes.
constexpr size_t kDescriptorMax = 33;

/// A sensor's completeness, and enough detail to say *why* it is what it is.
struct Completeness
{
    uint16_t applicable = 0;  ///< Claims that could be answered for this sensor.
    uint16_t answered   = 0;  ///< ...of which have any verdict but UNVERIFIED.
    uint16_t confirmed  = 0;
    uint16_t refuted    = 0;
    uint16_t likely     = 0;
    uint16_t inapplicable = 0; ///< Excluded from `applicable`, counted here.

    /// Percent, rounded down. Zero applicable claims reads as 100 %: there is
    /// nothing left to measure, which is different from nothing measured.
    uint8_t percent() const
    {
        if (applicable == 0) {
            return 100;
        }
        return static_cast<uint8_t>((static_cast<uint32_t>(answered) * 100u)
                                    / applicable);
    }
};

/**
 * @brief The claim table, and the only place a verdict is decided.
 *
 * Not a singleton and not global: the harness constructs one per scenario, and
 * a test that could not do that would be testing a different store from the one
 * the service uses. The service owns exactly one, by value.
 */
class ClaimStore
{
public:
    ClaimStore();

    /// Reset every claim to UNVERIFIED and forget every spread and descriptor.
    /// Called at construction and by the harness between scenarios.
    void clear();

    /// The run every subsequent `record()` is attributed to.
    ///
    /// **No claim without a run**: `record()` refuses while this is 0, which is
    /// its value until a run has been opened and its manifest written.
    void setRunId(uint16_t runId) { mRunId = runId; }
    uint16_t runId() const { return mRunId; }

    /// Apply the rules and store the result. Returns the verdict actually
    /// stored, which is not always the one the probe asked for.
    ///
    /// @param claimIdx  From `Catalogue::claimIndex(...)`.
    /// @param m         What the probe saw.
    /// @param uptimeMs  `kernel.sys.getTimeMs()` at the moment of measurement.
    /// @param wallUtc   `time(nullptr)`, or -1 if the clock is unreadable.
    Verdict record(size_t claimIdx, const Measurement &m,
                   uint32_t uptimeMs, int64_t wallUtc);

    /// Mark a claim INAPPLICABLE with a reason. Separate from `record()`
    /// because it is not a measurement: nothing was sampled and no n applies.
    void markInapplicable(size_t claimIdx, Note note, uint32_t uptimeMs,
                          int64_t wallUtc);

    /// Undo an INAPPLICABLE, returning the claim to UNVERIFIED.
    ///
    /// This is what makes a firmware update turn a whole protocol back on: the
    /// SPO2 rows are INAPPLICABLE because no driver resolves, and layer 1
    /// clears them the moment one does. Without it, a driver appearing would be
    /// invisible in a document that had already written the sensor off.
    void clearInapplicable(size_t claimIdx);

    const Claim &at(size_t claimIdx) const;

    /// The spread for a distribution claim, or nullptr.
    const Spread *spreadFor(size_t claimIdx) const;

    /// Store the driver descriptor for a type. Truncated at 32 characters,
    /// which is `RequestGetDesc::desc`'s own width.
    void setDescriptor(size_t typeIdx, const char *desc);
    /// "" when none has been read.
    const char *descriptor(size_t typeIdx) const;

    /// Completeness for one sensor type, across every layer.
    Completeness completenessForType(size_t typeIdx) const;
    /// Completeness for one sensor type within one layer, which is what the
    /// roster's per-row glyph is drawn from.
    Completeness completenessForType(size_t typeIdx, Catalogue::Layer layer) const;
    /// Completeness for one layer across every sensor.
    Completeness completenessForLayer(Catalogue::Layer layer) const;
    /// Completeness for the whole profile. Displayed alongside every result,
    /// because a screen that shows findings without showing how much is missing
    /// is a screen that reads as finished.
    Completeness completenessOverall() const;

    /// Spreads used, for the ledger's own bookkeeping and for a test that the
    /// table does not silently fill up.
    size_t spreadsUsed() const { return mSpreadsUsed; }
    /// Spread allocations that were dropped because the side table was full.
    /// Non-zero means `kMaxSpreads` is too small and a distribution lost its
    /// quantiles -- which the profile then reports as a value with no spread
    /// rather than pretending.
    uint32_t spreadsDropped() const { return mSpreadsDropped; }

private:
    /// Highest verdict this source may produce.
    static Verdict ceilingFor(Source s);

    /// Minimum n for a claim, from the catalogue.
    static uint32_t minimumNFor(size_t claimIdx);

    static constexpr size_t kCapacity =
        Catalogue::kBlockATotal + Catalogue::kBlockBTotal;

    /// Sized for the per-type and per-field blocks plus a generous bespoke
    /// list. The runtime `Catalogue::claimCount()` is asserted against it in
    /// the constructor, so outgrowing it is a startup failure with a message
    /// rather than an out-of-bounds write.
    static constexpr size_t kBespokeHeadroom = 160;
    static constexpr size_t kSlots = kCapacity + kBespokeHeadroom;

    Claim    mClaims[kSlots];
    Spread   mSpreads[kMaxSpreads];
    char     mDescriptors[Catalogue::kTypeCount][kDescriptorMax];

    size_t   mSpreadsUsed    = 0;
    uint32_t mSpreadsDropped = 0;
    uint16_t mRunId          = 0;
    /// Claims the store actually has room for, `min(catalogue, kSlots)`.
    size_t   mCount          = 0;
};

} // namespace SensorLab::Evidence

#endif // SENSORLAB_CLAIMSTORE_HPP
