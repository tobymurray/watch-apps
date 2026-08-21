/**
 ******************************************************************************
 * @file    Catalogue.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The probe catalogue: every claim this app can ever make, enumerated.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * The catalogue is the spec of the app
 *
 * The roster screen, the completeness fraction, `profile.json` and the report
 * are all generated from this file. Nothing anywhere else knows how many
 * claims there are, which sensors a probe applies to, or what a metric is
 * called. That is deliberate: a profiler whose display and whose report can
 * disagree about what has been measured is a profiler that will eventually
 * publish the disagreement.
 *
 * Every claim in the catalogue exists from the first run, tagged UNVERIFIED,
 * naming the probe that would settle it. So the completeness fraction is
 * honest from minute one -- `answered / applicable` over a denominator that
 * was fixed before any measurement was taken, rather than over "the things we
 * happened to try". A profiler that only counts what it attempted always looks
 * finished.
 *
 * ---------------------------------------------------------------------------
 * Claim identity, fixed once
 *
 * `claim_id` is what `profile_diff.py` keys on, so renaming one silently
 * breaks every firmware comparison that spans the rename. The scheme, and the
 * rules that keep it stable, are in `SensorLab/Docs/CLAIM-IDS.md`. In short:
 *
 *     <scope>.<layer>.<metric>
 *
 *     0x10.timing.dt_p50          sensor 0x10, layer 4, median inter-sample dt
 *     0x10.value.f0_lsb           ...its field 0's recovered quantisation step
 *     platform.consistency.uptime_vs_wall_drift_ppm
 *
 * `<scope>` is the sensor type's value in lower-case hex, or the literal
 * `platform` for the handful of claims that are not about one sensor.  It is
 * never the enumerator name: a type that gets renamed upstream keeps its
 * value, and the value is what the kernel actually dispatches on.
 *
 * ---------------------------------------------------------------------------
 * How the claim index is laid out, and why it is arithmetic
 *
 * The store is a static array with a compile-time bound, because the platform
 * forbids allocation in the sample path and a profiler that ran out of rows
 * halfway through a sweep would produce a document that looked complete. So
 * `claim index` is computed, not searched:
 *
 *   Block A   per-type metrics (layers 1, 2, 3, 4, 6)
 *             index = typeIdx * kPerTypeMetrics + metric
 *   Block B   per-field metrics (layer 5)
 *             index = kBlockA + fieldBase(typeIdx) + field * kFieldMetrics + metric
 *   Block C   the bespoke claims (layers 7, 8), one table entry each
 *
 * `fieldBase` is a constexpr prefix sum over each type's field slot count --
 * its parser's field count, or `kAssumedFields` for the five types the SDK
 * ships no parser for, whose real width is not knowable until layer 2 has
 * measured it.
 *
 * This header includes no SDK header at all, deliberately, so every rule in it
 * is testable at a desk. See `SensorLab/Tests/README.md`.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_CATALOGUE_HPP
#define SENSORLAB_CATALOGUE_HPP

#include <cstddef>
#include <cstdint>

#include "Catalogue/SensorTypeTable.generated.hpp"

namespace SensorLab::Catalogue
{

/// Bumped when a claim is added, removed or redefined. Written into every
/// profile and every run manifest: two profiles from different catalogue
/// versions are still diffable claim by claim, but a claim missing from one of
/// them means "this build could not measure it", not "the device changed", and
/// only the version tells them apart.
constexpr uint32_t kCatalogueVersion = 1;

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

/// The eight probe layers, in the order they are cheap to run.
///
/// The numbering is the prompt's and the documentation's, and it is stable:
/// `method_id` strings are built from it.
enum class Layer : uint8_t
{
    Existence   = 1,   ///< P1  resolve, enumerate, name, connect.
    Frame       = 2,   ///< P2  delivered field count vs the shipped parser.
    Liveness    = 3,   ///< P3  does it speak, how often, and with what gaps.
    Timing      = 4,   ///< P4  inter-sample dt, clocks, monotonicity, skew.
    Value       = 5,   ///< P5  per-field domain, resolution, stuck, non-finite.
    Control     = 6,   ///< P6  does the requested period/latency do anything.
    Physical    = 7,   ///< P7  guided protocols against a physical reference.
    Consistency = 8,   ///< P8  sensors against each other, unattended.
};

constexpr size_t kLayerCount = 8;

/// Short, stable name used in `method_id` and in the report's headings.
const char *layerName(Layer layer);

/// The prompt's method identifiers, e.g. "P4.dt-histogram". Every claim row
/// carries one, and it is what a reader follows to find out how a number was
/// obtained.
const char *layerMethodPrefix(Layer layer);

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------

/// Per-type metrics: layers 1, 2, 3, 4 and 6.
///
/// One enumerator per claim a sensor can carry that is not per-field and not
/// bespoke. The order is the store's layout, so **append only** -- inserting
/// one renumbers every claim index after it, which is harmless in RAM and
/// catastrophic if a `state.json` written by an older build is resumed.
enum class Metric : uint8_t
{
    // -- Layer 1: existence and identity -----------------------------------
    DefaultResolves = 0,   ///< RequestDefault returned a handle.
    DriverCount,           ///< RequestList's handle count. Nobody has seen this.
    Descriptor,            ///< RequestGetDesc's 32 chars. The kernel naming itself.
    ConnectSucceeds,       ///< RequestConnect succeeded, which is a separate question.

    // -- Layer 2: frame structure ------------------------------------------
    FieldCount,            ///< Delivered, derived from EventData::stride.
    ParserAgreement,       ///< Delivered count vs the shipped parser's.
    ParserAcceptsFrame,    ///< Whether the shipped isDataValid() accepts it.
    StrideStable,          ///< Did the field count ever change, within or across runs.

    // -- Layer 3: liveness --------------------------------------------------
    FirstSampleMs,         ///< connect() to first sample.
    SamplesPerMin,
    BatchesPerMin,
    SamplesPerBatch,       ///< Distribution, not just the mean.
    LongestGapMs,          ///< Never report a rate without this.
    Classification,        ///< Streaming or event, measured rather than assumed.

    // -- Layer 4: timing ---------------------------------------------------
    DtMs,                  ///< Inter-sample dt distribution. Delivered rate is 1/p50.
    DeliveredHz,
    TimestampUsOver999,    ///< Violations of DataView::getTimestampUs()'s assumption.
    TimestampMonotonic,
    ClockSkewPpm,          ///< Sample stamps against getTimeMs(), over hours.
    BatchJitterMs,         ///< Batch arrival, separately from sample dt.

    // -- Layer 6: control surface ------------------------------------------
    PeriodHonoured,        ///< Honoured / ignored / floored. Row S3 for one sensor.
    PeriodFloorMs,         ///< The fastest delivered period, if there is a floor.
    LatencyHonoured,       ///< Row S17 for one sensor.
    ReconnectStable,       ///< 100 disconnect/connect cycles.
    HandleStableOnReconnect,
    SecondConnection,      ///< Two connections to one type. Half of row S8.

    Count                  ///< Not a metric.
};

constexpr size_t kPerTypeMetrics = static_cast<size_t>(Metric::Count);

/// Per-field metrics: layer 5 only.
enum class FieldMetric : uint8_t
{
    Min = 0,
    Max,
    Mean,
    Lsb,               ///< Smallest non-zero |difference| between distinct values.
    NonFinite,         ///< NaN, +-Inf and denormals. Not hypothetical: see the ledger.
    StuckMaxRun,       ///< Longest run of byte-identical values.
    EverChanged,       ///< Did this field ever move at all.
    DistinctObserved,  ///< For enums and booleans: how many values were ever seen.

    Count
};

constexpr size_t kFieldMetrics = static_cast<size_t>(FieldMetric::Count);

/// Field slots reserved for a type the SDK ships no parser for.
///
/// `MAGNETIC_FIELD`, `HEART_BEAT`, `GESTURE_RECOGNITION`, `PPG` and `ECG` have
/// no documented width anywhere, so layer 2 has to measure it. Eight is the
/// largest frame any parsed type uses (`HEART_RATE_EX` at 7) plus one, chosen
/// so a discovered frame one field wider than anything currently shipped still
/// records every field rather than silently truncating at the interesting one.
/// A frame wider than this records the first eight fields and raises the
/// `frame.field_count` claim's note; it does not overflow.
constexpr uint8_t kAssumedFields = 8;

/// Which metrics belong to which layer. Table-driven so the report and the
/// completeness fraction cannot disagree with the roster screen.
Layer metricLayer(Metric m);

/// The metric's name as it appears in a `claim_id`, e.g. "dt_p50" -> "dt_ms".
const char *metricName(Metric m);
const char *fieldMetricName(FieldMetric m);

/// The unit a claim's value carries, or "" where it is dimensionless. Part of
/// the catalogue rather than of the measurement, because a unit is a property
/// of the metric and a number whose unit came from the run is a number nobody
/// can check.
const char *metricUnit(Metric m);
const char *fieldMetricUnit(FieldMetric m);

/// True when this metric's value is a distribution, so the row must carry
/// p05/p50/p95 as well as a headline figure.
bool metricIsDistribution(Metric m);

/// Minimum sample count before this metric may leave UNVERIFIED.
///
/// The prompt's rule, made mechanical: below the minimum the verdict stays
/// UNVERIFIED with a note saying how many samples were seen, and the screen
/// says how much longer is needed. Displaying "needs 40 more minutes" is a
/// feature; promoting a p95 off 30 samples is a lie with a pedigree.
uint32_t metricMinimumN(Metric m);
uint32_t fieldMetricMinimumN(FieldMetric m);

// ---------------------------------------------------------------------------
// Type helpers
// ---------------------------------------------------------------------------

/// Sentinel scope for claims that are not about one sensor. Formatted as
/// "platform" in a claim_id.
constexpr uint16_t kPlatformScope = 0xFFFFu;

/// Field slots this type reserves in block B.
constexpr uint8_t fieldSlots(size_t typeIdx)
{
    return kTypes[typeIdx].parser == kNoParser
               ? kAssumedFields
               : kParsers[kTypes[typeIdx].parser].fieldCount;
}

/// Prefix sum of `fieldSlots` over types [0, typeIdx).
constexpr size_t fieldBase(size_t typeIdx)
{
    size_t total = 0;
    for (size_t i = 0; i < typeIdx; i++) {
        total += fieldSlots(i);
    }
    return total;
}

constexpr size_t kFieldSlotTotal = fieldBase(kTypeCount);

// ---------------------------------------------------------------------------
// Bespoke claims: layers 7 and 8
// ---------------------------------------------------------------------------

/// One claim that is neither per-type-metric nor per-field.
///
/// Layers 7 and 8 are not a grid: a six-face static test belongs to the
/// accelerometer and to nothing else, and "does STEP_COUNTER_DAILY reset at
/// local midnight" belongs to one type and needs a run that crosses midnight.
/// So they are a list, and the list is the spec of what those tiers have to
/// build.
struct BespokeClaim
{
    /// Sensor type value, or `kPlatformScope`.
    uint16_t    scope;
    Layer       layer;
    /// The metric half of the claim_id. Must never change: see CLAIM-IDS.md.
    const char *metric;
    /// Unit, or "" if dimensionless.
    const char *unit;
    /// The method that would settle it, appended to the layer's prefix.
    const char *method;
    /// Minimum n before the claim may leave UNVERIFIED. For a guided protocol
    /// this is runs of the protocol, not samples.
    uint32_t    minimumN;
    /// True when the claim cannot be answered on this device at all -- no
    /// electrodes for ECG, no driver for SPO2. Recorded as INAPPLICABLE with
    /// this reason rather than left UNVERIFIED, so the report does not count it
    /// as missing. A firmware update that resolves a driver turns it back on:
    /// layer 1 clears the INAPPLICABLE when `DefaultResolves` flips.
    const char *inapplicableReason;
};

extern const BespokeClaim kBespoke[];
extern const size_t       kBespokeCount;

// ---------------------------------------------------------------------------
// The index space
// ---------------------------------------------------------------------------

constexpr size_t kBlockATotal = kTypeCount * kPerTypeMetrics;
constexpr size_t kBlockBTotal = kFieldSlotTotal * kFieldMetrics;

/// Every claim this build can hold. The store is exactly this long.
///
/// At 28 bytes a row this is the app's single largest RAM allocation, and it is
/// static -- see `ClaimStore.hpp` for the arithmetic against the service's
/// 500 KB.
size_t claimCount();

/// Claim index for a per-type metric. No bounds checking: both arguments come
/// from loops over `kTypeCount` and `Metric::Count`.
constexpr size_t claimIndex(size_t typeIdx, Metric m)
{
    return typeIdx * kPerTypeMetrics + static_cast<size_t>(m);
}

/// Claim index for a per-field metric.
constexpr size_t claimIndex(size_t typeIdx, uint8_t fieldIdx, FieldMetric m)
{
    return kBlockATotal + (fieldBase(typeIdx) + fieldIdx) * kFieldMetrics
           + static_cast<size_t>(m);
}

/// Claim index for a bespoke claim, by its position in `kBespoke`.
inline size_t bespokeClaimIndex(size_t bespokeIdx)
{
    return kBlockATotal + kBlockBTotal + bespokeIdx;
}

/// Which block an index falls in, and its decomposition. Used by the writer and
/// by the roster; nothing else should need it.
struct ClaimRef
{
    enum class Kind : uint8_t { PerType, PerField, Bespoke } kind;
    /// Index into `kTypes`, or `kTypeCount` for a platform-scoped bespoke claim.
    size_t      typeIdx;
    Metric      metric;      ///< PerType only.
    uint8_t     fieldIdx;    ///< PerField only.
    FieldMetric fieldMetric; ///< PerField only.
    size_t      bespokeIdx;  ///< Bespoke only.
    Layer       layer;
};

/// Decompose a claim index. Total over `claimCount()` is what the writer walks.
ClaimRef describe(size_t claimIdx);

/// Format a claim_id into @p out. Returns the length written, or 0 if @p out is
/// too small. `kClaimIdMax` is the widest the scheme can produce.
constexpr size_t kClaimIdMax = 64;
size_t formatClaimId(char *out, size_t outSize, size_t claimIdx);

/// Format the `method_id`, e.g. "P4.dt-histogram".
constexpr size_t kMethodIdMax = 48;
size_t formatMethodId(char *out, size_t outSize, size_t claimIdx);

/// The spec's claim for this row, where a spec makes one, and where to find it.
///
/// Populated only from the generated table -- a header path and, for a field
/// count, the parser's `Field::COUNT`. Anything that would need a datasheet or
/// a doc section is left absent here and filled in by the host report from
/// `Docs/EXPECTED.md`, because a datasheet figure compiled into an app is a
/// figure nobody can check against its source.
struct Expectation
{
    bool        hasValue;
    float       value;
    /// A file and line, a doc section, or "" for no claim.
    const char *source;
};

Expectation expectationFor(size_t claimIdx);

/// Index of a sensor type by its value, or `kTypeCount` if unknown.
size_t typeIndexOf(uint32_t typeValue);

} // namespace SensorLab::Catalogue

#endif // SENSORLAB_CATALOGUE_HPP
