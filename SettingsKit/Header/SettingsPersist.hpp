/**
 ******************************************************************************
 * @file    SettingsPersist.hpp
 * @brief   Writes one field back to `2:/settings.json` so a LiveSettings
 *          change survives a reboot. Not a supported SDK mechanism.
 ******************************************************************************
 *
 * LiveSettings changes the kernel's in-RAM copy; nothing writes that back to
 * the file the kernel loads at boot. This closes that gap by calling the
 * kernel's own internal `File` primitives in-process, at addresses the caller
 * resolves from the watch's running firmware (SettingsAddresses.hpp).
 *
 * The file is read, spliced and written whole rather than regenerated from
 * known fields: gender, dateOfBirth and version are not characterized, and
 * regenerating would drop whatever this app cannot name.
 ******************************************************************************
 */

#ifndef SETTINGS_PERSIST_HPP
#define SETTINGS_PERSIST_HPP

#include "SDK/Interfaces/IFileSystem.hpp"

#include "DebugLog.hpp"
#include "SettingsAddresses.hpp"
#include "SettingsField.hpp"
#include "SettingsPersistLimits.hpp"
#include "SettingsSplice.hpp"

namespace SettingsPersist
{

enum class Status {
    Ok,
    ReadOpenFailed,     ///< Could not open 2:/settings.json for reading.
    ReadFailed,         ///< Open succeeded but the read itself failed or was short.
    /// The reported size was 0 or above the cap this app reads into. Zero is a
    /// real file mid-write, not a missing one: on 2026-09-06 a units change made
    /// from the phone left `2:/settings.json` reading 0 bytes with a null FAT
    /// timestamp, and a later read returned the completed 247-byte file.
    /// Falsified by a whole-file settings write that is never observable at zero
    /// length.
    SizeOutOfRange,
    FieldNotFound,      ///< The field is absent or unrecognised in the file; nothing written.
    WriteOpenFailed,    ///< Could not open the temporary file. The real file was never touched.
    WriteFailed,        ///< The temporary write failed or was short. The real file was never touched.
    CommitFailed,       ///< The temporary file was written but could not be moved into place;
                        ///< the previous file is put back, and the temporary is left for recovery.
    ReadbackMismatch,   ///< The commit happened, but re-reading did not return what was written.
};

/// Replaces `field` in `2:/settings.json` with `newEnabled`, leaving every
/// other byte as it was, and re-reads to confirm. `addrs` is the caller's
/// already-resolved set for the running firmware. `fs` is used only for
/// DebugLog, there being no wired-up debug UART.
Status persistFlag(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                   const Field &field, bool newEnabled);

/// Puts back a settings file left aside by a commit that never finished.
///
/// The rollback inside a commit only covers a rename that returned an error;
/// losing power between the two renames leaves the scratch copy holding the
/// only settings file on the watch, and every later launch refusing because it
/// cannot read one. This is the exception to writing nothing when saving is
/// off: it moves back a file this app moved, and only when the real one is
/// missing.
///
/// True if it recovered something.
bool recoverInterruptedCommit(SDK::Interface::IFileSystem &fs,
                              const SettingsAddresses::AddressSet &addrs, const Field &field);

/// Proves `addrs`'s File primitives are the functions they are supposed to be,
/// by exercising them against this app's own scratch paths: a path written
/// through setPath and read back out of the object, a file of known length
/// whose size field reads back, a content round-trip, and rename and delete
/// returning distinguishable answers for present and absent files.
/// `2:/settings.json` is never opened for writing by any of it.
///
/// This is a gate, not a diagnostic: an ABI is shared by every firmware
/// version that ships it, so the row ABI selected is only a candidate until
/// this returns true. Debug builds log every raw return value on the way.
bool validatePrimitives(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                        const Field &field);

} // namespace SettingsPersist

#endif // SETTINGS_PERSIST_HPP
