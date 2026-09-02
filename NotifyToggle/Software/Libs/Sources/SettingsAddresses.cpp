#include "SettingsAddresses.hpp"

#include <cstring>

namespace SettingsAddresses
{

namespace
{

// Kernel 1.4.0, this exact unit. See SettingsAddresses.hpp for the
// derivation standard, and
// Docs/Investigations/2026-08-31-live-settings-persistence/README.md (this
// app) / una-sdk's `research` branch (canonical) for the full address-by-
// address evidence and falsification checks.
constexpr AddressSet kFirmware_1_4_0 = {
    /* settingsStructBase       */ 0x20010cb0u,
    /* phoneNotificationsOffset */ 5u,
    /* watchFaceIdOffset        */ 8u,
    /* fileOpenAddr             */ 0x0809b254u | 1u,
    /* fileReadAddr             */ 0x0809b4e8u | 1u,
    /* fileWriteAddr            */ 0x0809b334u | 1u,
    /* fileCloseAddr            */ 0x0809b450u | 1u,
    /* fileReleaseAddr          */ 0x0809b2ccu | 1u,
    /* setPathAddr              */ 0x0802b3eau | 1u,
    /* fileObjectSize           */ 856u,
    /* pathBufferOffset         */ 0x04u,
    /* pathBufferSize           */ 0x100u,
    /* fileSizeFieldOffset      */ 0x118u,
    // General-purpose kernel file utilities (real-caller counts across the
    // full 4MB image: exists 55, delete 41, rename 21 -- not Settings-
    // specific, confirmed general kernel primitives). Each takes plain
    // path string(s); the filesystem singleton they delegate through is
    // resolved internally on every call, so no singleton address is needed
    // here.
    /* fileExistsAddr           */ 0x0809b5a0u | 1u,
    /* fileDeleteAddr           */ 0x0809b648u | 1u,
    /* fileRenameAddr           */ 0x0809b5d8u | 1u,
};

struct Entry {
    const char *version;
    const AddressSet *addresses;
};

// One row per firmware version that has actually had this investigation's
// RE process run against it, cross-checked and verified live. Add a row
// only after doing that work -- never by extrapolating from a neighboring
// version.
constexpr Entry kSupported[] = {
    {"1.4.0", &kFirmware_1_4_0},
};

} // namespace

const AddressSet *resolve(const char *firmwareVersion)
{
    if (firmwareVersion == nullptr) {
        return nullptr;
    }
    for (const auto &entry : kSupported) {
        if (std::strcmp(entry.version, firmwareVersion) == 0) {
            return entry.addresses;
        }
    }
    return nullptr;
}

} // namespace SettingsAddresses
