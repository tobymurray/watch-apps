/**
 ******************************************************************************
 * @file    SettingsFile.hpp
 * @brief   Reads and patches the real, watch-wide notifications flag in the
 *          system settings.json -- not an app's own SDK::AppConfig file.
 ******************************************************************************
 *
 * settings.json lives one directory above "Apps/" (`../../settings.json`
 * relative to this app's own sandboxed directory: one `..` reaches "Apps/",
 * a second reaches the volume root). `SDK::Kernel::fs` places no wall there
 * -- Libs/Source/Simulator/Kernel/Mock/FileSystem.cpp resolves every path by
 * plain string concatenation, and RustGuiPoc/Docs/FINDINGS.md documents this
 * exact one-level-up escape for a sibling "SharedData" directory -- but nothing
 * in this app assumes that further than this one, specific, verified path.
 *
 * Every function here does only what SettingsPatch.hpp's pure logic already
 * proved safe on the exact bytes pulled off the watch: open, read the whole
 * file into a bounded buffer, hand it to SettingsPatch, and (for a write)
 * stage the result in a temp file, read that back and byte-compare it against
 * what was meant to be written, and only then swap it in. If any step is not
 * exactly as expected, nothing is written and the real file is left alone.
 ******************************************************************************
 */

#ifndef SETTINGS_FILE_HPP
#define SETTINGS_FILE_HPP

#include "SDK/Interfaces/IFileSystem.hpp"

namespace SettingsFile
{

enum class Status {
    Ok,             ///< outEnabled reflects the real, current on-disk value.
    NoChange,       ///< Write only: the file already said what was asked; nothing touched.
    OpenFailed,     ///< settings.json could not be found or opened for reading.
    TooLarge,       ///< settings.json is larger than this code trusts itself to hold whole.
    ReadFailed,     ///< The read did not return the number of bytes the file reported.
    NotFound,       ///< The `"phone":{"notifications":...}` literal was not present exactly once.
    WriteFailed,    ///< Staging the patched document to a temp file failed.
    VerifyFailed,   ///< The temp file, read back, did not match what was meant to be written.
    SwapFailed,     ///< Replacing settings.json with the verified temp file failed.
};

/// Bare filename cap on how large settings.json is trusted to be. The real
/// file observed on the watch is ~250 bytes; this is a wide margin, not a
/// tuned limit -- anything past it means "this is probably not the file this
/// code thinks it is", and the honest response is to refuse it, not to guess.
constexpr size_t kMaxTrustedFileBytes = 2048;

/**
 * @brief   Read the real notifications flag. Opens the file read-only; never
 *          opens it for writing.
 */
Status readNotificationsFlag(SDK::Interface::IFileSystem &fs, bool &outEnabled);

/**
 * @brief   Set the real notifications flag, if it isn't already that value.
 * @param   outEnabled: On Status::Ok or Status::NoChange, the value now
 *          confirmed on disk (re-read after writing, not merely assumed).
 *
 * On any failure, settings.json is exactly as it was before this call --
 * see the file header for the staged-write-then-verify-then-swap sequence
 * that guarantees this.
 */
Status writeNotificationsFlag(SDK::Interface::IFileSystem &fs, bool newEnabled, bool &outEnabled);

} // namespace SettingsFile

#endif // SETTINGS_FILE_HPP
