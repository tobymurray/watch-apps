/**
 ******************************************************************************
 * @file    ProfileWriter.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   profile.json: the manifest, then every claim row that has an answer.
 ******************************************************************************
 *
 * NORMATIVE FORMAT. `Tools/profile_report.py` and `Tools/profile_diff.py` parse
 * exactly what is described here, so this comment and those scripts are the
 * halves of one contract. The round-trip is a ctest, not a promise.
 *
 * ---------------------------------------------------------------------------
 * Shape
 *
 * {
 *   "schema": 1,
 *   "manifest": { ... },              -- the primary key; see Manifest.hpp
 *   "completeness": {
 *     "overall": { "applicable": n, "answered": n, "percent": n, ... },
 *     "by_layer": [ { "layer": "timing", ... }, ... ]
 *   },
 *   "sensors": [
 *     { "type": "0x10", "name": "ACCELEROMETER",
 *       "descriptor": "...",          -- RequestGetDesc, when one was read
 *       "parser": "Accelerometer",    -- or null for the five with none
 *       "missing_from_doc": false,
 *       "completeness": { ... },
 *       "claims": [ { ... }, ... ] }
 *   ],
 *   "platform_claims": [ { ... } ]
 * }
 *
 * A claim row, which is §1.2 of the implementation prompt verbatim:
 *
 * { "claim_id": "0x10.timing.dt_ms", "layer": "timing", "metric": "dt_ms",
 *   "verdict": "CONFIRMED", "method_id": "P4.dt-histogram",
 *   "n": 148231, "minimum_n": 10000,
 *   "value": "20.4", "unit": "ms",
 *   "spread": { "p05": "19", "p50": "20.4", "p95": "22.1" },
 *   "run_id": 7,
 *   "observed_at": { "uptime_ms": 12345678, "wall_utc": "1755553500" },
 *   "expected": null, "expected_source": null,
 *   "conformance": "NO_CLAIM", "inferred": false, "notes": null }
 *
 * ---------------------------------------------------------------------------
 * Every number is a decimal string, and this is not gratuitous
 *
 * `"value": "20.4"`, `"n": 148231`. Counts are JSON numbers; anything that could
 * be fractional, negative or wider than 32 bits is a **string**, and `float()`
 * in python reads it either way.
 *
 * Three reasons, none of them a preference:
 *
 * **The watch's newlib may not link floating-point `printf`.** When it does not,
 * `%f` and `%g` emit nothing *at runtime* rather than failing at link time -- on
 * hardware, in a file nobody reads until the next morning.
 * `SDK::JsonStreamWriter::add(key, float)` routes through `snprintf("%g")`, so
 * this writer does not use it, and no float reaches a formatter anywhere on this
 * device. `Profile::format` does the conversion with integer arithmetic.
 *
 * **`SDK::JsonStreamWriter::add(int32_t)` writes garbage for negative values on
 * any 64-bit build.** It formats an `int32_t` with `%ld`, which expects eight
 * bytes and gets four: -5 came out as 4294967291. ARM is unaffected, because
 * `long` is four bytes there -- which is exactly why the defect survives as far
 * as the host tests. Measured here, not read.
 *
 * **`add(int64_t)` and `add(uint64_t)` route through `double` and `%g`.** Six
 * significant digits, so a UNIX timestamp loses its seconds: 1755553500 would
 * be written as 1.75555e+09.
 *
 * So the only SDK number path this app trusts is `add(uint32_t)`. Both defects
 * are in `Docs/FINDINGS.md` with a minimal reproduction.
 *
 * The upside of the string form is that `profile.json` is readable by eye, and
 * `profile_report.py` renders `SENSOR-PROFILE.md` from it for anyone who wants
 * prose.
 *
 * ---------------------------------------------------------------------------
 * Every claim is written, including the unanswered ones
 *
 * This is the decision that makes the document honest, and it is worth stating
 * plainly because the opposite is so tempting. A profile that wrote only the
 * rows it had answers for would be a third of the size and would read as
 * finished. Writing the UNVERIFIED rows -- each naming the probe that would
 * settle it -- turns the file into its own to-do list, and makes
 * `profile_diff.py`'s "appeared" case mean *a claim was added to the
 * catalogue*, not *somebody finally measured it*.
 *
 * The cost is file size: ~1 974 claims at roughly 220 bytes is about 430 KB.
 * `MapManager` CRC-verified 160.5 MiB of map packs on this volume, so the space
 * is not in question -- but the *write* is, at one open-seek-write-flush-close,
 * so the writer streams straight into the file handle through
 * `SDK::JSON::JsonStreamWriter` rather than building a buffer.
 *
 * ---------------------------------------------------------------------------
 * Rewritten whole, and why that is safe here
 *
 * Unlike the run log, `profile.json` is rewritten from scratch each time. It is
 * a *derived* document: every fact in it is in the claim store, which is in RAM
 * and was itself rebuilt from the runs. Losing it to a cable at the wrong
 * moment loses nothing that the run logs and the next write do not restore. The
 * append-only file with the seek discipline is `runs/<id>.csv`, which holds the
 * raw samples, which is the thing that cannot be regenerated.
 *
 * The write goes to a temporary name and is renamed over the target, so a
 * profile interrupted halfway is the *previous* profile rather than a truncated
 * one. `IFileSystem::rename` is available and this is what it is for.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_PROFILEWRITER_HPP
#define SENSORLAB_PROFILEWRITER_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "Evidence/ClaimStore.hpp"
#include "Profile/Manifest.hpp"

namespace SensorLab::Profile
{

/// Bumped when a key is added, removed or reinterpreted. The host tools refuse
/// a schema they do not know rather than reading a renamed key as absent.
constexpr uint32_t kProfileSchema = 1;

/// Temporary name the profile is written to before being renamed into place.
constexpr char kProfileTempPath[] = "profile.tmp";

/**
 * @brief Writes `profile-<firmware>.json` from a claim store.
 *
 * Stateless apart from the kernel reference and a failure counter: everything
 * it writes comes from the store and the manifest handed to `write()`. That is
 * what lets the whole format be exercised in a host test against
 * `InMemoryFileSystem`, with the real writer, and then handed to the real
 * python script -- see `Tests/README.md`.
 */
class ProfileWriter
{
public:
    explicit ProfileWriter(const SDK::Kernel &kernel);

    /**
     * @brief Write the profile.
     *
     * @param store    Every claim, answered or not.
     * @param manifest The run that most recently contributed to it.
     * @return true when the file reached storage and was renamed into place.
     */
    bool write(const Evidence::ClaimStore &store, const RunManifest &manifest);

    /// The path the last successful `write()` produced, for the log and the
    /// screen. Empty before the first one.
    const char *lastPath() const { return mLastPath; }

    uint32_t failures() const { return mFailures; }

private:
    const SDK::Kernel &mKernel;
    char               mLastPath[32] {};
    uint32_t           mFailures = 0;
};

/// Write one run's manifest to `runs/<run_id>.json`.
///
/// Called twice per run -- once when it opens with `end == InProgress`, once
/// when it closes. A run terminated by the cable would otherwise have no
/// manifest at all, and a raw log with no manifest is a file nobody can
/// interpret.
bool writeRunManifest(const SDK::Kernel &kernel, const RunManifest &manifest);

} // namespace SensorLab::Profile

#endif // SENSORLAB_PROFILEWRITER_HPP
