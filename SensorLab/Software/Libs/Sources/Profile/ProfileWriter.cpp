/**
 ******************************************************************************
 * @file    ProfileWriter.cpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   profile.json. NORMATIVE FORMAT is in the header.
 ******************************************************************************
 */

#include "Profile/ProfileWriter.hpp"

#include <cstdio>
#include <memory>

#include "SDK/JSON/JsonStreamWriter.hpp"

#include "Profile/Decimal.hpp"

#define LOG_MODULE_PRX      "Profile"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace SensorLab::Profile
{

namespace
{

using Writer = SDK::JsonStreamWriter;

/// Write a number as a decimal **string**, formatted without any printf.
///
/// Two independent reasons, and both are defects in things this app does not
/// control:
///
/// **1. The watch's newlib may not link floating-point `printf`.** When it does
/// not, `%f` and `%g` emit nothing *at runtime* rather than failing at link
/// time -- on hardware, in a file nobody reads until the next morning.
/// `SDK::JsonStreamWriter::add(key, float)` routes through `snprintf("%g")`, so
/// it is not used here, and no float reaches a formatter anywhere on this
/// device. Every other app in this repository solves the same problem by
/// scaling to a fixed integer (SleepLab's `count_scale_x1e6`); a profiler
/// cannot pick one scale, because it reports station pressure near 101 325 Pa
/// and a recovered accelerometer LSB near 0.000 061 g in the same document.
///
/// **2. The SDK's integer paths are worse than its float path.** Both of these
/// were measured while writing this file, on the host build:
///
///   - `add(int32_t)` -> `writeInt()` -> `snprintf("%ld", value)` where `value`
///     is an `int32_t`. On any 64-bit build `%ld` expects eight bytes and gets
///     four, so **a negative value comes out as its unsigned reinterpretation**:
///     -5 was written as 4294967291. ARM is unaffected, because `long` is four
///     bytes there -- which is exactly why it survives as far as the host tests.
///   - `add(int64_t)` and `add(uint64_t)` -> `add(static_cast<double>(value))`
///     -> `%g`. Six significant digits, so **a UNIX timestamp loses its
///     seconds**: 1755553500 would be written as 1.75555e+09. And it inherits
///     the float-printf risk above.
///
/// So the only SDK number path this app trusts is `add(uint32_t)`. Anything
/// wider or signed is written as a string, by `Profile::format` or by an integer
/// `snprintf` with a correctly-sized cast. See Docs/FINDINGS.md.
///
/// The cost is that values are JSON strings rather than JSON numbers. `float()`
/// in python reads them either way, and the upside is that `profile.json`
/// becomes readable by eye -- which the mantissa/exponent pairs this replaced
/// were not.
void addNumber(Writer &w, const char *key, float v)
{
    char buf[kDecimalStringMax];
    if (format(buf, sizeof(buf), v) == 0) {
        w.addNull(key);
        return;
    }
    w.add(key, buf);
}

/// A signed 64-bit integer as a string. `%lld` against a `long long` is
/// correctly sized on every target here, and it is an integer conversion, so it
/// is always linked.
void addI64(Writer &w, const char *key, int64_t v)
{
    char      buf[24];
    const int n = std::snprintf(buf, sizeof(buf), "%lld",
                                static_cast<long long>(v));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf)) {
        w.addNull(key);
        return;
    }
    w.add(key, buf);
}

void addU64(Writer &w, const char *key, uint64_t v)
{
    char      buf[24];
    const int n = std::snprintf(buf, sizeof(buf), "%llu",
                                static_cast<unsigned long long>(v));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf)) {
        w.addNull(key);
        return;
    }
    w.add(key, buf);
}

void addCompleteness(Writer &w, const char *key,
                     const Evidence::Completeness &c)
{
    Writer::KeyedMapScope scope(w, key);
    w.add("applicable",   static_cast<uint32_t>(c.applicable));
    w.add("answered",     static_cast<uint32_t>(c.answered));
    w.add("confirmed",    static_cast<uint32_t>(c.confirmed));
    w.add("likely",       static_cast<uint32_t>(c.likely));
    w.add("refuted",      static_cast<uint32_t>(c.refuted));
    w.add("inapplicable", static_cast<uint32_t>(c.inapplicable));
    w.add("percent",      static_cast<uint32_t>(c.percent()));
}

