/**
 ******************************************************************************
 * @file    SettingsPersist.hpp
 * @brief   Persists `phone.notifications` to `2:/settings.json` on flash, so
 *          a LiveSettings write survives a reboot -- not just this app
 *          session or this power-on. Not a supported SDK mechanism.
 ******************************************************************************
 *
 * LiveSettings.hpp/.cpp already gets an immediate, in-RAM change working by
 * writing the kernel's live WatchSettings struct directly. That does not
 * survive a reboot: nothing writes the change back to the file the kernel
 * loads that struct from at boot. This closes that gap.
 *
 * `Settings::save()`, the class-level C++ save method this investigation
 * first went looking for, turned out to have zero callers anywhere in the
 * 4MB kernel image on this firmware (confirmed by two independent static
 * passes) -- the real UNA phone app never calls it either; it does a
 * whole-file JSON overwrite over BLE File Transfer instead, decompiled and
 * confirmed this session. That BLE path is not available to an on-watch app
 * calling itself, so this instead calls the kernel's own internal `File`
 * class directly, in-process -- the same non-virtual open/read/write/close
 * primitives the kernel's own settings-backup logic already uses for this
 * exact file in normal operation (reached, in stock firmware, only via BLE;
 * this app calls them the same way LiveSettings.hpp reads/writes the live
 * struct directly, rather than through the SDK's sandboxed IFileSystem,
 * which cannot reach settings.json at all on this firmware -- see
 * LiveSettings.hpp's doc comment for that derivation).
 *
 * ADDRESS PROVENANCE: every function address and calling convention below is
 * register-evidenced against a real, confirmed caller in the 1.4.0 flash
 * dump (una-sdk/firmware-dumps/1.4.0, whole-image CRC32 0x14009D03) -- not a
 * decompiler guess. Full derivation, including the disassembly excerpts that
 * establish each signature, lives in una-sdk's `research` branch,
 * `Docs/Investigations/2026-08-31-live-settings-persistence/README.md`
 * ("Path 2 (in-process route)" section) -- that is the canonical, kept-current
 * record; treat this comment as a summary, not the source of truth.
 *
 * These addresses are valid ONLY for kernel 1.4.0 on this exact watch unit,
 * same caveat as LiveSettings.hpp.
 *
 * WHAT THIS DOES: reads the real current bytes of 2:/settings.json (not a
 * regeneration from known struct fields -- several fields, e.g. gender,
 * dateOfBirth, version, are not fully characterized, and regenerating from a
 * partial field set risks the kernel's loader resetting unknown/missing keys
 * to defaults on next boot, silently dropping real personal data), splices
 * in exactly the one field this app owns, and writes the whole file back.
 * Every other byte of the file is preserved unchanged.
 *
 * WHAT THIS DOES NOT DO: use the firmware's own atomic tmp-file + backup +
 * rename commit pattern (confirmed to exist, at 0x0806dd54/0x0806de64) --
 * that path routes through a filesystem singleton whose own calling
 * convention has not been register-evidenced the way everything used here
 * has. This does a direct overwrite instead, relying on the same
 * NotifyToggle/DeviceBackups/ backup-before-every-write habit as the rest of
 * this investigation for crash-safety instead of an on-device atomic commit.
 ******************************************************************************
 */

#ifndef SETTINGS_PERSIST_HPP
#define SETTINGS_PERSIST_HPP

#include "SDK/Interfaces/IFileSystem.hpp"

namespace SettingsPersist
{

enum class Status {
    Ok,
    ReadOpenFailed,     ///< Could not open 2:/settings.json for reading.
    ReadFailed,         ///< Open succeeded but the read itself failed or was short.
    SizeOutOfRange,      ///< The file's reported size was 0 or larger than the sane cap this
                          ///< app reads into -- refused rather than reading a truncated or
                          ///< unexpectedly huge file.
    FieldNotFound,       ///< Neither `"notifications":true` nor `"notifications":false`
                          ///< appears in the file content read back -- refused to write,
                          ///< since the format assumption this app relies on doesn't hold.
    WriteOpenFailed,      ///< Could not open 2:/settings.json for writing.
    WriteFailed,          ///< Open succeeded but the write itself failed or was short.
    ReadbackMismatch,     ///< Post-write readback didn't match what was just written.
};

/// Reads the real current content of `2:/settings.json`, replaces exactly the
/// one `phone.notifications` field with `newEnabled`, writes the whole file
/// back, and reads it back again to verify a byte-exact match. `fs` is used
/// only for DebugLog diagnostics, same as LiveSettings -- there being no
/// wired-up debug UART to log to instead.
Status persistNotificationsFlag(SDK::Interface::IFileSystem &fs, bool newEnabled);

} // namespace SettingsPersist

#endif // SETTINGS_PERSIST_HPP
