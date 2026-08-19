/**
 ******************************************************************************
 * @file    Palette.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The 14 palette slots the watch cartography is specified in, and the
 *          four LUT variants built on top of them.
 ******************************************************************************
 *
 * Every value here is copied from `slippypack/MAP_CARTOGRAPHY_SPEC.md` § 3,
 * which derived them from a colorimetric model of the Sharp LS012B7DD06A
 * (experiment E1) rather than from the panel itself. **That is the reason this
 * app exists.** The L* figures below are a model's output, the contrast ratios
 * are arithmetic on that output, and until somebody looks at these codes on
 * glass, every cartographic judgement downstream of them is a judgement about
 * a spreadsheet.
 *
 * So: the numbers are carried here as data, `Cards.hpp` puts them on the
 * panel, and the README says what to look for. If the panel disagrees with the
 * model, this header is what changes, and the spec follows it.
 *
 * ---------------------------------------------------------------------------
 * The byte layout, confirmed against the spec's own table
 *
 * ABGR2222, one byte per pixel, MSB first: `A A B B G G R R`. Alpha is pinned
 * fully opaque in rawtiles v1, so every code used here has `A = 3` and the
 * meaningful part is the low six bits -- which is exactly what makes a
 * 64-entry LUT the right shape for a restyle (§ 9).
 *
 * Cross-check, from § 3's table: `wood_lt` is documented `r1 g3 b1` = 0xDD,
 * and 0xDD is `11 01 11 01` = A3 B1 G3 R1. `water` is documented `r0 g1 b3`
 * = 0xF4 = `11 11 01 00` = A3 B3 G1 R0. Both agree.
 *
 * ---------------------------------------------------------------------------
 * Pure: no SDK, no kernel, no TouchGFX. Host-tested in `MapLab/Tests`.
 ******************************************************************************
 */

#ifndef MAPLAB_PALETTE_HPP
#define MAPLAB_PALETTE_HPP

#include <cstdint>

namespace MapLab
{

/// Pack a colour into an ABGR2222 byte. Channels are 0..3; anything larger is
/// masked rather than clamped, because a caller passing 4 has a bug that a
/// silent clamp would hide until it reached the panel.
constexpr uint8_t abgr2222(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 3)
{
    return static_cast<uint8_t>(((a & 3) << 6) | ((b & 3) << 4) | ((g & 3) << 2) | (r & 3));
}

constexpr uint8_t redOf(uint8_t code)   { return static_cast<uint8_t>(code & 3); }
constexpr uint8_t greenOf(uint8_t code) { return static_cast<uint8_t>((code >> 2) & 3); }
constexpr uint8_t blueOf(uint8_t code)  { return static_cast<uint8_t>((code >> 4) & 3); }
constexpr uint8_t alphaOf(uint8_t code) { return static_cast<uint8_t>((code >> 6) & 3); }

/// Index of a code within a 64-entry LUT. Alpha is opaque throughout v1, so
/// the low six bits are the whole colour.
constexpr uint8_t lutIndex(uint8_t code) { return static_cast<uint8_t>(code & 0x3F); }

/// Entries in a restyle table. Named rather than 64 so a caller cannot pass a
/// table of the wrong length.
constexpr int kLutEntries = 64;

/**
 * @brief The specified slots. Order is the spec's table order.
 *
 * `Ink` and `Halo` are deliberately not separate codes -- the spec spends
 * 0xC0 on both `road_major` and label text, and makes the halo `paper`. They
 * are listed as their own names anyway, because a card that draws "text" and
 * a card that draws "the road you are on" are asking different questions of
 * the same byte.
 */
enum class Slot : uint8_t {
    Paper = 0,
    Landuse,
    WoodLight,
    Building,
    Wood,
    Water,
    Contour,
    WaterDark,
    Trace,
    RoadMinor,
    Path,
    RoadMajor,
    Count
};

/// One row of § 3's table, carried whole so a card can label what it draws.
struct SlotSpec {
    Slot        slot;
    uint8_t     code;
    const char* name;
    int16_t     lStarX100;   ///< Model's L*, x100. E1's output, not a measurement.
    int16_t     crVsPaperX10;///< Contrast ratio against `paper`, x10.
    const char* role;
};

/// The table, verbatim from MAP_CARTOGRAPHY_SPEC.md § 3.
inline constexpr SlotSpec kSlots[] = {
    { Slot::Paper,     0xFF, "paper",      10000,  10, "ground; also text halo" },
    { Slot::Landuse,   0xEE, "landuse",     9574,  11, "built-up wash" },
    { Slot::WoodLight, 0xDD, "wood_lt",     9112,  13, "large forest blocks" },
    { Slot::Building,  0xEA, "building",    8601,  15, "context only" },
    { Slot::Wood,      0xD8, "wood",        7732,  19, "strongest usable green" },
    { Slot::Water,     0xF4, "water",       7040,  28, "lake/sea fill" },
    { Slot::Contour,   0xC5, "contour",     6224,  41, "terrain lines" },
    { Slot::WaterDark, 0xF0, "water_dk",    5177,  50, "waterway lines" },
    { Slot::Trace,     0xC3, "trace",       5176,  50, "app-drawn GPS trace" },
    { Slot::RoadMinor, 0xC1, "road_minor",  3658, 107, "second ink, warm" },
    { Slot::Path,      0xD0, "path",        3658, 107, "second ink, cool" },
    { Slot::RoadMajor, 0xC0, "road_major",  2367, 250, "darkest thing on the map" },
};

static_assert(sizeof(kSlots) / sizeof(kSlots[0]) == static_cast<int>(Slot::Count),
              "kSlots must carry every slot exactly once");

/// The byte for a slot.
constexpr uint8_t code(Slot s) { return kSlots[static_cast<int>(s)].code; }

/// Label text and its halo, named for what they do rather than what they are.
constexpr uint8_t kInk   = 0xC0;
constexpr uint8_t kHalo  = 0xFF;

/**
 * @brief The four restyle variants of § 9, as 64-entry LUTs.
 *
 * E6 proved the mechanism in simulation from a single unmodified pack; the
 * per-frame cost on the real blit path is unmeasured, and measuring it is
 * bench R4. Nothing here is on the render path -- a LUT is built once and
 * applied to a finished canvas.
 *
 * Every table starts as identity, so a code the cartography does not use is
 * passed through untouched rather than mapped to something arbitrary. That
 * matters for a real pack: 50 of the 64 codes are deliberately unspent, and a
 * variant that collapsed them would be lying about what it changes.
 */
enum class Variant : uint8_t { Day = 0, Night, HighContrast, Trail, Count };

/// Fill `out` (64 entries) with the variant's table. Returns false, leaving
/// `out` identity, if the variant is out of range.
bool buildLut(Variant v, uint8_t out[kLutEntries]);

/// Name for a variant, for the card's caption and the log's column.
const char* variantName(Variant v);

} // namespace MapLab

#endif // MAPLAB_PALETTE_HPP