void addManifest(Writer &w, const RunManifest &m)
{
    Writer::KeyedMapScope scope(w, "manifest");
    w.add("run_id", m.runId);

    // The primary key, and its provenance. `firmware_read_from_kernel` false
    // means the string came from settings.json or is empty -- usable, but not
    // evidence, and the report says which it has rather than presenting a
    // declared version as a read one.
    w.add("firmware", m.firmware);
    w.add("hardware", m.hardware);
    w.add("firmware_read_from_kernel", m.haveSystemInfo);

    w.add("kernel_interface_version", m.kernelInterfaceVersion);
    w.add("app_version",       m.appVersion);
    w.add("catalogue_version", m.catalogueVersion);
    w.add("type_table_version", m.typeTableVersion);
    w.add("sdk_tag",           m.sdkTag);

    // What else was going on while the measurements were taken. A dt
    // distribution measured alongside eight other streams is a different
    // measurement from one taken alone.
    addU64(w, "types_asked_mask", m.typesAsked);
    addU64(w, "types_resolved_mask", m.typesResolved);
    addU64(w, "types_delivered_mask", m.typesDelivered);
    addNumber(w, "requested_period_ms", m.requestedPeriodMs);
    w.add("requested_latency_ms", m.requestedLatencyMs);
    w.add("gui_attached",         m.guiAttached);
    w.add("saw_charging",         m.sawCharging);

    {
        Writer::KeyedMapScope started(w, "started");
        w.add("uptime_ms", m.started.uptimeMs);
        addI64(w, "wall_utc", m.started.wallUtc);
    }
    {
        Writer::KeyedMapScope ended(w, "ended");
        w.add("uptime_ms", m.ended.uptimeMs);
        addI64(w, "wall_utc", m.ended.wallUtc);
    }
    // Uptime, never the wall-clock difference: the wall clock can jump on a
    // timezone change, a host sync or DST, and a jump would silently rewrite
    // the run's length.
    w.add("duration_ms", m.durationMs());
    w.add("end",         toString(m.end));

    w.add("rows_written",  m.rowsWritten);
    w.add("row_failures",  m.rowFailures);
    addU64(w, "bytes_written", m.bytesWritten);
}

/// One claim row: §1.2 of the implementation prompt, verbatim.
void addClaim(Writer &w, const Evidence::ClaimStore &store, size_t claimIdx)
{
    const Evidence::Claim &c = store.at(claimIdx);

    char claimId[Catalogue::kClaimIdMax];
    char methodId[Catalogue::kMethodIdMax];
    Catalogue::formatClaimId(claimId, sizeof(claimId), claimIdx);
    Catalogue::formatMethodId(methodId, sizeof(methodId), claimIdx);

    const Catalogue::ClaimRef r = Catalogue::describe(claimIdx);

    const char *metric = "?";
    const char *unit   = "";
    uint32_t    minN   = 1;
    switch (r.kind) {
        case Catalogue::ClaimRef::Kind::PerType:
            metric = Catalogue::metricName(r.metric);
            unit   = Catalogue::metricUnit(r.metric);
            minN   = Catalogue::metricMinimumN(r.metric);
            break;
        case Catalogue::ClaimRef::Kind::PerField:
            metric = Catalogue::fieldMetricName(r.fieldMetric);
            unit   = Catalogue::fieldMetricUnit(r.fieldMetric);
            minN   = Catalogue::fieldMetricMinimumN(r.fieldMetric);
            break;
        case Catalogue::ClaimRef::Kind::Bespoke:
            metric = Catalogue::kBespoke[r.bespokeIdx].metric;
            unit   = Catalogue::kBespoke[r.bespokeIdx].unit;
            minN   = Catalogue::kBespoke[r.bespokeIdx].minimumN;
            break;
    }

    Writer::MapScope scope(w);
    w.add("claim_id",  claimId);
    w.add("layer",     Catalogue::layerName(r.layer));
    w.add("metric",    metric);
    w.add("verdict",   Evidence::toString(c.verdict));
    w.add("method_id", methodId);

    // No number without an n, and no n without the minimum it is being judged
    // against: a reader has to be able to see *why* a row is still UNVERIFIED.
    w.add("n",         c.n);
    w.add("minimum_n", minN);

    if (c.hasValue()) {
        addNumber(w, "value", c.value);
        w.add("unit",  unit);
    } else {
        // Explicitly null rather than absent. A key that is sometimes missing
        // makes every consumer write the same defensive branch, and one of them
        // will get it wrong.
        w.addNull("value");
        w.add("unit", unit);
    }

    const Evidence::Spread *s = store.spreadFor(claimIdx);
    if (s != nullptr) {
        Writer::KeyedMapScope spread(w, "spread");
        addNumber(w, "p05", s->p05);
        addNumber(w, "p50", s->p50);
        addNumber(w, "p95", s->p95);
    } else {
        w.addNull("spread");
    }

    w.add("run_id", static_cast<uint32_t>(c.runId));
    {
        // Both clocks, always. Uptime is what durations come from; wall clock is
        // what a time of day is read from.
        Writer::KeyedMapScope at(w, "observed_at");
        w.add("uptime_ms", c.uptimeMs);
        addI64(w, "wall_utc", c.wallUtc);
    }

    // Quote the spec or make no claim. `expected_source` is a file, a doc
    // section or a datasheet section -- never a paraphrase.
    const Catalogue::Expectation e = Catalogue::expectationFor(claimIdx);
    if (e.hasValue) {
        addNumber(w, "expected", e.value);
        w.add("expected_source", e.source);
    } else {
        w.addNull("expected");
        w.addNull("expected_source");
    }
    w.add("conformance", Evidence::toString(c.conformance));

    // True when the row came from reading a spec or from deriving it, rather
    // than from the device. Carried so a reader can tell a measurement from an
    // inference at a glance.
    w.add("inferred", c.inferred());

    const char *note = Evidence::toString(c.note);
    if (note != nullptr && note[0] != '\0') {
        w.add("notes", note);
    } else {
        w.addNull("notes");
    }

    // An INAPPLICABLE claim carries the reason the catalogue gave, which is the
    // thing that lets a firmware update turn the row back on: the reason names
    // the condition, and layer 1 re-checks the condition every run.
    if (r.kind == Catalogue::ClaimRef::Kind::Bespoke
        && Catalogue::kBespoke[r.bespokeIdx].inapplicableReason != nullptr) {
        w.add("inapplicable_reason",
              Catalogue::kBespoke[r.bespokeIdx].inapplicableReason);
    }
}

} // namespace

