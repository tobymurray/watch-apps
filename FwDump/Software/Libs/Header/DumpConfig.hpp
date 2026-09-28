/**
 ******************************************************************************
 * @file    DumpConfig.hpp
 * @brief   Optional override of what to dump, from a file in the app's folder.
 ******************************************************************************
 *
 * The default region -- 4 MB of internal flash -- needs no configuration, and
 * the app ships working without this file. What it buys is a smaller window of
 * flash, or a different chunk size, without rebuilding. A window outside flash
 * is refused: see DumpRegion::knownReadable().
 *
 * The file is `fwdump.json`, a bare relative name, so it resolves into the
 * app's own sandbox folder -- the same directory the USB-MSC volume exposes,
 * which is what makes it writable from a desktop at all. It is the envelope of
 * the SDK's Docs/app-config-fields.md, which a companion app writes from the
 * fields app-manifest.json declares:
 *
 *     {
 *       "schema": 1,
 *       "values": { "base": "08060000", "size": "00100000" }
 *     }
 *
 * Read here rather than through SDK::AppConfig, which does not exist in the
 * SDK 1.3 this app builds against. A file with no "values" object is read with
 * the same keys at the top level, which is the form 1.0.0 documented.
 *
 * Addresses are hex strings without `0x`, matching the manifest's own notation
 * and sidestepping the fact that JSON has no hex literal -- writing 0x08000000
 * as a decimal number is how someone ends up dumping the wrong region.
 *
 * Every failure falls back to the built-in default rather than refusing to run.
 * A dumper that will not start because a config file has a typo is worse than
 * one that dumps flash: flash is what all but one user of this will want, and
 * the status the reader reports lets the screen say the file was ignored.
 *
 * **The override does not make the app write anything.** A configured region is
 * read exactly as flash is -- see FlashDumper's class comment, and the
 * read-only guarantee in the README.
 *
 ******************************************************************************
 */

#ifndef DUMP_CONFIG_HPP
#define DUMP_CONFIG_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "DumpRegion.hpp"

namespace DumpConfig
{

/// Schema major this build understands. An unknown major is ignored wholesale
/// rather than read key by key, the same rule SDK::Variant::Config and
/// Barcode's InputConfig apply -- a rearranged schema read with old
/// expectations is how you dump the wrong addresses and believe the manifest.
constexpr uint32_t kSchemaSupported = 1;

/// Refused before anything is allocated. A region config is a few tens of
/// bytes; 4 kB is already far more than it can need, and the ceiling keeps a
/// large file dropped into the app folder from taking a bite out of the app's
/// memory budget.
constexpr size_t kMaxFileBytes = 4096;

/// Sandbox-relative, as the USB volume sees it.
constexpr char kPath[] = "fwdump.json";

/// Why the region is what it is. Reported rather than collapsed to a bool so
/// the screen can distinguish "you never wrote a config" from "the config you
/// wrote is wrong", which need different things done about them and neither of
/// which is discoverable from a watch.
enum class Status : uint8_t {
    Default,       ///< No file, or one that asks for the built-in flash region.
    Ok,            ///< File read and applied, and it differs from the default.
    TooLarge,      ///< Larger than kMaxFileBytes; not read.
    NotJson,       ///< Present but not parseable as JSON.
    WrongSchema,   ///< Parsed, but its "schema" is not kSchemaSupported.
    BadField,      ///< A field was present but not a valid hex number.
    BadGeometry,   ///< Parsed and read, but the region fails DumpRegion::valid().
    Unproven,      ///< Coherent, but outside DumpRegion::knownReadable().
};

/**
 * @brief The region to dump, and where it came from.
 *
 * On any status but Ok, `region` is the built-in default -- always valid, and
 * always the flash of this watch.
 */
struct Result {
    DumpRegion region{};
    Status     status = Status::Default;
};

/**
 * @brief Read `fwdump.json` from the app's folder, if it is there.
 *
 * Never throws, never leaves the region invalid, and never partially applies a
 * file: a config whose geometry does not hold is discarded entirely rather than
 * contributing its individually-plausible fields, because a half-applied
 * geometry is exactly the kind that tiles the region wrongly.
 */
Result load(const SDK::Kernel& kernel);

/// Short, screen-sized description of a status. Inline so the GUI, which does
/// not link this reader, can show it too.
inline const char* describe(Status status)
{
    switch (status) {
        case Status::Default:     return "default region";
        case Status::Ok:          return "config applied";
        case Status::TooLarge:    return "config too large";
        case Status::NotJson:     return "config not JSON";
        case Status::WrongSchema: return "config schema wrong";
        case Status::BadField:    return "config field invalid";
        case Status::BadGeometry: return "config does not tile";
        case Status::Unproven:    return "config not in flash";
    }
    return "config unknown";
}

} // namespace DumpConfig

#endif // DUMP_CONFIG_HPP
