/**
 ******************************************************************************
 * @file    LiveSettings.hpp
 * @brief   Direct read/write of the kernel's live, in-RAM WatchSettings
 *          struct -- specifically the `phone.notifications` field -- via a
 *          raw pointer. This is not a supported SDK mechanism.
 ******************************************************************************
 *
 * The supported route (open "../../settings.json" through Kernel::fs) was
 * tried first and conclusively failed -- that attempt no longer lives in
 * this tree (see git history and
 * Docs/Investigations/2026-08-31-live-settings-persistence/ for the record),
 * but the reason still matters: static disassembly of this exact unit's
 * verified kernel 1.4.0 flash dump (una-sdk/firmware-dumps/1.4.0,
 * CRC-checked) shows FileSystemGuard::getFullPath implements exactly one
 * relative-path escape -- a hardcoded literal match on "../SharedData" --
 * and rejects every other ".." path outright, logging
 * "Rejected/invalid path!". settings.json is not reachable through the
 * app-facing sandboxed filesystem at all, on this firmware.
 *
 * This is the fallback: UNA apps run **privileged** with the MPU switched
 * off (confirmed both by the SDK's own investigation docs and by this
 * unit's own dump_context.txt: CONTROL=0x6 -> nPRIV=0), so a running app
 * can read and write arbitrary memory with a plain pointer -- no fault. The
 * kernel's own settings loader parses settings.json once at boot into a
 * fixed, compile-time-constant struct address; this reads/writes that live
 * struct directly.
 *
 * ADDRESSES: not hardcoded here. Every function below takes a
 * `SettingsAddresses::AddressSet` -- the caller resolves that once, from the
 * watch's actual running firmware version (queried via a supported SDK
 * message, `SDK::Message::RequestSystemInfo`, never assumed), and refuses to
 * call anything here at all if that firmware hasn't been reverse-engineered
 * yet. See SettingsAddresses.hpp for why (a live signature scanner that
 * might lock onto the wrong address was considered and rejected) and for
 * the 1.4.0 entry's own address-by-address derivation (traced by hand from
 * the verified 1.4.0 dump, cross-checked against a live value and a second
 * code path -- not guessed), which used to live in this comment before this
 * module stopped being single-firmware-only.
 *
 * WHAT THIS ALONE DOES NOT DO: persist the change across a reboot -- that's
 * SettingsPersist.hpp, a separate step this module knows nothing about. This
 * is a live, immediate, in-memory change only.
 *
 * WHAT THIS DOES NOT DO EITHER: take the kernel's own settings mutex before
 * writing (a lock/unlock pair was observed guarding this struct in the
 * kernel's own code, at a separate address). A race against the kernel
 * writing the same struct at the same instant is possible in principle; it
 * is accepted here as a small, bounded risk for a discrete, user-triggered,
 * infrequent action, not attempted to be closed by calling another
 * unverified internal function.
 ******************************************************************************
 */

#ifndef LIVE_SETTINGS_HPP
#define LIVE_SETTINGS_HPP

#include "SDK/Interfaces/IFileSystem.hpp"

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

/// Read-only: reads the live `phone.notifications` byte and a nearby
/// cross-check field (`watchFaceId`), refusing (Status other than Ok) rather
/// than trusting a value that doesn't look like what this address should
/// hold. `addrs` is the caller's already-resolved SettingsAddresses::AddressSet
/// for the watch's actual running firmware -- never guessed, never a default,
/// see SettingsAddresses.hpp. `fs` is used only to write diagnostic log lines
/// (DebugLog), the same debug.log/gui-debug.log/service-debug.log mechanism
/// used everywhere else tonight -- there being no wired-up debug UART to log
/// to instead.
Status readNotificationsFlag(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs, bool &outEnabled);

/// Reads fresh, refuses under the same conditions as readNotificationsFlag,
/// and only then writes -- skipping the write entirely (Status::NoChange) if
/// the live value already matches.
Status writeNotificationsFlag(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs, bool newEnabled);

} // namespace LiveSettings

#endif // LIVE_SETTINGS_HPP
