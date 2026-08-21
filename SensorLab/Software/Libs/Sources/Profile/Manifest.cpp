/**
 ******************************************************************************
 * @file    Manifest.cpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The run manifest. Rationale is in the header.
 ******************************************************************************
 */

#include "Profile/Manifest.hpp"

#include <cstdio>
#include <cstring>

#include "SDK/Interfaces/IKernel.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"

#define LOG_MODULE_PRX      "Manifest"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace SensorLab::Profile
{

namespace
{

/// Response timeout for `RequestSystemInfo`, milliseconds.
///
/// A hundred, matching what `SensorConnection` uses for the sensor-layer
/// handshake. The request happens once, before any sensor is subscribed, so the
/// cost of waiting is a tenth of a second at startup and the cost of not
/// waiting is a profile with no primary key.
constexpr uint32_t kSystemInfoTimeoutMs = 100;

/// Copy at most @p outSize-1 characters, terminating.
///
/// The kernel's `firmwareVersion[16]` carries no guarantee of a terminator, so
/// a version string that filled the field exactly would otherwise run into
/// whatever follows it in the pool block.
void copyBounded(char *out, size_t outSize, const char *in, size_t inMax)
{
    size_t i = 0;
    for (; i + 1 < outSize && i < inMax && in[i] != '\0'; i++) {
        out[i] = in[i];
    }
    out[i] = '\0';
}

/// FatFs-safe: letters, digits, dot and dash survive; everything else becomes a
/// dash. The input is a string the kernel chose, so it is not this app's to
/// trust.
void sanitise(char *s)
{
    for (; *s != '\0'; s++) {
        const char c = *s;
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')
                        || (c >= 'A' && c <= 'Z') || c == '.' || c == '-';
        if (!ok) {
            *s = '-';
        }
    }
}

} // namespace

const char *toString(RunEnd e)
{
    switch (e) {
        case RunEnd::InProgress:        return "in_progress";
        case RunEnd::Completed:         return "completed";
        case RunEnd::Aborted:           return "aborted";
        case RunEnd::TruncatedByUsb:    return "truncated_by_usb";
        case RunEnd::TruncatedByReboot: return "truncated_by_reboot";
    }
    return "?";
}

bool readSystemInfo(const SDK::Kernel &kernel, RunManifest &out)
{
    auto req = SDK::make_msg<SDK::Message::RequestSystemInfo>(kernel);
    if (!req) {
        LOG_WARNING("no pool block for RequestSystemInfo; firmware unread\n");
        return false;
    }

    if (!req.send(kSystemInfoTimeoutMs) || !req.ok()) {
        // Not an error worth failing startup over, but worth saying: it is the
        // difference between a diffable profile and an undiffable one.
        LOG_WARNING("kernel did not answer RequestSystemInfo; the profile's "
                    "primary key falls back to settings.json\n");
        return false;
    }

    copyBounded(out.firmware, sizeof(out.firmware), req->firmwareVersion,
                sizeof(req->firmwareVersion));
    copyBounded(out.hardware, sizeof(out.hardware), req->hardwareVersion,
                sizeof(req->hardwareVersion));

    // An answer whose firmware string is empty is not an answer. Treated as a
    // failure rather than as "firmware version: (blank)", which would look like
    // a measurement.
    if (out.firmware[0] == '\0') {
        LOG_WARNING("RequestSystemInfo succeeded with an empty firmware "
                    "string; treating it as unread\n");
        out.hardware[0] = '\0';
        return false;
    }

    out.haveSystemInfo = true;
    LOG_INFO("firmware %s hardware %s (read from the kernel)\n",
             out.firmware, out.hardware);
    return true;
}

void stampBuild(RunManifest &out, const char *appVersion)
{
    copyBounded(out.appVersion, sizeof(out.appVersion),
                appVersion != nullptr ? appVersion : "", kVersionMax);
    out.kernelInterfaceVersion = KERNEL_INTERFACE_VERSION;
    out.catalogueVersion       = Catalogue::kCatalogueVersion;
    out.typeTableVersion       = Catalogue::kTypeTableVersion;
    copyBounded(out.sdkTag, sizeof(out.sdkTag), Catalogue::kGeneratedFromSdk,
                sizeof(Catalogue::kGeneratedFromSdk));
}

size_t profileFileName(char *out, size_t outSize, const RunManifest &m)
{
    if (out == nullptr || outSize == 0) {
        return 0;
    }

    char version[kVersionMax];
    copyBounded(version, sizeof(version),
                m.firmware[0] != '\0' ? m.firmware : "unknown", kVersionMax);
    sanitise(version);

    const int n = std::snprintf(out, outSize, "profile-%s.json", version);
    if (n <= 0 || static_cast<size_t>(n) >= outSize) {
        out[0] = '\0';
        return 0;
    }
    return static_cast<size_t>(n);
}

size_t runLogFileName(char *out, size_t outSize, uint32_t runId)
{
    const int n = std::snprintf(out, outSize, "runs/%lu.csv",
                                static_cast<unsigned long>(runId));
    if (n <= 0 || static_cast<size_t>(n) >= outSize) {
        if (outSize > 0) { out[0] = '\0'; }
        return 0;
    }
    return static_cast<size_t>(n);
}

size_t runManifestFileName(char *out, size_t outSize, uint32_t runId)
{
    const int n = std::snprintf(out, outSize, "runs/%lu.json",
                                static_cast<unsigned long>(runId));
    if (n <= 0 || static_cast<size_t>(n) >= outSize) {
        if (outSize > 0) { out[0] = '\0'; }
        return 0;
    }
    return static_cast<size_t>(n);
}

} // namespace SensorLab::Profile
