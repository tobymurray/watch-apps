/**
 ******************************************************************************
 * @file    Catalogue.cpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The probe catalogue. Rationale is in Catalogue.hpp.
 ******************************************************************************
 */

#include "Catalogue/Catalogue.hpp"

#include <cstdio>

namespace SensorLab::Catalogue
{

namespace
{

/// One row per per-type metric, in `Metric` order. Kept as a table rather than
/// a switch so that adding a metric without naming it, dimensioning it and
/// saying what would settle it is a compile error.
struct MetricSpec
{
    Layer       layer;
    const char *name;
    const char *unit;
    const char *method;      ///< appended to the layer prefix
    bool        distribution;
    uint32_t    minimumN;
};

/// Minimum n for the timing distributions. The prompt's figure, and the reason
/// for it: at the ~48 Hz the accelerometer actually delivers (ledger row S3),
/// ten thousand samples is about three and a half minutes -- long enough that a
/// p95 is a property of the sensor rather than of the minute it was sampled in,
/// short enough that a Tier 2 run per sensor group is an evening rather than a
/// week. It is *not* enough for the skew claim, which is why that one is
/// separate.
constexpr uint32_t kDtMinimumN = 10000;

/// Minimum n for clock skew. Skew is a slope, and a slope needs a baseline: at
/// 48 Hz this is roughly three hours, which is the shortest run in which a
/// drift of a few tens of ppm is distinguishable from the quantisation of a
/// millisecond timestamp. TODO: replace with the run that measures the actual
/// residual, once Tier 2 has one -- the figure below is arithmetic, not
/// measurement.
constexpr uint32_t kSkewMinimumN = 500000;

/// Minimum n for a rate. One minute at 1 Hz is the slowest sensor's whole
/// minute; anything less cannot distinguish "slow" from "just started".
constexpr uint32_t kRateMinimumN = 60;

/// Minimum n for a per-field distribution. Lower than the timing minimum,
/// because a min/max/mean converges far faster than a p95 -- but not 1: a
/// single sample would let `value.f0_min` be promoted to CONFIRMED off one
/// reading, and `ever_changed` needs at least two.
constexpr uint32_t kFieldMinimumN = 1000;

/// Minimum n for the quantisation-step estimate. It is a *lower bound* on the
/// LSB and only meaningful once the value has actually varied, so it needs
/// enough distinct values that the smallest observed step is plausibly the
/// real one. TODO: this is a guess; the run that would justify it is a layer-5
/// soak of `ACCELEROMETER_RAW`, whose LSB is knowable independently from the
/// BMI270's configured range.
constexpr uint32_t kLsbMinimumN = 5000;

constexpr MetricSpec kMetrics[kPerTypeMetrics] = {
    // -- Layer 1 -----------------------------------------------------------
    // n = 1: a driver either resolved or it did not, and one attempt settles
    // it. A negative result here is a CONFIRMED row, not an absent one --
    // ledger rows S4 and S5 are exactly this, and they closed design questions
    // permanently.
    { Layer::Existence, "default_resolves",  "",     "request-default",  false, 1 },
    { Layer::Existence, "driver_count",      "",     "request-list",     false, 1 },
    { Layer::Existence, "descriptor",        "",     "request-get-desc", false, 1 },
    { Layer::Existence, "connect_succeeds",  "",     "request-connect",  false, 1 },

    // -- Layer 2 -----------------------------------------------------------
    // The field count comes from one batch's stride, so n = 1 batch is enough
    // to know the width. `stride_stable` needs more than one, by definition.
    { Layer::Frame, "field_count",           "fields", "stride-arithmetic", false, 1 },
    { Layer::Frame, "parser_agreement",      "",       "stride-vs-parser",  false, 1 },
    { Layer::Frame, "parser_accepts_frame",  "",       "is-data-valid",     false, 1 },
    { Layer::Frame, "stride_stable",         "",       "stride-watch",      false, 2 },

    // -- Layer 3 -----------------------------------------------------------
    { Layer::Liveness, "first_sample_ms",    "ms",     "connect-to-first",  false, 1 },
    { Layer::Liveness, "samples_per_min",    "1/min",  "delivery-count",    false, kRateMinimumN },
    { Layer::Liveness, "batches_per_min",    "1/min",  "delivery-count",    false, 1 },
    { Layer::Liveness, "samples_per_batch",  "samples","batch-histogram",   true,  30 },
    { Layer::Liveness, "longest_gap_ms",     "ms",     "delivery-gap",      false, kRateMinimumN },
    { Layer::Liveness, "classification",     "",       "classify",          false, kRateMinimumN },

    // -- Layer 4 -----------------------------------------------------------
    { Layer::Timing, "dt_ms",                "ms",     "dt-histogram",      true,  kDtMinimumN },
    { Layer::Timing, "delivered_hz",         "Hz",     "dt-histogram",      false, kDtMinimumN },
    { Layer::Timing, "ts_us_over_999",       "samples","us-invariant",      false, kDtMinimumN },
    { Layer::Timing, "ts_monotonic",         "",       "monotonicity",      false, kDtMinimumN },
    { Layer::Timing, "clock_skew_ppm",       "ppm",    "skew-regression",   false, kSkewMinimumN },
    { Layer::Timing, "batch_jitter_ms",      "ms",     "batch-arrival",     true,  1000 },

    // -- Layer 6 -----------------------------------------------------------
    // n counts sweep points, not samples: eight requested periods is the grid
    // in the prompt, and a sweep that stopped early cannot say whether the
    // period was ignored or merely floored.
    { Layer::Control, "period_honoured",     "",       "period-sweep",      false, 8 },
    { Layer::Control, "period_floor_ms",     "ms",     "period-sweep",      false, 8 },
    { Layer::Control, "latency_honoured",    "",       "latency-sweep",     false, 4 },
    { Layer::Control, "reconnect_stable",    "cycles", "reconnect-soak",    false, 100 },
    { Layer::Control, "handle_stable_on_reconnect", "","reconnect-soak",    false, 100 },
    { Layer::Control, "second_connection",   "",       "contention",        false, 1 },
};

struct FieldMetricSpec
{
    const char *name;
    const char *unit;
    const char *method;
    uint32_t    minimumN;
};

constexpr FieldMetricSpec kFieldMetricSpecs[kFieldMetrics] = {
    { "min",              "", "field-extrema",     kFieldMinimumN },
    { "max",              "", "field-extrema",     kFieldMinimumN },
    { "mean",             "", "field-mean",        kFieldMinimumN },
    // The unit is the field's own, which the catalogue does not know: it is a
    // recovered quantisation step in whatever the field is measured in. Left
    // blank rather than guessed, and the report prints it against the field's
    // parser doc comment.
    { "lsb",              "", "quantisation-step", kLsbMinimumN },
    { "nonfinite",        "samples", "nonfinite-count", 1 },
    { "stuck_max_run",    "samples", "stuck-run",       kFieldMinimumN },
    { "ever_changed",     "",        "stuck-run",       2 },
    { "distinct_observed","values",  "distinct-values", kFieldMinimumN },
};

const char *const kLayerNames[kLayerCount] = {
    "existence", "frame", "liveness", "timing",
    "value", "control", "physical", "consistency",
};

const char *const kLayerPrefixes[kLayerCount] = {
    "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8",
};

} // namespace

// ---------------------------------------------------------------------------
// Bespoke claims
// ---------------------------------------------------------------------------

// Layers 7 and 8. This list *is* the specification of Tier 3 and Tier 4: every
// entry is a claim those tiers exist to answer, it appears in the completeness
// denominator from the first run, and until the tier is built it reads
// UNVERIFIED with the method that would settle it. That is the honest way to
// ship a profiler at Tier 1.
//
// Minimum n for a guided protocol counts *runs of the protocol*, not samples.
// Three is the smallest number that yields a spread, and spread across runs of
// the same protocol is real information about the sensor -- see the prompt's
// Tier 4 requirement that re-running appends rather than overwrites.
const BespokeClaim kBespoke[] = {
    // -- Layer 8 first: cheaper, unattended, fully repeatable, and half of
    // what it finds changes what layer 7's protocols should measure.
    { 0x10, Layer::Consistency, "vs_0x11_ratio",         "",     "raw-ratio",          1000, nullptr },
    { 0x10, Layer::Consistency, "vs_0x11_ts_aligned",    "",     "raw-ratio",          1000, nullptr },
    { 0x20, Layer::Consistency, "vs_0x21_ratio",         "",     "raw-ratio",          1000, nullptr },
    { 0x20, Layer::Consistency, "vs_0x21_ts_aligned",    "",     "raw-ratio",          1000, nullptr },
    // FUSION's doc comment says accel+gyro+mag; its parser has six fields,
    // which is accel+gyro only. One of them is wrong and only measurement says
    // which.
    { 0x130, Layer::Consistency, "vs_accel_gyro_equal",  "",     "fusion-vs-parts",    1000, nullptr },
    { 0x130, Layer::Consistency, "vs_accel_gyro_rate",   "",     "fusion-vs-parts",    1000, nullptr },
    { 0x130, Layer::Consistency, "carries_magnetometer", "",     "fusion-vs-parts",    1000, nullptr },
    { 0x131, Layer::Consistency, "vs_raw_pair",          "",     "fusion-vs-parts",    1000, nullptr },
    { 0x90,  Layer::Consistency, "vs_pressure_formula_m","m",    "barometric-formula", 100,  nullptr },
    // Is PRESS_SEA_LEVEL a real input or a constant 1013.25 hPa? A whole class
    // of altitude bug turns on the answer.
    { 0x80,  Layer::Consistency, "sea_level_is_constant","",     "sea-level-watch",    100,  nullptr },
    { 0x150, Layer::Consistency, "vs_altimeter_and_gps", "",     "grade-derivation",   100,  nullptr },
    // These two should agree exactly. If they do not, every activity app on
    // the platform has a bug it does not know about.
    { 0x51,  Layer::Consistency, "vs_step_detector_count","steps","counter-vs-events", 100,  nullptr },
    { 0x53,  Layer::Consistency, "vs_step_detector_rate","spm",  "cadence-vs-events",  100,  nullptr },
    // The one automated probe with a scheduling constraint: it needs a run
    // across local midnight. It is also the only test of a contract the header
    // states and nothing verifies.
    { 0x52,  Layer::Consistency, "resets_at_local_midnight","",  "midnight-crossing",  1,    nullptr },
    { 0x52,  Layer::Consistency, "reset_value",           "",    "midnight-crossing",  1,    nullptr },
    { 0x61,  Layer::Consistency, "resets_at_local_midnight","",  "midnight-crossing",  1,    nullptr },
    { 0xE1,  Layer::Consistency, "resets_at_local_midnight","",  "midnight-crossing",  1,    nullptr },
    { 0x52,  Layer::Consistency, "survives_timezone_change","",  "timezone-change",    1,    nullptr },
    { 0x51,  Layer::Consistency, "survives_app_restart",  "",    "restart-watch",      1,    nullptr },
    { 0x51,  Layer::Consistency, "resets_on_device_reboot","",   "reboot-watch",       1,    nullptr },
    { 0x60,  Layer::Consistency, "survives_app_restart",  "",    "restart-watch",      1,    nullptr },
    { 0xE0,  Layer::Consistency, "survives_app_restart",  "",    "restart-watch",      1,    nullptr },
    // The parser's own comment says HEART_RATE and HEART_RATE_EX's arbitrated
    // field are the same value. Confirm it rather than believe it.
    { 0x41,  Layer::Consistency, "vs_hrex_arbitrated",    "bpm", "hr-vs-hrex",         1000, nullptr },
    { 0x43,  Layer::Consistency, "source_switches_to_external","","strap-arbitration",  1,    nullptr },
    { 0x43,  Layer::Consistency, "optical_reports_with_strap","", "strap-arbitration",  1,    nullptr },
    { 0x42,  Layer::Consistency, "vs_day_hr_samples",     "bpm", "daily-vs-samples",   1,    nullptr },
    // Ledger row S18: the percent gauge did not move across 8.45 h in which
    // BATTERY_METRICS' capacity fell by 10 mAh. Establish the relationship
    // properly, and settle the CURRENT sign convention the ledger flags as an
    // unverified firmware contract.
    { 0x120, Layer::Consistency, "vs_metrics_capacity_pct","%",  "gauge-vs-coulomb",   1000, nullptr },
    { 0x122, Layer::Consistency, "current_sign_convention","",   "gauge-vs-coulomb",   1000, nullptr },
    { 0x140, Layer::Consistency, "worn_vs_accel_movement", "",   "worn-vs-movement",   1000, nullptr },
    // Free, and a measurement of the RTC against the systick.
    { kPlatformScope, Layer::Consistency, "uptime_vs_wall_drift_ppm", "ppm",
      "two-clock-regression", 3600, nullptr },

    // -- Layer 7: guided, interactive, one screen per protocol --------------
    { 0x10, Layer::Physical, "bias_x_g",            "g",      "six-face-static", 3, nullptr },
    { 0x10, Layer::Physical, "bias_y_g",            "g",      "six-face-static", 3, nullptr },
    { 0x10, Layer::Physical, "bias_z_g",            "g",      "six-face-static", 3, nullptr },
    { 0x10, Layer::Physical, "scale_err_x_pct",     "%",      "six-face-static", 3, nullptr },
    { 0x10, Layer::Physical, "scale_err_y_pct",     "%",      "six-face-static", 3, nullptr },
    { 0x10, Layer::Physical, "scale_err_z_pct",     "%",      "six-face-static", 3, nullptr },
    { 0x10, Layer::Physical, "cross_axis_pct",      "%",      "six-face-static", 3, nullptr },
    { 0x10, Layer::Physical, "noise_density_mg",    "mg",     "six-face-static", 3, nullptr },
    { 0x10, Layer::Physical, "magnitude_vs_1g_pct", "%",      "six-face-static", 3, nullptr },
    // The raw-to-float scale recovers the configured full-scale range without
    // reading a register, and cross-checks layer 5's LSB estimate.
    { 0x11, Layer::Physical, "raw_to_g_scale",      "g/LSB",  "raw-vs-scaled",   3, nullptr },
    { 0x20, Layer::Physical, "zero_rate_x_dps",     "deg/s",  "still-bias",      3, nullptr },
    { 0x20, Layer::Physical, "zero_rate_y_dps",     "deg/s",  "still-bias",      3, nullptr },
    { 0x20, Layer::Physical, "zero_rate_z_dps",     "deg/s",  "still-bias",      3, nullptr },
    { 0x20, Layer::Physical, "bias_drift_dps_per_min","deg/s/min","still-bias",  3, nullptr },
    { 0x20, Layer::Physical, "rotation_err_pct",    "%",      "manual-360",      3, nullptr },
    // Layer 2 has to discover the frame before this protocol can read it.
    { 0x30, Layer::Physical, "field_magnitude_ut",  "uT",     "vs-geomag-model", 3, nullptr },
    { 0x30, Layer::Physical, "magnitude_stability_pct","%",    "figure-eight",    3, nullptr },
    { 0x80, Layer::Physical, "vs_station_qnh_pa",   "Pa",     "vs-weather-station", 3, nullptr },
    { 0x90, Layer::Physical, "vs_surveyed_point_m", "m",      "vs-surveyed-point", 3, nullptr },
    // Far more defensible than an absolute reading, which is why it is here as
    // well as the absolute one.
    { 0x90, Layer::Physical, "stairwell_diff_err_m","m",      "counted-flights", 3, nullptr },
    // Labelled ambient, never body. `AMBIENT_TEMPERATURE` is the SDK's own name
    // for it and there is no skin-temperature channel on this device.
    { 0x70, Layer::Physical, "vs_room_thermometer_c","C",     "vs-thermometer",  3, nullptr },
    { 0x51, Layer::Physical, "count_bias_pct",      "%",      "counted-100",     3, nullptr },
    { 0x51, Layer::Physical, "count_spread_pct",    "%",      "counted-100",     3, nullptr },
    { 0x51, Layer::Physical, "fp_per_min_armwave",  "1/min",  "negative-armwave",3, nullptr },
    { 0x51, Layer::Physical, "fp_per_min_vehicle",  "1/min",  "negative-vehicle",3, nullptr },
    { 0x51, Layer::Physical, "fp_per_min_handwash", "1/min",  "negative-handwash",3, nullptr },
    { 0x60, Layer::Physical, "floor_count_err",     "floors", "counted-flights", 3, nullptr },
    { 0x41, Layer::Physical, "err_rest_bpm",        "bpm",    "vs-chest-strap",  3, nullptr },
    { 0x41, Layer::Physical, "err_walk_bpm",        "bpm",    "vs-chest-strap",  3, nullptr },
    { 0x41, Layer::Physical, "err_recovery_bpm",    "bpm",    "vs-chest-strap",  3, nullptr },
    // Does the reported trust level predict the error? If not, it is
    // decoration, and every app that gates on it is gating on nothing.
    { 0x41, Layer::Physical, "trust_predicts_error","",       "vs-chest-strap",  3, nullptr },
    // Measured zero over 507 minutes on a sleeping wrist (ledger row S7).
    // Remarkable, load-bearing for SleepLab, and worth re-confirming on new
    // firmware for exactly that reason.
    { 0x140, Layer::Physical, "transitions_per_h_tight","1/h", "worn-protocol",  3, nullptr },
    { 0x140, Layer::Physical, "transitions_per_h_loose","1/h", "worn-protocol",  3, nullptr },
    { 0x140, Layer::Physical, "transitions_per_h_table","1/h", "worn-protocol",  3, nullptr },
    { 0x140, Layer::Physical, "transitions_per_h_in_hand","1/h","worn-protocol", 3, nullptr },
    { 0xC0, Layer::Physical, "confusion_accuracy_pct","%",    "labelled-activity",3, nullptr },
    { 0xB0, Layer::Physical, "confusion_accuracy_pct","%",    "labelled-activity",3, nullptr },
    { 0xA0, Layer::Physical, "detect_rate_pct",     "%",      "twenty-raises",   3, nullptr },
    { 0xA0, Layer::Physical, "detect_latency_ms",   "ms",     "twenty-raises",   3, nullptr },
    // Undocumented anywhere. Whether it produces anything at all is the whole
    // question, and if it does, which gestures trigger it is the next one.
    { 0xD0, Layer::Physical, "produces_anything",   "",       "gesture-hunt",    1, nullptr },
    { 0x110, Layer::Physical, "cep50_m",            "m",      "open-sky-soak",   3, nullptr },
    { 0x110, Layer::Physical, "cep95_m",            "m",      "open-sky-soak",   3, nullptr },
    // An error estimate nobody has checked is decoration.
    { 0x110, Layer::Physical, "precision_vs_scatter","",      "open-sky-soak",   3, nullptr },
    { 0x110, Layer::Physical, "ttff_cold_s",        "s",      "ttff",            3, nullptr },
    { 0x110, Layer::Physical, "ttff_warm_s",        "s",      "ttff",            3, nullptr },
    { 0x110, Layer::Physical, "ttff_hot_s",         "s",      "ttff",            3, nullptr },
    { 0x110, Layer::Physical, "reports_indoor_fix", "",       "indoor-check",    3, nullptr },
    { 0x112, Layer::Physical, "loop_err_pct",       "%",      "closed-loop",     3, nullptr },
    { 0x112, Layer::Physical, "vs_haversine_pct",   "%",      "closed-loop",     3, nullptr },
    { 0x111, Layer::Physical, "speed_err_pct",      "%",      "timed-distance",  3, nullptr },

    // -- Existence-only, re-run every firmware version ---------------------
    //
    // Four types whose whole protocol is "does this exist yet". Each carries
    // its reason, and each is INAPPLICABLE only while layer 1 says no driver
    // resolves -- a firmware update that resolves one turns the row back on
    // automatically, which is the point of recording the reason rather than
    // deleting the row.
    { 0xF1, Layer::Physical, "driver_exists", "", "existence-recheck", 1,
      "SPO2 resolves no driver on this firmware (ledger row S4), so there is "
      "nothing to compare against a reference. Re-checked every run." },
    { 0x40, Layer::Physical, "driver_exists", "", "existence-recheck", 1,
      "HEART_BEAT resolves no driver (ledger row S5); UNA state HR detection is "
      "frequency-domain rather than per-beat (PR #167). Re-checked every run." },
    { 0xF0, Layer::Physical, "driver_exists", "", "existence-recheck", 1,
      "PPG has never been observed to resolve a driver on hardware. Whether the "
      "raw waveform is available to apps at all is the open question." },
    { 0x100, Layer::Physical, "driver_exists", "", "existence-recheck", 1,
      "ECG on a device with no electrodes is expected absent. Confirmed rather "
      "than assumed, so the report can say so instead of leaving a hole." },
};

const size_t kBespokeCount = sizeof(kBespoke) / sizeof(kBespoke[0]);

// ---------------------------------------------------------------------------

size_t claimCount()
{
    return kBlockATotal + kBlockBTotal + kBespokeCount;
}

const char *layerName(Layer layer)
{
    const size_t i = static_cast<size_t>(layer);
    return (i >= 1 && i <= kLayerCount) ? kLayerNames[i - 1] : "?";
}

const char *layerMethodPrefix(Layer layer)
{
    const size_t i = static_cast<size_t>(layer);
    return (i >= 1 && i <= kLayerCount) ? kLayerPrefixes[i - 1] : "P?";
}

Layer metricLayer(Metric m)
{
    return kMetrics[static_cast<size_t>(m)].layer;
}

const char *metricName(Metric m)
{
    return kMetrics[static_cast<size_t>(m)].name;
}

const char *metricUnit(Metric m)
{
    return kMetrics[static_cast<size_t>(m)].unit;
}

bool metricIsDistribution(Metric m)
{
    return kMetrics[static_cast<size_t>(m)].distribution;
}

uint32_t metricMinimumN(Metric m)
{
    return kMetrics[static_cast<size_t>(m)].minimumN;
}

const char *fieldMetricName(FieldMetric m)
{
    return kFieldMetricSpecs[static_cast<size_t>(m)].name;
}

const char *fieldMetricUnit(FieldMetric m)
{
    return kFieldMetricSpecs[static_cast<size_t>(m)].unit;
}

uint32_t fieldMetricMinimumN(FieldMetric m)
{
    return kFieldMetricSpecs[static_cast<size_t>(m)].minimumN;
}

size_t typeIndexOf(uint32_t typeValue)
{
    for (size_t i = 0; i < kTypeCount; i++) {
        if (kTypes[i].value == typeValue) {
            return i;
        }
    }
    return kTypeCount;
}

ClaimRef describe(size_t claimIdx)
{
    ClaimRef r {};

    if (claimIdx < kBlockATotal) {
        r.kind    = ClaimRef::Kind::PerType;
        r.typeIdx = claimIdx / kPerTypeMetrics;
        r.metric  = static_cast<Metric>(claimIdx % kPerTypeMetrics);
        r.layer   = metricLayer(r.metric);
        return r;
    }

    if (claimIdx < kBlockATotal + kBlockBTotal) {
        const size_t rel  = claimIdx - kBlockATotal;
        const size_t slot = rel / kFieldMetrics;
        r.kind        = ClaimRef::Kind::PerField;
        r.fieldMetric = static_cast<FieldMetric>(rel % kFieldMetrics);
        r.layer       = Layer::Value;
        // Walk the prefix sums. 37 types, and this is called once per row by
        // the writer and the roster -- never on the sample path.
        size_t acc = 0;
        for (size_t t = 0; t < kTypeCount; t++) {
            const size_t n = fieldSlots(t);
            if (slot < acc + n) {
                r.typeIdx  = t;
                r.fieldIdx = static_cast<uint8_t>(slot - acc);
                return r;
            }
            acc += n;
        }
        // Unreachable: kBlockBTotal is the sum of the same slot counts.
        r.typeIdx  = kTypeCount;
        r.fieldIdx = 0;
        return r;
    }

    r.kind       = ClaimRef::Kind::Bespoke;
    r.bespokeIdx = claimIdx - kBlockATotal - kBlockBTotal;
    r.layer      = kBespoke[r.bespokeIdx].layer;
    r.typeIdx    = (kBespoke[r.bespokeIdx].scope == kPlatformScope)
                       ? kTypeCount
                       : typeIndexOf(kBespoke[r.bespokeIdx].scope);
    return r;
}

namespace
{

/// The `<scope>` half of a claim_id.
int writeScope(char *out, size_t outSize, const ClaimRef &r)
{
    if (r.typeIdx >= kTypeCount) {
        return std::snprintf(out, outSize, "platform");
    }
    // Lower-case hex, no padding: the value, not the enumerator name, because a
    // type renamed upstream keeps its value and the value is what the kernel
    // dispatches on.
    return std::snprintf(out, outSize, "0x%x",
                         static_cast<unsigned>(kTypes[r.typeIdx].value));
}

} // namespace

size_t formatClaimId(char *out, size_t outSize, size_t claimIdx)
{
    if (out == nullptr || outSize == 0) {
        return 0;
    }
    out[0] = '\0';
    if (claimIdx >= claimCount()) {
        return 0;
    }

    const ClaimRef r = describe(claimIdx);

    char scope[16];
    if (writeScope(scope, sizeof(scope), r) <= 0) {
        return 0;
    }

    int n = 0;
    switch (r.kind) {
        case ClaimRef::Kind::PerType:
            n = std::snprintf(out, outSize, "%s.%s.%s", scope,
                              layerName(r.layer), metricName(r.metric));
            break;
        case ClaimRef::Kind::PerField:
            n = std::snprintf(out, outSize, "%s.%s.f%u_%s", scope,
                              layerName(r.layer),
                              static_cast<unsigned>(r.fieldIdx),
                              fieldMetricName(r.fieldMetric));
            break;
        case ClaimRef::Kind::Bespoke:
            n = std::snprintf(out, outSize, "%s.%s.%s", scope,
                              layerName(r.layer),
                              kBespoke[r.bespokeIdx].metric);
            break;
    }

    if (n <= 0 || static_cast<size_t>(n) >= outSize) {
        out[0] = '\0';
        return 0;
    }
    return static_cast<size_t>(n);
}

size_t formatMethodId(char *out, size_t outSize, size_t claimIdx)
{
    if (out == nullptr || outSize == 0) {
        return 0;
    }
    out[0] = '\0';
    if (claimIdx >= claimCount()) {
        return 0;
    }

    const ClaimRef r = describe(claimIdx);

    const char *method = "?";
    switch (r.kind) {
        case ClaimRef::Kind::PerType:
            method = kMetrics[static_cast<size_t>(r.metric)].method;
            break;
        case ClaimRef::Kind::PerField:
            method = kFieldMetricSpecs[static_cast<size_t>(r.fieldMetric)].method;
            break;
        case ClaimRef::Kind::Bespoke:
            method = kBespoke[r.bespokeIdx].method;
            break;
    }

    const int n = std::snprintf(out, outSize, "%s.%s",
                                layerMethodPrefix(r.layer), method);
    if (n <= 0 || static_cast<size_t>(n) >= outSize) {
        out[0] = '\0';
        return 0;
    }
    return static_cast<size_t>(n);
}

Expectation expectationFor(size_t claimIdx)
{
    Expectation e { false, 0.0f, "" };
    if (claimIdx >= claimCount()) {
        return e;
    }

    const ClaimRef r = describe(claimIdx);
    if (r.kind != ClaimRef::Kind::PerType || r.typeIdx >= kTypeCount) {
        return e;
    }

    const TypeSpec &t = kTypes[r.typeIdx];
    if (t.parser == kNoParser) {
        // Five types with no parser: the SDK makes no claim about the frame at
        // all, so `conformance` is NO_CLAIM and a measured layout is the only
        // description that exists anywhere.
        return e;
    }

    const ParserSpec &p = kParsers[t.parser];

    if (r.metric == Metric::FieldCount) {
        e.hasValue = true;
        e.value    = static_cast<float>(p.fieldCount);
        e.source   = p.header;
    }

    return e;
}

} // namespace SensorLab::Catalogue
