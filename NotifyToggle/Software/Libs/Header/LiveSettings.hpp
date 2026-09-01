/**
 ******************************************************************************
 * @file    LiveSettings.hpp
 * @brief   Direct read/write of the kernel's live, in-RAM WatchSettings
 *          struct -- specifically the `phone.notifications` field -- via a
 *          raw pointer. This is not a supported SDK mechanism.
 ******************************************************************************
 *
 * SettingsFile.hpp/SettingsPatch.hpp attempted the supported route (open
 * "../../settings.json" through Kernel::fs) and conclusively failed: static
 * disassembly of this exact unit's verified kernel 1.4.0 flash dump
 * (una-sdk/firmware-dumps/1.4.0, CRC-checked) shows FileSystemGuard::getFullPath
 * implements exactly one relative-path escape -- a hardcoded literal match on
 * "../SharedData" -- and rejects every other ".." path outright, logging
 * "Rejected/invalid path!". settings.json is not reachable through the
 * app-facing sandboxed filesystem at all, on this firmware.
 *
 * This is the fallback: UNA apps run unprivileged with the MPU switched off
 * (confirmed both by the SDK's own investigation docs and by this unit's own
 * dump_context.txt), so a running app can read and write arbitrary memory
 * with a plain pointer -- no fault. The kernel's own settings loader parses
 * settings.json once at boot into a fixed, compile-time-constant struct
 * address; this reads/writes that live struct directly.
 *
 * ADDRESS PROVENANCE (traced by hand from the verified dump, not guessed):
 *
 *   806b5ba: ldr r4, [pc, #472] @ (0x806b794)   ; literal pool word
 *   -> flash word at 0x0806b794 contains the constant 0x20010ca8
 *
 *   806b618: add.w r3, r4, #8                   ; substruct = 0x20010cb0
 *   806b61c: bl    0x80abc78                     ; the settings JSON loader
 *
 *   Inside the loader, the "phone.notifications" field specifically:
 *   80abd0c: ldr r1, [pc,#168] @ (0x80abdb8)     ; r1 = &"phone.notifications"
 *   80abd0e: adds r2, r4, #5                     ; dest = (inner r4) + 5
 *   80abd12: bl    0x80ca6a4                      ; getBool-into-r2
 *
 *   => kPhoneNotificationsAddr = 0x20010cb0 + 5 = 0x20010cb5
 *
 * This address is valid ONLY for kernel 1.4.0 on this exact watch unit (UID
 * matches una-sdk/firmware-dumps/1.4.0/README.md, whole-image CRC32
 * 0x14009D03 independently verified against the device and the host). It is
 * not portable to another unit or firmware version without re-deriving it
 * the same way.
 *
 * WHAT THIS DOES NOT DO: persist the change across a reboot. The kernel's
 * settings-save-to-flash function has not yet been identified with the same
 * confidence as this read address (the first candidate examined turned out
 * to be a reset-to-factory-defaults routine, not a save routine, and was not
 * used). This is a live, immediate, in-memory change only.
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
/// hold. `fs` is used only to write diagnostic log lines (DebugLog), the
/// same debug.log/gui-debug.log/service-debug.log mechanism used everywhere
/// else tonight -- there being no wired-up debug UART to log to instead.
Status readNotificationsFlag(SDK::Interface::IFileSystem &fs, bool &outEnabled);

/// Reads fresh, refuses under the same conditions as readNotificationsFlag,
/// and only then writes -- skipping the write entirely (Status::NoChange) if
/// the live value already matches.
Status writeNotificationsFlag(SDK::Interface::IFileSystem &fs, bool newEnabled);

} // namespace LiveSettings

#endif // LIVE_SETTINGS_HPP
