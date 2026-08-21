/**
 ******************************************************************************
 * @file    Settings.cpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   settings.json. Rationale is in the header.
 ******************************************************************************
 */

#include "Settings.hpp"

#include <cstring>
#include <memory>

#include "SDK/JSON/JsonStreamReader.hpp"

#include "Catalogue/Catalogue.hpp"

#define LOG_MODULE_PRX      "Settings"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace SensorLab
{

namespace
{

/// Whether @p query is present but blank.
///
/// A blank field means "leave the default alone", and it is the normal case
/// rather than a mistake: Kira's install page writes every field the registry
/// declares, so a form somebody filled in one box of arrives with the rest as
/// empty strings. coreJSON reads `""` as the number 0, so without this a blank
/// box would silently *set* any field whose valid range includes zero --
/// `latency_ms` blank would disable batching -- and would look healthy doing it.
bool isBlank(const SDK::JsonStreamReader &json, const char *query)
{
    std::string_view sv;
    return json.get(query, sv) && sv.empty();
}

/// Read a bounded unsigned, leaving @p out alone if absent, blank or outside
/// [lo, hi]. Refused rather than clamped -- see the header.
template <typename T>
void readBounded(const SDK::JsonStreamReader &json, const char *query,
                 uint32_t lo, uint32_t hi, T &out)
{
    if (isBlank(json, query)) {
        return;
    }
    uint32_t raw = 0;
    if (!json.get(query, raw)) {
        return;
    }
    if (raw < lo || raw > hi) {
        LOG_WARNING("%s = %lu outside [%lu, %lu], keeping %lu\n", query,
                    static_cast<unsigned long>(raw),
                    static_cast<unsigned long>(lo),
                    static_cast<unsigned long>(hi),
                    static_cast<unsigned long>(out));
        return;
    }
    out = static_cast<T>(raw);
}

/// Read a flag written the way a person writes one.
///
/// The accepted vocabulary matches Squash's `record_imu` and SleepLab's
/// `diagnostics` deliberately: these files are typed by hand, and a flag that
/// only accepts one spelling of "yes" is a flag that will be got wrong.
void readFlag(const SDK::JsonStreamReader &json, const char *query, bool &out)
{
    std::string_view sv;
    if (!json.get(query, sv) || sv.empty()) {
        return;
    }

    char         lower[16] = {};
    const size_t n = sv.size() < sizeof(lower) - 1 ? sv.size() : sizeof(lower) - 1;
    for (size_t i = 0; i < n; i++) {
        const char c = sv[i];
        lower[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    if (std::strcmp(lower, "on")   == 0 || std::strcmp(lower, "yes")  == 0 ||
        std::strcmp(lower, "true") == 0 || std::strcmp(lower, "1")    == 0 ||
        std::strcmp(lower, "enabled") == 0) {
        out = true;
        return;
    }
    if (std::strcmp(lower, "off")   == 0 || std::strcmp(lower, "no")    == 0 ||
        std::strcmp(lower, "false") == 0 || std::strcmp(lower, "0")     == 0 ||
        std::strcmp(lower, "disabled") == 0) {
        out = false;
        return;
    }

    LOG_WARNING("%s = '%.*s' is neither on nor off, keeping %s\n", query,
                static_cast<int>(sv.size()), sv.data(), out ? "on" : "off");
}

/// Read a sensor type written as hex (`"0x10"`) or as its enumerator name
/// (`"ACCELEROMETER"`).
///
/// Both, because a person naming one sensor will reach for the name and a
/// script generating a sweep will reach for the value -- and the generated table
/// already holds both, so accepting only one would be a gratuitous way to be
/// wrong.
void readType(const SDK::JsonStreamReader &json, const char *query,
              uint32_t &out)
{
    std::string_view sv;
    if (!json.get(query, sv) || sv.empty()) {
        return;
    }

    char buf[40] = {};
    const size_t n = sv.size() < sizeof(buf) - 1 ? sv.size() : sizeof(buf) - 1;
    for (size_t i = 0; i < n; i++) {
        buf[i] = sv[i];
    }

    // Hex first: "0x10" or "0X10".
    if (n > 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
        uint32_t v = 0;
        for (size_t i = 2; i < n; i++) {
            const char c = buf[i];
            uint32_t   d = 0;
            if (c >= '0' && c <= '9')      { d = static_cast<uint32_t>(c - '0'); }
            else if (c >= 'a' && c <= 'f') { d = static_cast<uint32_t>(c - 'a' + 10); }
            else if (c >= 'A' && c <= 'F') { d = static_cast<uint32_t>(c - 'A' + 10); }
            else {
                LOG_WARNING("%s = '%s' is not a hex sensor type\n", query, buf);
                return;
            }
            v = v * 16u + d;
        }
        if (Catalogue::typeIndexOf(v) >= Catalogue::kTypeCount) {
            LOG_WARNING("%s = '%s' is not a type SensorTypes.hpp declares\n",
                        query, buf);
            return;
        }
        out = v;
        return;
    }

    for (size_t i = 0; i < Catalogue::kTypeCount; i++) {
        if (std::strcmp(buf, Catalogue::kTypes[i].name) == 0) {
            out = Catalogue::kTypes[i].value;
            return;
        }
    }
    LOG_WARNING("%s = '%s' is not a type SensorTypes.hpp declares\n", query, buf);
}

void readVersion(const SDK::JsonStreamReader &json, const char *query,
                 char *out, size_t outSize)
{
    std::string_view sv;
    if (!json.get(query, sv) || sv.empty()) {
        return;
    }
    size_t i = 0;
    for (; i + 1 < outSize && i < sv.size(); i++) {
        out[i] = sv[i];
    }
    out[i] = '\0';
}

void apply(const char *buffer, size_t len, Settings &out)
{
    SDK::JsonStreamReader json(buffer, len);

    // 0 is legal for both: it asks the driver for its own default, which is a
    // distinct experiment from asking for a number. The upper bounds are a
    // minute -- a requested period slower than that is not a sensor
    // configuration, it is a typo.
    readBounded(json, "values.period_ms",   0, 60000, out.periodMs);
    readBounded(json, "values.latency_ms",  0, 60000, out.latencyMs);

    // Five seconds is the fastest interval worth writing: below it the log is
    // dominated by its own row overhead. An hour is the slowest that still
    // localises a change to something less than a night.
    readBounded(json, "values.interval_sec", 5, 3600, out.intervalSec);

    // 0 = run until stopped, which is why the lower bound is 0 rather than 1.
    readBounded(json, "values.soak_max_minutes", 0, 10080, out.soakMaxMinutes);
    readBounded(json, "values.soak_max_kb",      64, 262144, out.soakMaxKb);

    readFlag(json, "values.subscribe_all", out.subscribeAll);
    readType(json, "values.only_type",     out.onlyType);
    readFlag(json, "values.field_stats",   out.fieldStats);

    readFlag(json, "values.raw_capture", out.rawCapture);
    // 1 MB floor: below that a chunk header is a measurable fraction of the
    // budget and the cap would be reached inside a second, which is a
    // configuration mistake rather than an experiment. 8 GB ceiling because the
    // volume is smaller than that and a larger number is a typo.
    readBounded(json, "values.raw_max_mb",   1, 8192, out.rawMaxMb);
    // 32 KB floor: four buffer-fulls, so rotation is not the dominant cost.
    readBounded(json, "values.raw_chunk_kb", 32, 65536, out.rawChunkKb);
    readFlag(json, "values.read_registers", out.readRegisters);

    readVersion(json, "values.firmware", out.declaredFirmware,
                sizeof(out.declaredFirmware));

    // -- Coherence checks, after every value is in --------------------------
    //
    // Combinations that parse cleanly and cannot do what was asked. Falling
    // back and saying so beats running a soak whose manifest claims something
    // the app was not doing.

    if (!out.subscribeAll && out.onlyType == 0) {
        LOG_WARNING("subscribe_all is off and only_type names no type; this run "
                    "will subscribe nothing, which measures the service and not "
                    "a sensor\n");
    }

    if (out.rawCapture && !out.fieldStats) {
        // Not an error -- it is the cheapest way to get an uncontended timing
        // measurement with the inputs kept -- but worth saying, because the
        // combination looks like a mistake and is not.
        LOG_INFO("raw capture on with field statistics off: the per-field "
                 "summaries will be absent and the raw samples they would have "
                 "come from will not\n");
    }

    if (out.readRegisters) {
        LOG_WARNING("read_registers is on, but no register path is built in this "
                    "version; layer 5's inference is what the profile will "
                    "carry\n");
    }
}

} // namespace

const char *toString(SettingsStatus s)
{
    switch (s) {
        case SettingsStatus::Ok:          return "settings.json loaded";
        case SettingsStatus::Absent:      return "no settings.json - using defaults";
        case SettingsStatus::TooLarge:    return "settings.json too large";
        case SettingsStatus::Unreadable:  return "settings.json unreadable";
        case SettingsStatus::NotJson:     return "settings.json is not valid JSON";
        case SettingsStatus::WrongSchema: return "settings.json has an unknown schema";
    }
    return "?";
}

SettingsStatus loadSettings(const SDK::Kernel &kernel, Settings &out,
                            const char *path)
{
    out = Settings {};

    SDK::Interface::IFileSystem::ObjectInfo info {};
    if (!kernel.fs.objectInfo(path, info) || info.isDir) {
        return SettingsStatus::Absent;
    }
    if (info.size > kSettingsMaxBytes) {
        LOG_WARNING("%s is %u bytes, over the %u limit\n", path,
                    static_cast<unsigned>(info.size),
                    static_cast<unsigned>(kSettingsMaxBytes));
        return SettingsStatus::TooLarge;
    }
    if (info.size == 0) {
        return SettingsStatus::NotJson;
    }

    std::unique_ptr<SDK::Interface::IFile> file = kernel.fs.file(path);
    if (!file || !file->open()) {
        return SettingsStatus::Unreadable;
    }

    std::unique_ptr<char[]> buffer(new (std::nothrow) char[info.size]);
    if (!buffer) {
        file->close();
        return SettingsStatus::Unreadable;
    }

    size_t     read = 0;
    const bool ok   = file->read(buffer.get(), info.size, read) && read == info.size;
    file->close();
    if (!ok) {
        return SettingsStatus::Unreadable;
    }

    SDK::JsonStreamReader json(buffer.get(), info.size);
    if (!json.validate()) {
        return SettingsStatus::NotJson;
    }

    uint32_t schema = 0;
    if (!json.get("schema", schema) || schema != kSettingsSchema) {
        return SettingsStatus::WrongSchema;
    }

    apply(buffer.get(), info.size, out);
    return SettingsStatus::Ok;
}

} // namespace SensorLab
