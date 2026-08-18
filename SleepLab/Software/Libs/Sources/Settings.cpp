/**
 ******************************************************************************
 * @file    Settings.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   What the wearer has asked for. Rationale is in the header.
 ******************************************************************************
 */

#include "Settings.hpp"

#include <cstring>
#include <memory>

#include "SDK/JSON/JsonStreamReader.hpp"

#define LOG_MODULE_PRX      "Settings"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace SleepLab
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

const char *toString(SettingsStatus status)
{
    switch (status) {
        case SettingsStatus::Absent:      return "no settings.json - using defaults";
        case SettingsStatus::TooLarge:    return "settings.json too large";
        case SettingsStatus::Unreadable:  return "settings.json unreadable";
        case SettingsStatus::NotJson:     return "settings.json is not valid JSON";
        case SettingsStatus::WrongSchema: return "settings.json has an unknown schema";
        case SettingsStatus::Ok:          return "settings.json loaded";
    }
    return "?";
}

namespace {

/// Read a bounded unsigned, leaving @p out alone if the key is absent or the
/// value is outside [lo, hi].
///
/// Refused rather than clamped -- see the header. Every rejection carries the
/// value and the range into the log, because on a watch with no keyboard that
/// log line is the only way to find out the file was wrong.
template <typename T>
void readBounded(const SDK::JsonStreamReader &json, const char *query,
                 uint32_t lo, uint32_t hi, T &out)
{
    uint32_t raw = 0;
    if (!json.get(query, raw)) {
        return;
    }
    if (raw < lo || raw > hi) {
        LOG_WARNING("%s = %lu outside [%lu, %lu], keeping %ld\n", query,
                    static_cast<unsigned long>(raw),
                    static_cast<unsigned long>(lo),
                    static_cast<unsigned long>(hi),
                    static_cast<long>(out));
        return;
    }
    out = static_cast<T>(raw);
}

/// Read `"hh:mm"` as local minutes past midnight.
///
/// A time of day written the way a person writes one. `"23:15"` is
/// unambiguous; 1395 is a number somebody has to compute, and get wrong.
void readTimeOfDay(const SDK::JsonStreamReader &json, const char *query,
                   int16_t &out)
{
    std::string_view sv;
    if (!json.get(query, sv)) {
        return;
    }

    // Exactly "H:MM" or "HH:MM". Deliberately strict: a partially-parsed time
    // is a wrong time, and a wrong bedtime window records the wrong hours.
    if (sv.size() < 4 || sv.size() > 5) {
        LOG_WARNING("%s = '%.*s' is not hh:mm\n", query,
                    static_cast<int>(sv.size()), sv.data());
        return;
    }
    const size_t colon = sv.find(':');
    if (colon == std::string_view::npos || colon == 0 ||
        sv.size() - colon - 1 != 2) {
        LOG_WARNING("%s = '%.*s' is not hh:mm\n", query,
                    static_cast<int>(sv.size()), sv.data());
        return;
    }

    int hh = 0, mm = 0;
    for (size_t i = 0; i < colon; i++) {
        if (sv[i] < '0' || sv[i] > '9') { return; }
        hh = hh * 10 + (sv[i] - '0');
    }
    for (size_t i = colon + 1; i < sv.size(); i++) {
        if (sv[i] < '0' || sv[i] > '9') { return; }
        mm = mm * 10 + (sv[i] - '0');
    }

    if (hh > 23 || mm > 59) {
        LOG_WARNING("%s = '%.*s' is not a time of day\n", query,
                    static_cast<int>(sv.size()), sv.data());
        return;
    }
    out = static_cast<int16_t>(hh * 60 + mm);
}

/// Read a flag written the way a person writes one.
///
/// The accepted vocabulary matches Squash's `record_imu` deliberately: these
/// files are typed by hand, and a flag that only accepts one spelling of "yes"
/// is a flag that will be got wrong.
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

    if (std::strcmp(lower, "on")   == 0 || std::strcmp(lower, "yes")     == 0 ||
        std::strcmp(lower, "true") == 0 || std::strcmp(lower, "1")       == 0 ||
        std::strcmp(lower, "enabled") == 0) {
        out = true;
        return;
    }
    if (std::strcmp(lower, "off")   == 0 || std::strcmp(lower, "no")      == 0 ||
        std::strcmp(lower, "false") == 0 || std::strcmp(lower, "0")       == 0 ||
        std::strcmp(lower, "disabled") == 0) {
        out = false;
        return;
    }

    LOG_WARNING("%s = '%.*s' is neither on nor off, keeping %s\n", query,
                static_cast<int>(sv.size()), sv.data(), out ? "on" : "off");
}

void apply(const char *buffer, size_t len, Settings &out)
{
    SDK::JsonStreamReader json(buffer, len);

    readTimeOfDay(json, "values.bedtime",      out.segmenter.windowStartMin);
    readTimeOfDay(json, "values.wake_by",      out.segmenter.windowEndMin);
    readTimeOfDay(json, "values.alarm_at",     out.alarmDeadlineMin);

    readBounded(json, "values.min_night_min",      30, 960,
                out.segmenter.minSessionMin);
    readBounded(json, "values.alarm_window_min",    5, 120, out.alarmWindowMin);

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

    readFlag(json, "values.alarm",         out.alarmEnabled);
    readFlag(json, "values.raw_recording", out.rawRecording);
    readBounded(json, "values.raw_max_mb",   1, 512,  out.rawMaxMb);
    readBounded(json, "values.raw_max_min",  1, 1440, out.rawMaxMin);

    // -- Coherence checks, after every value is in -------------------------
    //
    // Each of these is a combination that parses cleanly and cannot work.
    // Falling back and saying so beats recording a night whose settings claim
    // something the app was not doing.

    if (out.hrMode == HrMode::Duty && out.hrDutyOnSec >= out.hrDutyPerSec) {
        LOG_WARNING("duty on=%u >= period=%u, falling back to continuous\n",
                    static_cast<unsigned>(out.hrDutyOnSec),
                    static_cast<unsigned>(out.hrDutyPerSec));
        out.hrMode = HrMode::Continuous;
    }

    if (out.segmenter.windowStartMin == out.segmenter.windowEndMin) {
        LOG_WARNING("bedtime == wake_by (%d): a zero-width window never opens, "
                    "restoring defaults\n",
                    static_cast<int>(out.segmenter.windowStartMin));
        const Engine::SegmenterConfig def;
        out.segmenter.windowStartMin = def.windowStartMin;
        out.segmenter.windowEndMin   = def.windowEndMin;
    }

    // An alarm outside the bedtime window can never fire: the session is
    // already closed by then, and a silent alarm is worse than no alarm.
    if (out.alarmEnabled &&
        !Engine::inWindow(out.alarmDeadlineMin, out.segmenter.windowStartMin,
                          out.segmenter.windowEndMin)) {
        LOG_WARNING("alarm_at is outside the bedtime window and could never "
                    "fire; disabling it\n");
        out.alarmEnabled = false;
    }
}

} // namespace

SettingsStatus loadSettings(const SDK::Kernel &kernel, Settings &out,
                            const char *path)
{
    out = Settings{};

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

} // namespace SleepLab
