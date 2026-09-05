/**
 ******************************************************************************
 * @file    LiveSettings.hpp
 * @brief   Direct read/write of the kernel's live, in-RAM `phone.notifications`
 *          byte via a raw pointer. Not a supported SDK mechanism.
 ******************************************************************************
 *
 * The supported route does not exist: `FileSystemGuard::getFullPath` allows
 * exactly one relative-path escape, a hardcoded match on "../SharedData", and
 * rejects every other ".." path. `2:/settings.json` is unreachable through the
 * app-facing filesystem on this firmware -- confirmed by disassembly of a
 * CRC-verified 1.4.0 dump, and on a watch, where `exist("2:/")` and every
 * two-hop spelling returned false.
 *
 * This is the fallback: UNA apps run privileged with the MPU off (this unit's
 * own boot state, CONTROL=0x6 -> nPRIV=0), so a plain pointer reaches the
 * struct the kernel parses settings.json into at boot.
 *
 ******************************************************************************
 */

#ifndef LIVE_SETTINGS_HPP
#define LIVE_SETTINGS_HPP

#include "SDK/Interfaces/IFileSystem.hpp"

#include <cstdint>

#include "SettingsAddresses.hpp"

namespace LiveSettings
{

enum class Status {
    Ok,
    NoChange,                ///< Write only: already the requested value; nothing written.
    UnexpectedCurrentValue,  ///< The notifications byte wasn't 0 or 1 -- refused to trust the address.
    CrossCheckOutOfRange,    ///< The watchFaceId cross-check field looked implausible -- refused.
    ReadbackMismatch,        ///< Write only: the byte didn't read back as what was just written.
};

/// Reads the live `phone.notifications` byte and a nearby cross-check field
/// (`watchFaceId`), refusing rather than trusting a value that does not look
/// like what this address should hold. `fs` is used only for DebugLog.
Status readNotificationsFlag(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs, bool &outEnabled);

/// Reads fresh, refuses under the same conditions as readNotificationsFlag,
/// and only then writes -- skipping the write entirely (Status::NoChange) if
/// the live value already matches.
Status writeNotificationsFlag(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs, bool newEnabled);

/// True if two fields read straight out of the struct match what the kernel
/// reports for them through RequestSystemSettings. Both sides are live, so
/// nothing can drift them apart -- unlike a comparison against settings.json,
/// which this app itself diverges the moment it flips the flag without saving.
/// Neither field is one this app ever writes.
bool matchesKernel(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                   uint32_t kernelActivityMinutes, uint32_t kernelSteps);

} // namespace LiveSettings

#endif // LIVE_SETTINGS_HPP
