/**
 ******************************************************************************
 * @file    Claim.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   One row, one claim, one method. The unit of storage and of diff.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * Two axes, kept apart
 *
 * **Confidence** is a property of one claim. **Completeness** is a property of
 * a sensor, and it is a fraction. They are never combined into a single score,
 * because a sensor with one CONFIRMED row out of twelve is not "8 % confident"
 * -- it is 8 % complete and confident about one thing, and a number that
 * conflated the two would be wrong in both directions at once.
 *
 * ---------------------------------------------------------------------------
 * The rules this file makes mechanical
 *
 *  - **No claim without a run.** `record()` refuses a row whose `runId` is 0.
 *  - **No distribution below its minimum n.** `record()` demotes to UNVERIFIED
 *    with a note saying how many samples were seen. The catalogue holds the
 *    minimum; nothing here decides it.
 *  - **A negative result is a result.** `SPO2 does not resolve` is a CONFIRMED
 *    row with value 0, not an absent one.
 *  - **Absent, silent and stuck are three findings.** They are three different
 *    claims -- `existence.default_resolves`, `liveness.samples_per_min` and
 *    `value.f0_ever_changed` -- and no code path collapses them.
 *  - **INAPPLICABLE is not UNVERIFIED.** The completeness denominator excludes
 *    the first and includes the second, so a dt distribution for an event
 *    sensor does not make the profile look unfinished for ever.
 *  - **Reading cannot CONFIRM.** `record()` takes a `Source`, and a claim whose
 *    source is `SpecRead` can be REFUTED or left UNVERIFIED but never promoted
 *    to CONFIRMED. Three rows in SleepLab's ledger were refuted by reading
 *    code; none was ever confirmed that way, and this is that rule in the
 *    type system rather than in a review comment.
 *
 * No SDK header. Testable at a desk.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_CLAIM_HPP
#define SENSORLAB_CLAIM_HPP

#include <cstddef>
#include <cstdint>

namespace SensorLab::Evidence
{

/// Confidence in one claim. The four tags the hardware-recovery investigation
/// established, plus the one a profiler needs.
enum class Verdict : uint8_t
{
    /// Nobody has checked. The row names the probe that would. This is the
    /// initial state of every claim in the catalogue, which is what makes the
    /// completeness fraction honest from the first run.
    Unverified = 0,
    /// Measured directly on this hardware, with n recorded, by a probe whose
    /// method is written down.
    Confirmed,
    /// Inferred from another measurement, or from documentation that is itself
    /// unverified. Good enough to design against; not good enough to state as
    /// fact.
    Likely,
    /// Checked and found false. Kept on the record: a claim that was believed
    /// and is now known wrong is worth more than a deleted one.
    Refuted,
    /// The claim cannot exist for this sensor -- a dt distribution for an event
    /// sensor, a range check on an enum, a reference comparison for a type with
    /// no driver. Distinct from Unverified, and the report must not count it as
    /// missing.
    Inapplicable,
};

const char *toString(Verdict v);

/// Whether the measured value agrees with the spec, where a spec makes a claim.
enum class Conformance : uint8_t
{
    /// No spec anywhere states a value for this claim. The common case, and not
    /// a failing: it is what makes the measurement worth publishing.
    NoClaim = 0,
    Matches,
    Differs,
    /// A spec states a value and this app cannot reach it -- a register the
    /// app is not allowed to read, a reference instrument nobody has.
    Untestable,
};

const char *toString(Conformance c);

/// Where a verdict came from. The type system's half of "reading cannot
/// confirm".
enum class Source : uint8_t
{
    /// A probe ran on this device and produced a number.
    Measured = 0,
    /// A header, a doc or a datasheet was read. May refute; may never confirm.
    SpecRead,
    /// Derived from another claim in this profile rather than measured
    /// directly. Yields LIKELY at best.
    Derived,
};

/// Canned notes. A free-text field per claim would cost 40 bytes times two
/// thousand rows; these cover every case the probes actually produce, and the
/// one genuinely free-text string in the profile -- the 32-char driver
/// descriptor -- has its own side table in `ClaimStore`.
enum class Note : uint8_t
{
    None = 0,
    BelowMinimumN,        ///< Seen fewer samples than the metric requires.
    NoProducer,           ///< RequestDefault resolved nothing to subscribe to.
    ResolvedButSilent,    ///< A driver resolved and delivered nothing.
    EventSensorNoDt,      ///< Classified as event: dt statistics do not apply.
    NeverVaried,          ///< The value never changed, so an LSB is meaningless.
    ParserFrameShort,     ///< Delivered fewer fields than the parser expects.
    ParserFrameExtended,  ///< Delivered more fields than the parser expects.
    FrameWiderThanSlots,  ///< More fields than the catalogue reserved slots for.
    TruncatedByUsb,       ///< The run ended when the cable went in.
    TruncatedByReboot,
    NotBuiltYet,          ///< The probe for this layer is not implemented.
    NoReferenceEntered,   ///< A guided protocol ran with no reference value.
    Count
};

const char *toString(Note n);

/// Value flags, packed into one byte.
namespace Flag
{
constexpr uint8_t kHasValue  = 1u << 0;
constexpr uint8_t kHasSpread = 1u << 1;
/// The row's value came from reading a spec rather than from the device. Set
/// whenever `Source` is not `Measured`, and carried into the profile so a
/// reader can tell a measurement from an inference at a glance.
constexpr uint8_t kInferred  = 1u << 2;
} // namespace Flag

/// p05/p50/p95 for the claims that are distributions. Held in a side table
/// rather than in every row: 28 bytes times two thousand rows is the app's
/// largest allocation already, and fewer than one row in ten is a distribution.
struct Spread
{
    float p05 = 0.0f;
    float p50 = 0.0f;
    float p95 = 0.0f;
};

/// No spread recorded.
constexpr uint16_t kNoSpread = 0xFFFFu;

/**
 * @brief One claim's measured state.
 *
 * Everything that is a property of the *claim* rather than of the *measurement*
 * -- its id, its metric, its unit, its method, its minimum n, the spec's
 * expectation and the source of that expectation -- lives in the catalogue, in
 * flash. This struct is only what a run found out.
 *
 * 28 bytes, and the arithmetic matters: see `ClaimStore.hpp`.
 */