ProfileWriter::ProfileWriter(const SDK::Kernel &kernel)
    : mKernel(kernel)
{
}

bool ProfileWriter::write(const Evidence::ClaimStore &store,
                          const RunManifest &manifest)
{
    char target[32];
    if (profileFileName(target, sizeof(target), manifest) == 0) {
        mFailures++;
        return false;
    }

    // Written to a temporary and renamed over the target, so a profile
    // interrupted by the cable is the *previous* profile rather than a
    // truncated one. The append-only file with the seek discipline is the run
    // log, which holds what cannot be regenerated.
    {
        std::unique_ptr<SDK::Interface::IFile> file =
            mKernel.fs.file(kProfileTempPath);
        if (!file || !file->open(true, true)) {
            LOG_WARNING("cannot open %s for writing\n", kProfileTempPath);
            mFailures++;
            return false;
        }

        SDK::JsonStreamWriter w(file.get());

        {
            Writer::MapScope root(w);
            w.add("schema", kProfileSchema);
            addManifest(w, manifest);

            {
                Writer::KeyedMapScope completeness(w, "completeness");
                addCompleteness(w, "overall", store.completenessOverall());
                {
                    Writer::KeyedArrayScope byLayer(w, "by_layer");
                    for (size_t i = 1; i <= Catalogue::kLayerCount; i++) {
                        const auto layer = static_cast<Catalogue::Layer>(i);
                        Writer::MapScope row(w);
                        w.add("layer", Catalogue::layerName(layer));
                        addCompleteness(w, "completeness",
                                        store.completenessForLayer(layer));
                    }
                }
                // Spread allocations that were dropped because the side table
                // filled up. Non-zero means a distribution lost its quantiles,
                // and the profile says so rather than reporting a rate with no
                // spread as though none existed.
                w.add("spreads_used",
                      static_cast<uint32_t>(store.spreadsUsed()));
                w.add("spreads_dropped", store.spreadsDropped());
            }

            {
                Writer::KeyedArrayScope sensors(w, "sensors");
                for (size_t t = 0; t < Catalogue::kTypeCount; t++) {
                    const Catalogue::TypeSpec &spec = Catalogue::kTypes[t];

                    Writer::MapScope sensor(w);
                    char typeHex[12];
                    std::snprintf(typeHex, sizeof(typeHex), "0x%x",
                                  static_cast<unsigned>(spec.value));
                    w.add("type", typeHex);
                    w.add("name", spec.name);
                    w.add("doc",  spec.doc);

                    // `RequestGetDesc`'s answer: the kernel naming its own
                    // driver, and the closest thing to an authoritative part
                    // identification an app can obtain.
                    const char *desc = store.descriptor(t);
                    if (desc != nullptr && desc[0] != '\0') {
                        w.add("descriptor", desc);
                    } else {
                        w.addNull("descriptor");
                    }

                    if (spec.parser == Catalogue::kNoParser) {
                        // One of the five types the SDK ships no parser for. A
                        // measured layout is the only description of the frame
                        // that exists anywhere, so the report flags these
                        // specially.
                        w.addNull("parser");
                        w.addNull("parser_field_count");
                        w.addNull("parser_validity");
                        w.add("parser_reads_before_count", false);
                    } else {
                        const Catalogue::ParserSpec &p =
                            Catalogue::kParsers[spec.parser];
                        w.add("parser", p.cls);
                        w.add("parser_header", p.header);
                        w.add("parser_field_count",
                              static_cast<uint32_t>(p.fieldCount));
                        // 28 of 29 parsers are exact, so one appended field
                        // silently invalidates every sample. HeartRateEx is the
                        // exception. That asymmetry is a conformance finding.
                        w.add("parser_validity",
                              p.validity == Catalogue::Validity::AtLeast
                                  ? "at_least" : "exact");
                        w.add("parser_range_checked", p.rangeChecked);
                        // GpsLocation reads a field before checking the count:
                        // on a short frame that is an out-of-bounds read,
                        // because DataView's bounds assert is compiled out at
                        // -Os. The profiler is the first thing that will ever
                        // meet a short frame.
                        w.add("parser_reads_before_count", p.readsBeforeCount);

                        Writer::KeyedArrayScope fields(w, "parser_fields");
                        for (uint8_t f = 0; f < p.fieldCount; f++) {
                            Writer::MapScope field(w);
                            w.add("index", static_cast<uint32_t>(f));
                            w.add("name",  p.fields[f].name);
                            w.add("kind",
                                  p.fields[f].kind == Catalogue::FieldKind::Float ? "float"
                                  : p.fields[f].kind == Catalogue::FieldKind::U32 ? "u32"
                                  : p.fields[f].kind == Catalogue::FieldKind::I32 ? "i32"
                                                                                 : "unread");
                            w.add("doc", p.fields[f].doc);
                        }
                    }

                    // Finding number one, per row: the SDK's own documentation
                    // does not mention this type at all.
                    w.add("missing_from_doc", spec.missingFromDoc);

                    addCompleteness(w, "completeness",
                                    store.completenessForType(t));

                    Writer::KeyedArrayScope claims(w, "claims");
                    const size_t total = Catalogue::claimCount();
                    for (size_t i = 0; i < total; i++) {
                        if (Catalogue::describe(i).typeIdx == t) {
                            addClaim(w, store, i);
                        }
                    }
                }
            }

            {
                Writer::KeyedArrayScope platform(w, "platform_claims");
                const size_t total = Catalogue::claimCount();
                for (size_t i = 0; i < total; i++) {
                    if (Catalogue::describe(i).typeIdx >= Catalogue::kTypeCount) {
                        addClaim(w, store, i);
                    }
                }
            }
        }

        w.flush();
        const bool jsonOk = !w.isError();
        file->flush();
        file->close();

        if (!jsonOk) {
            LOG_WARNING("JSON writer reported an error; %s not renamed\n",
                        kProfileTempPath);
            mFailures++;
            return false;
        }
    }

    // FatFs rename fails when the target exists, so remove first. The window
    // between the two is the reason for the temporary file rather than a
    // reason against it: if the app dies in that window the profile is missing
    // and `profile.tmp` is complete, which is recoverable. Writing in place
    // would leave a half-written profile, which is not.
    if (mKernel.fs.exist(target)) {
        mKernel.fs.remove(target);
    }
    if (!mKernel.fs.rename(kProfileTempPath, target)) {
        LOG_WARNING("cannot rename %s to %s\n", kProfileTempPath, target);
        mFailures++;
        return false;
    }

    std::snprintf(mLastPath, sizeof(mLastPath), "%s", target);
    LOG_INFO("wrote %s\n", mLastPath);
    return true;
}

bool writeRunManifest(const SDK::Kernel &kernel, const RunManifest &manifest)
{
    char path[32];
    if (runManifestFileName(path, sizeof(path), manifest.runId) == 0) {
        return false;
    }

    std::unique_ptr<SDK::Interface::IFile> file = kernel.fs.file(path);
    if (!file || !file->open(true, true)) {
        LOG_WARNING("cannot open %s\n", path);
        return false;
    }

    SDK::JsonStreamWriter w(file.get());
    {
        Writer::MapScope root(w);
        w.add("schema", kProfileSchema);
        addManifest(w, manifest);
    }
    w.flush();
    const bool ok = !w.isError();
    file->flush();
    file->close();
    return ok;
}

} // namespace SensorLab::Profile
