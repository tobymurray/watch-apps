/**
 ******************************************************************************
 * @file    InputConfig.hpp
 * @date    04-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Reader for user-supplied values written into the app's own folder.
 ******************************************************************************
 *
 * The watch has four buttons, so anything only the user can decide has to
 * arrive from outside. The one path that works today is the USB mass-storage
 * volume: `Apps/<Folder>/` is writable from any desktop, and is the same
 * directory this app's own relative paths resolve into, so a file the host
 * writes is a file the app can open.
 *
 * Here that value is whether to record the raw IMU stream. It cannot be a
 * setting on the watch without a TouchGFX Designer change, and it must not live
 * in settings.json: that file is the app's own, rewritten whole on every save,
 * so a key the app did not put there is destroyed the next time somebody
 * touches a settings screen. 1.0.0 shipped with the flag in settings.json and
 * was unusable for exactly that reason -- an install had no such file, and
 * nothing but hand-editing could create one.
 *
 * The file's shape deliberately copies SDK::Variant::Config, the SDK's own
 * bounded-JSON-config reader: a "schema" major that must match exactly, a size
 * ceiling checked before anything is allocated, an app-owned subtree the shared
 * reader treats as opaque, and a fall back to the default on every failure -- a
 * config somebody else wrote must never stop the app starting.
 *
 * Ported from the Barcode app, which reads an athlete id the same way. The two
 * copies are independent on purpose: an app root has to be self-contained for
 * Kira to build it from `subdir` alone. Worth making shared if a third app
 * needs it.
 *
 ******************************************************************************
 */

#ifndef INPUTCONFIG_HPP
#define INPUTCONFIG_HPP

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <memory>

#include "SDK/Kernel/Kernel.hpp"

namespace InputConfig
{

/// Schema major this build parses. An unknown major falls back entirely rather
/// than guessing at rearranged keys, the same rule SDK::Variant::Config applies
/// to its own "schema" field.
constexpr uint32_t kSchemaSupported = 1;

/// Refused before a single byte is allocated. The SDK's settings serializers
/// all `new char[file->size()]` with no ceiling, which turns any oversized file
/// dropped in an app folder into a bite out of the app's memory budget. A
/// provisioning file is tens of bytes; 4 KB is already generous.
constexpr size_t kMaxFileBytes = 4096;

/// Relative to the app's sandbox root -- `Apps/<Folder>/` as the USB volume and
/// the BLE file-transfer service see it.
constexpr char kPath[] = "input.json";

/// Query for the raw-IMU recording flag. Kept here so the app and its tests
/// cannot disagree with each other about the key, and so the one place to look
/// when reconciling with the Kira manifest's `config.fields[].path` is obvious.
constexpr char kQueryRecordImu[] = "values.record_imu";

/**
 * @brief Why there is, or is not, a usable file.
 *
 * Reported rather than collapsed to a bool because "you have not written the
 * file yet" and "the file you wrote is malformed" need different things done
 * about them, and neither is discoverable from a watch with no keyboard.
 */
enum class Status : uint8_t {
    Absent,      ///< No file. Nothing has been provisioned yet.
    TooLarge,    ///< Over kMaxFileBytes; refused before allocating.
    Unreadable,  ///< Present, but the read failed or memory ran out.
    NotJson,     ///< Empty, or coreJSON rejected it.
    WrongSchema, ///< No "schema", or a major this build does not parse.
    Ok,          ///< Parsed. Individual keys may still be absent.
};

/**
 * @brief Bounded reader over the provisioning file.
 *
 * Construction touches nothing; the first refresh() does the reading. That is
 * not fastidiousness about constructors doing I/O -- reading here can log, and
 * the simulator builds the app's objects before TouchGFX exists while its
 * logger writes through touchgfx_printf, so a constructor that logs takes the
 * process down. Reading on demand keeps this usable from either harness.
 *
 * After the first call, refresh() re-reads only when something outside has
 * changed the file, which is the closest thing to a change notification this
 * platform has -- no message type tells an app that a file it does not own was
 * rewritten.
 */
class Reader
{
public:
    explicit Reader(const SDK::Kernel &kernel, const char *path = kPath);

    /**
     * @brief Re-read if the file's (size, mtime) differs from the last read.
     *
     * Compared for *difference*, never for "newer": over USB mass storage the
     * host's clock stamps the write, not the watch's, so an ordering
     * comparison would be meaningless. Any difference means re-read.
     *
     * @retval true  The file changed and was re-read; the value may differ.
     * @retval false Nothing outside has touched it; no I/O beyond one stat.
     */
    bool refresh();

    Status status() const { return mStatus; }

    /**
     * @brief Read a flag whose value is written as a word.
     *
     * Kira's config form has exactly one field type and it writes strings, so
     * a boolean arrives as text. Accepted as true, case-insensitively: "on",
     * "yes", "true", "1", "enabled".
     *
     * Everything else is false, including an unrecognised word: refusing is the
     * conservative direction for a flag whose only effect is to start consuming
     * flash, and the form's help text names the value to type. An unrecognised
     * word is logged so it is at least visible in the simulator.
     */
    bool getFlag(const char *query) const;

    /**
     * @brief Copy a string value out, bounded and validated.
     *
     * Rejects rather than truncates: a value silently cut short is a different
     * value, not a shorter one.
     *
     * @param query   coreJSON query, e.g. "values.record_imu".
     * @param out     Receives a NUL-terminated copy; set empty on failure.
     * @param outSize Size of @p out including the terminator.
     * @retval true   A value was found, is printable ASCII, and fitted.
     */
    bool getString(const char *query, char *out, size_t outSize) const;

    /**
     * @brief Whether the query resolves at all, whatever the value looks like.
     *
     * Separate from getString() so a caller can tell "the file does not mention
     * this key" from "it does, and the value is unusable" -- an incomplete file
     * and a wrong one need different things said about them.
     */
    bool has(const char *query) const;

private:
    const SDK::Kernel &mKernel;
    const char        *mPath;

    Status mStatus = Status::Absent;

    /// Last-seen file identity. See refresh() for why mtime is only ever
    /// compared for equality.
    bool   mPresent   = false;
    size_t mStampSize = 0;
    time_t mStampUtc  = 0;

    std::unique_ptr<char[]> mJson;
    size_t                  mJsonLen = 0;

    /// Read and parse whatever the current stamp describes.
    void load();

    /// @retval true The file exists; @p size and @p utc describe it.
    bool statFile(size_t &size, time_t &utc) const;
};

} // namespace InputConfig

#endif // INPUTCONFIG_HPP
