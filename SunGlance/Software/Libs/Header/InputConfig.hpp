/**
 ******************************************************************************
 * @file    InputConfig.hpp
 * @date    02-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Reader for a user-supplied value written into the app's own folder.
 ******************************************************************************
 *
 * A copy of `Barcode/Software/Libs/{Header,Sources}/InputConfig.{hpp,cpp}`,
 * unchanged except for this note and the default path. Copied rather than
 * shared: `Barcode` is published in Kira's catalogue, where a version's build
 * recipe is a commit and a subdirectory, and rearranging that app's source tree
 * to extract a common library would rewrite what its published versions claim
 * to have been built from for no gain to anybody. Two hundred lines is the
 * cheaper side of that trade. If a third app needs this, extract it then, in
 * the shape `MapKit` already uses for shared code.
 *
 * Divergence is the risk a copy carries, so: this file is a copy and not a
 * fork. A fix belongs in both.
 *
 * The watch has four buttons, so a value only the user can supply -- a parkrun
 * athlete id there, a home latitude and longitude here, but equally a transit
 * pass, a membership number or an account token -- has to arrive from outside.
 * The one path that works today is the USB mass-storage volume:
 * `Apps/<Folder>/` is writable from any desktop, and is the same directory
 * this app's own relative paths resolve into, so a file the host writes is a
 * file the app can open.
 *
 * The file's shape deliberately copies SDK::Variant::Config, which is the
 * SDK's existing bounded-JSON-config reader: a "schema" major that must match
 * exactly, a size ceiling checked before anything is allocated, an app-owned
 * subtree the shared reader treats as opaque, and a fall back to the caller's
 * default on every failure -- a config somebody else wrote must never stop the
 * app starting. Only the source differs: Variant reads an alias .uapp the
 * platform builds, this reads a plain file a person can type.
 *
 * Deliberately NOT settings.json. That file is the app's own, rewritten whole
 * on every save, so a key the app does not know about is destroyed the next
 * time the user touches a settings screen. Keeping externally-written data in
 * its own file also keeps "this came from outside, validate it" a property of
 * the filename rather than something to remember.
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

/// Schema major this build parses. An unknown major falls back entirely
/// rather than guessing at rearranged keys, the same rule
/// SDK::Variant::Config applies to its own "schema" field.
constexpr uint32_t kSchemaSupported = 1;

/// Refused before a single byte is allocated. The five SDK settings
/// serializers all `new char[file->size()]` with no ceiling, which turns any
/// oversized file dropped in an app folder into a bite out of a 256 KB app
/// budget. A provisioning file is tens of bytes; 4 KB is already generous.
constexpr size_t kMaxFileBytes = 4096;

/// Relative to the app's sandbox root -- `Apps/<Folder>/` as the USB volume
/// and the BLE file-transfer service see it. The name is Kira's convention for
/// a config it writes, and is what this app's registry manifest declares.
constexpr char kPath[] = "input.json";

/**
 * @brief Why there is, or is not, a usable file.
 *
 * Reported rather than collapsed to a bool so the app can tell the user which
 * of these it is: "you have not written the file yet" and "the file you wrote
 * is malformed" need different things done about them, and neither is
 * discoverable from a watch with no keyboard.
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
     * @brief Copy a string value out, bounded and validated.
     *
     * Rejects rather than truncates: a shortened id is a *wrong* id, and a
     * barcode that scans as someone else's athlete number is worse than no
     * barcode at all.
     *
     * @param query   coreJSON query, e.g. "values.id".
     * @param out     Receives a NUL-terminated copy; set empty on failure.
     * @param outSize Size of @p out including the terminator.
     * @retval true   A value was found, is printable ASCII, and fitted.
     */
    bool getString(const char *query, char *out, size_t outSize) const;

    /**
     * @brief Whether the query resolves at all, whatever the value looks like.
     *
     * Separate from getString() so a caller can tell "the file does not
     * mention this key" from "it does, and the value is unusable" -- an
     * incomplete file and a wrong one need different things said about them.
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
