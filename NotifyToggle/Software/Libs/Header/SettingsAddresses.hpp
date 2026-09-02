/**
 ******************************************************************************
 * @file    SettingsAddresses.hpp
 * @brief   Per-firmware-version table of the raw addresses LiveSettings.cpp
 *          and SettingsPersist.cpp need. Not a supported SDK mechanism.
 ******************************************************************************
 *
 * Every address here is specific to one exact kernel build -- there is no
 * general formula from a version number to an address, only a one-time
 * reverse-engineering pass per firmware (disassembly, cross-checked against
 * a real caller and a live value, the same discipline documented in
 * una-sdk's `research` branch,
 * `Docs/Investigations/2026-08-31-live-settings-persistence/README.md`, and
 * this app's own copy of that investigation).
 *
 * A live, on-device signature scanner that tried to *find* these addresses
 * at runtime instead was considered and rejected: a scanner that silently
 * locks onto the wrong address on an unexpected firmware build is a worse
 * failure than refusing to run at all, and it would throw away exactly the
 * "confirmed via a real caller, not just a byte-pattern match" standard the
 * whole investigation held itself to. So: one verified entry per firmware
 * version, added only after doing the RE work for that version, and a
 * lookup that returns nothing -- not a guess -- for anything not listed.
 *
 * Growing this to a new firmware version means: dump and CRC-verify that
 * firmware's flash, re-derive every field below the same way 1.4.0's were
 * derived (register-level evidence, a real caller, a live cross-check where
 * possible), add a new entry, and leave every existing entry untouched.
 ******************************************************************************
 */

#ifndef SETTINGS_ADDRESSES_HPP
#define SETTINGS_ADDRESSES_HPP

#include <cstddef>
#include <cstdint>

namespace SettingsAddresses
{

/// Everything LiveSettings.cpp and SettingsPersist.cpp need, for one exact
/// firmware build. Addresses for the internal `File` class methods already
/// carry the Thumb bit (see SettingsPersist.hpp) -- callable as-is.
struct AddressSet {
    // --- LiveSettings: the kernel's live, in-RAM WatchSettings struct ---
    uintptr_t settingsStructBase;
    size_t    phoneNotificationsOffset;
    size_t    watchFaceIdOffset;

    // --- SettingsPersist: the kernel's internal, non-virtual File class ---
    uintptr_t fileOpenAddr;
    uintptr_t fileReadAddr;
    uintptr_t fileWriteAddr;
    uintptr_t fileCloseAddr;
    uintptr_t fileReleaseAddr;
    uintptr_t setPathAddr;

    // --- SettingsPersist: general-purpose kernel file utilities, used for
    // the tmp-file + backup-rotate + rename atomic commit pattern. Each
    // takes a plain path string, not a File object -- these delegate
    // through a filesystem singleton the kernel resolves internally on
    // every call, not something this app has to manage. ---
    uintptr_t fileExistsAddr;
    uintptr_t fileDeleteAddr;
    uintptr_t fileRenameAddr;

    // File object layout (may differ across builds even if the functions
    // above land at different addresses but keep the same shape -- kept
    // explicit rather than assumed constant).
    size_t fileObjectSize;
    size_t pathBufferOffset;
    size_t pathBufferSize;
    size_t fileSizeFieldOffset;
};

/// Looks up the verified address set for `firmwareVersion` (an exact
/// string match against SDK::Message::RequestSystemInfo's own
/// `firmwareVersion` field, e.g. "1.4.0" -- not a `>=` floor comparison,
/// unlike the manifest's `minKernelVersion`). Returns nullptr for any
/// version not in the table, including anything newer -- there is no
/// assumption that a later firmware keeps the same layout.
const AddressSet *resolve(const char *firmwareVersion);

} // namespace SettingsAddresses

#endif // SETTINGS_ADDRESSES_HPP
