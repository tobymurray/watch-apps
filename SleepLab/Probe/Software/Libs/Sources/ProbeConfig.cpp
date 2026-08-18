/**
 ******************************************************************************
 * @file    ProbeConfig.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   What this run of the probe measures, from `probe.json`.
 ******************************************************************************
 */

#include "ProbeConfig.hpp"

#include <cstring>
#include <memory>

#include "SDK/JSON/JsonStreamReader.hpp"

#define LOG_MODULE_PRX      "ProbeConfig"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace Probe
{

const char *toString(HrMode mode)
{
    switch (mode) {
        case HrMode::Continuous: return "continuous";
        case HrMode::Off:        return "off";
        case HrMode::Duty:       return "duty";
    }
    return "?";
}

const char *toString(Status status)
{
    switch (status) {
        case Status::Absent:      return "absent";
        case Status::TooLarge:    return "too-large";
        case Status::Unreadable:  return "unreadable";
        case Status::NotJson:     return "not-json";
        case Status::WrongSchema: return "wrong-schema";
        case Status::Ok:          return "ok";
    }
    return "?";
}

namespace {

/// Read a bounded unsigned into @p out, leaving it alone if the key is absent
/// or the value is outside [lo, hi].
///
/// Out of range is treated as absent rather than clamped. Clamping turns a
/// typo into a silently different experiment: `"accel_period_ms": 4000`
/// meaning 4 s would clamp to the ceiling and record a night at some rate
/// nobody chose, and the log would look perfectly healthy.
void readBounded(const SDK::JsonStreamReader &json, const char *query,
                 uint16_t lo, uint16_t hi, uint16_t &out)
{
    uint32_t raw = 0;
    if (!json.get(query, raw)) {
        return;
    }
    if (raw < lo || raw > hi) {
        LOG_WARNING("%s = %lu is outside [%u, %u], keeping %u\n",
                    query, static_cast<unsigned long>(raw),
                    static_cast<unsigned>(lo), static_cast<unsigned>(hi),
                    static_cast<unsigned>(out));
        return;
    }
    out = static_cast<uint16_t>(raw);
}

/// Read a flag written the way a person writes one.
///
/// The accepted vocabulary matches Squash's `record_imu`, deliberately: these
/// files are typed by hand into Notepad, and a flag that only accepts one
/// spelling of "yes" is a flag that will be got wrong.
void readFlag(const SDK::JsonStreamReader &json, const char *query, bool &out)
{
    std::string_view sv;
    if (!json.get(query, sv)) {
        return;
    }

    char lower[16] = {};
    const size_t n = sv.size() < sizeof(lower) - 1 ? sv.size() : sizeof(lower) - 1;
    for (size_t i = 0; i < n; i++) {
        const char c = sv[i];
        lower[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    if (std::strcmp(lower, "on")   == 0 || std::strcmp(lower, "yes") == 0 ||
        std::strcmp(lower, "true") == 0 || std::strcmp(lower, "1")   == 0 ||
        std::strcmp(lower, "enabled") == 0) {
        out = true;
        return;
    }
    if (std::strcmp(lower, "off")  == 0 || std::strcmp(lower, "no")  == 0 ||
        std::strcmp(lower, "false") == 0 || std::strcmp(lower, "0")  == 0 ||
        std::strcmp(lower, "disabled") == 0) {
        out = false;
        return;
    }

    LOG_WARNING("%s = '%.*s' is neither on nor off, keeping %s\n",
                query, static_cast<int>(sv.size()), sv.data(),
                out ? "on" : "off");
}

/// Parse the validated buffer into @p out. Every key is optional.
void apply(const char *buffer, size_t len, Config &out)
{
    SDK::JsonStreamReader json(buffer, len);

    std::string_view hr;
    if (json.get("values.hr", hr)) {
        if (hr == "continuous") {
            out.hrMode = HrMode::Continuous;
        } else if (hr == "off") {
            out.hrMode = HrMode::Off;
        } else if (hr == "duty") {
            out.hrMode = HrMode::Duty;
        } else {
            LOG_WARNING("values.hr = '%.*s' unknown, keeping %s\n",
                        static_cast<int>(hr.size()), hr.data(),
                        toString(out.hrMode));
        }
    }

    readBounded(json, "values.hr_duty_on_sec",   5, 3600, out.hrDutyOnSec);
    readBounded(json, "values.hr_duty_per_sec", 10, 3600, out.hrDutyPerSec);
    readBounded(json, "values.accel_period_ms",  5, 1000, out.accelPeriodMs);
    readBounded(json, "values.accel_latency_ms", 0, 60000, out.accelLatencyMs);

    readFlag(json, "values.ppg",  out.ppgEnabled);
    readFlag(json, "values.spo2", out.spo2Enabled);

    // A duty cycle whose on-time meets or exceeds its period is not a duty
    // cycle. Fall back to continuous and say so, rather than run a night whose
    // mode column claims something the sensor was not doing.
    if (out.hrMode == HrMode::Duty && out.hrDutyOnSec >= out.hrDutyPerSec) {
        LOG_WARNING("duty on=%u >= period=%u, falling back to continuous\n",
                    static_cast<unsigned>(out.hrDutyOnSec),
                    static_cast<unsigned>(out.hrDutyPerSec));
        out.hrMode = HrMode::Continuous;
    }
}

} // namespace

Status load(const SDK::Kernel &kernel, Config &out, const char *path)
{
    out = Config{};

    SDK::Interface::IFileSystem::ObjectInfo info {};
    if (!kernel.fs.objectInfo(path, info) || info.isDir) {
        return Status::Absent;
    }

    if (info.size > kConfigMaxBytes) {
        LOG_WARNING("%s is %u bytes, over the %u limit\n",
                    path, static_cast<unsigned>(info.size),
                    static_cast<unsigned>(kConfigMaxBytes));
        return Status::TooLarge;
    }
    if (info.size == 0) {
        return Status::NotJson;
    }

    std::unique_ptr<SDK::Interface::IFile> file = kernel.fs.file(path);
    if (!file || !file->open()) {
        return Status::Unreadable;
    }

    std::unique_ptr<char[]> buffer(new (std::nothrow) char[info.size]);
    if (!buffer) {
        file->close();
        return Status::Unreadable;
    }

    size_t     read = 0;
    const bool ok   = file->read(buffer.get(), info.size, read) && read == info.size;
    file->close();
    if (!ok) {
        return Status::Unreadable;
    }

    SDK::JsonStreamReader json(buffer.get(), info.size);
    if (!json.validate()) {
        return Status::NotJson;
    }

    uint32_t schema = 0;
    if (!json.get("schema", schema) || schema != kConfigSchema) {
        return Status::WrongSchema;
    }

    apply(buffer.get(), info.size, out);
    return Status::Ok;
}

} // namespace Probe