struct Claim
{
    /// The measurement. Meaningful only when `Flag::kHasValue` is set; a claim
    /// like `existence.descriptor` carries no number at all.
    float    value    = 0.0f;
    /// Samples, batches, sweep points or protocol runs that produced it --
    /// which of those it is depends on the metric, and the catalogue says.
    /// **No number without an n.**
    uint32_t n        = 0;
    /// Device uptime at the moment of measurement. The monotonic clock, and
    /// the only one any duration is derived from.
    uint32_t uptimeMs = 0;
    /// Wall clock, for labelling only. Never differenced.
    int64_t  wallUtc  = 0;

    uint16_t spreadIdx  = kNoSpread;
    uint16_t runId      = 0;

    Verdict     verdict     = Verdict::Unverified;
    Conformance conformance = Conformance::NoClaim;
    Note        note        = Note::None;
    uint8_t     flags       = 0;

    bool hasValue()  const { return (flags & Flag::kHasValue)  != 0; }
    bool hasSpread() const { return (flags & Flag::kHasSpread) != 0; }
    bool inferred()  const { return (flags & Flag::kInferred)  != 0; }

    /// Answered, for the completeness fraction: any verdict but Unverified.
    /// INAPPLICABLE counts as answered *and* is excluded from the denominator,
    /// so it neither inflates nor depresses the fraction.
    bool answered() const { return verdict != Verdict::Unverified; }
};

static_assert(sizeof(Claim) <= 32,
              "Claim is the app's largest per-row cost; keep it under 32 bytes "
              "or redo the arithmetic in ClaimStore.hpp");

/**
 * @brief One measurement, before the rules are applied.
 *
 * Probes fill this in and hand it to `ClaimStore::record()`, which decides the
 * verdict. Separating the two is what stops a probe from promoting its own
 * result: a probe reports what it saw and how, and one place applies the
 * minimum-n rule, the reading-cannot-confirm rule and the conformance
 * comparison.
 */
struct Measurement
{
    bool     hasValue = false;
    float    value    = 0.0f;
    uint32_t n        = 0;
    Source   source   = Source::Measured;

    bool     hasSpread = false;
    Spread   spread {};

    /// The probe's own reading, before the minimum-n rule. A probe that has
    /// measured a thing says Confirmed; one that has measured something
    /// contradicting a spec says Refuted; one that has established the claim
    /// cannot apply says Inapplicable.
    Verdict  verdict = Verdict::Confirmed;
    Note     note    = Note::None;
};

} // namespace SensorLab::Evidence

#endif // SENSORLAB_CLAIM_HPP
