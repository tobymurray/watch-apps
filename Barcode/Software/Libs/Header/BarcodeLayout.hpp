/**
 ******************************************************************************
 * @file    BarcodeLayout.hpp
 * @date    27-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Where the barcode goes, and what the display can actually hold.
 ******************************************************************************
 *
 * The numbers the screen is drawn from, plus the arithmetic that says whether
 * what they describe is a barcode a scanner can read. Pure integer maths, no
 * SDK and no TouchGFX, so the geometry can be argued about at a desk instead
 * of at a finish line.
 *
 * It exists because the two facts that decide whether this app works are both
 * facts about the panel, and neither was written down anywhere a test could
 * reach:
 *
 * - **The display is round.** A 240x240 framebuffer is addressable in full,
 *   but only the inscribed circle is lit. A rectangle's corners can sit
 *   outside it while its middle rows are fine, which is exactly the case for
 *   the white backing here -- see fitsInPanel() and the invariant the tests
 *   hold it to.
 * - **The display has four levels a channel.** 8bpp ABGR2222 through
 *   LCD8bpp_ABGR2222, software rendered. Anti-aliasing a bar edge has four
 *   greys to work with, so a bar boundary that lands mid-pixel does not
 *   soften, it steps. Bar edges therefore want to be whole pixels, which is
 *   what modulesAreWholePixels() asks.
 *
 * Quiet zones are the other half. ISO/IEC 15417 specifies the clear margin
 * either side of the symbol in *modules*, not pixels, and a module here is a
 * fraction of a pixel wide -- so a margin that looks generous in pixels can be
 * well under the standard. meetsQuietZone() compares the two in the unit the
 * standard uses.
 *
 * Nothing here decides anything on its own. It reports what the current
 * geometry achieves so the tests can state it, and so a change to the numbers
 * has to answer for what it does to scannability.
 *
 ******************************************************************************
 */

#ifndef BARCODELAYOUT_HPP
#define BARCODELAYOUT_HPP

#include <cstdint>

namespace BarcodeLayout
{

// ---------------------------------------------------------------------------
// The panel
//
// 240x240, 8bpp ABGR2222, software rendered -- SDK
// Docs/TouchGFX-Port-Architecture.md, and LCD8bpp_ABGR2222 in
// Libs/Source/Port/TouchGFX/generated/TouchGFXConfiguration.cpp.
// ---------------------------------------------------------------------------

constexpr int16_t kPanelWidth  = 240;
constexpr int16_t kPanelHeight = 240;

/// Levels per channel in ABGR2222. Two bits each, so four. This is the number
/// that makes anti-aliased bar edges step instead of blend, and it is a fact
/// about the glass and not just about the framebuffer: the panel is a Sharp
/// LS012B7DD06 (spec LD-29652B), whose overview states "1 pixel has RGB each
/// 2bit, the pixel can display 64 colors".
constexpr uint8_t kLevelsPerChannel = 4;

// ---------------------------------------------------------------------------
// The panel as a physical object
//
// From Table 3-1 of the LS012B7DD06 technical literature (Sharp, spec
// LD-29652B, rev 07-Aug-17), which is the board this watch carries -- see
// UNAview_LS012 in UNAWatch/una-hardware:
//
//   Screen size       1.19 inch
//   Active area       diameter 30.24 mm
//   Dot configuration 240 (H) x 240 (V)
//   Dot pitch         0.126 (H) x 0.126 (V) mm
//   Display mode      Normally Black, reflective with slight transmission
//
// The dot pitch is the number that decides whether a barcode can be read, and
// it is the one no amount of rendering work changes. It is also the number the
// pixel arithmetic elsewhere in this file cannot see: a module 1.6 *pixels*
// wide sounds ample and is 0.20 *millimetres* wide, which is at the very edge
// of what ISO/IEC 15417 contemplates.
//
// Being reflective rather than emissive cuts both ways for scanning: contrast
// in daylight is paper-like, which is ideal, and in a dim room without the
// front light there is nothing to read at all.
// ---------------------------------------------------------------------------

/// Dot pitch in microns. Exactly 126 per the datasheet, so no rounding to
/// carry: 240 * 126 um = 30.24 mm, which is the quoted active diameter.
constexpr int32_t kDotPitchMicrons = 126;

/// Active area diameter in microns, derived rather than restated so the two
/// cannot drift apart.
constexpr int32_t kActiveDiameterMicrons = kDotPitchMicrons * kPanelWidth;

/// ISO/IEC 15417 gives 0.19 mm as the smallest X-dimension for general use,
/// with 0.25 mm the floor for retail scanning. Below the first of these a
/// symbol is outside the standard whatever the rendering does about it.
constexpr int32_t kMinXDimensionMicrons    = 190;
constexpr int32_t kRetailXDimensionMicrons = 250;

// ---------------------------------------------------------------------------
// Where the barcode sits
//
// The white backing is the paper a printed barcode would be on -- the screen
// background is black, and a barcode needs light quiet zones. It is wider and
// taller than the bars on purpose: the overhang *is* the quiet zone.
// ---------------------------------------------------------------------------

constexpr int16_t kBackingX = 10;
constexpr int16_t kBackingY = 73;
constexpr int16_t kBackingW = 220;
constexpr int16_t kBackingH = 94;

constexpr int16_t kBarsX = 20;
constexpr int16_t kBarsY = 75;
constexpr int16_t kBarsW = 200;
constexpr int16_t kBarsH = 90;

/// White either side of the bars, in pixels. Not the same thing as the quiet
/// zone the standard asks for -- see meetsQuietZone().
constexpr int16_t kQuietPxEachSide = kBarsX - kBackingX;

/// ISO/IEC 15417: the clear margin each side of a Code 128 symbol is at least
/// ten times the narrow-element width.
constexpr uint16_t kQuietZoneModulesRequired = 10;

// ---------------------------------------------------------------------------
// The id, beneath the bars
//
// The human-readable half of the barcode, and the widest text on the face. Its
// box is sized to the *circle*, not to the framebuffer: the TouchGFX Designer
// gave it 203px at x=19, which is 203px of a square, and the bezel took the
// ends off a long id. See the tables in the README.
//
// kIdInkLimit and kIdW are one decision in two numbers and have to move
// together: above the limit the 20pt face is swapped for the 18pt one, and
// above what 18pt can fit the id is split over two lines, so kIdW only ever
// clips an id that beats both.
// ---------------------------------------------------------------------------

constexpr int16_t kIdX = 27, kIdY = 178, kIdW = 187, kIdH = 30;
constexpr uint16_t kIdInkLimit = 186;

/// The two rows a split id uses instead of kIdY, 18px apart.
constexpr int16_t kIdLine1Y = 169, kIdLine2Y = 187, kIdLineH = 18;

// ---------------------------------------------------------------------------
// The pager marks
//
// One per code, below the id: kMarkW wide on a kMarkPitch grid, centred. They
// replaced a fraction appended to the caption in 0.3.4.
// ---------------------------------------------------------------------------

constexpr int16_t kMarkW = 8, kMarkH = 4, kMarkPitch = 15, kMarkY = 212;

/// Width a row of @p count marks occupies, and where it starts.
constexpr int16_t markRowWidth(int16_t count)
{
    return count < 1 ? 0 : static_cast<int16_t>((count - 1) * kMarkPitch + kMarkW);
}
constexpr int16_t markRowLeft(int16_t count)
{
    return static_cast<int16_t>((kPanelWidth - markRowWidth(count)) / 2);
}

// ---------------------------------------------------------------------------
// The round panel
//
// Worked in doubled coordinates so the centre of an even-sized panel lands on
// an integer: for 240 px the centre is 119.5, which doubles to 239, and the
// radius doubles to the same. No floating point and no sqrt, so all of this is
// constexpr and exact.
//
// The radius is a deliberately conservative choice, not a measurement. It puts
// the boundary at 119.5 dot pitches, half a pitch inside the datasheet's
// ⌀30.24 mm active area -- which is exactly 240 pitches, so a circle inscribed
// in the dot grid's bounding square. Under this model a pixel counts as lit
// only if its *centre* is inside, which makes the widest row 238 px and column
// 0 dark everywhere; under the datasheet's diameter the outermost pixel of the
// centre row has its centre inside the circle but part of its area outside.
// Neither the datasheet nor anything in this repository settles whether such a
// pixel appears lit, and the bezel overlaps the glass besides, so this file
// takes the pessimistic reading and pays one column each side for it.
//
// Nothing currently depends on which is right: the id row, the bars and the
// pager marks were all measured against both a 119.5- and a 120-pitch mask and
// put zero ink outside either.
// ---------------------------------------------------------------------------

constexpr int32_t kDoubledCentreX = kPanelWidth - 1;
constexpr int32_t kDoubledCentreY = kPanelHeight - 1;
constexpr int32_t kDoubledRadius  = kPanelWidth - 1;

/// @retval true The pixel is inside the lit circle.
constexpr bool pixelIsLit(int32_t x, int32_t y)
{
    const int32_t dx = 2 * x - kDoubledCentreX;
    const int32_t dy = 2 * y - kDoubledCentreY;
    return dx * dx + dy * dy <= kDoubledRadius * kDoubledRadius;
}

/// @retval true Every pixel of row @p y from @p x to @p x + @p w - 1 is lit.
///
/// Only the ends are checked, which is sufficient: a horizontal run's furthest
/// points from the centre are its ends.
constexpr bool rowIsLit(int32_t x, int32_t y, int32_t w)
{
    return w > 0 && pixelIsLit(x, y) && pixelIsLit(x + w - 1, y);
}

/// @retval true All four corners of the rectangle are lit.
///
/// False does not mean "invisible" -- it means the corners are cut, which for
/// a backing rectangle may be perfectly fine. What matters is whether the
/// rows that carry meaning are whole, which is a separate question; see the
/// tests.
constexpr bool fitsInPanel(int32_t x, int32_t y, int32_t w, int32_t h)
{
    return w > 0 && h > 0
        && pixelIsLit(x, y) && pixelIsLit(x + w - 1, y)
        && pixelIsLit(x, y + h - 1) && pixelIsLit(x + w - 1, y + h - 1);
}

// ---------------------------------------------------------------------------
// Is it scannable?
//
// Parameterised on the module count rather than on the id, so a caller has to
// get the count from Code128::encode() and the answer stays tied to what will
// actually be drawn instead of to a second opinion about symbol arithmetic.
// ---------------------------------------------------------------------------

/**
 * @brief What a given module count comes out as on this screen.
 *
 * A module is `barsWidthPx / totalModules` pixels wide, which is virtually
 * never a whole number. Rather than round it away, the ratio is kept intact
 * and every question is asked as integer arithmetic on it -- so nothing here
 * quietly agrees that a 1.6 px module is 2 px.
 */
struct Scannability
{
    uint16_t totalModules  = 0;
    int16_t  barsWidthPx   = kBarsW;
    int16_t  quietPxPerSide = kQuietPxEachSide;

    /// Module width in hundredths of a pixel. Reporting only -- no decision
    /// below is taken on this rounded value.
    constexpr uint16_t moduleWidthCentipx() const
    {
        return totalModules == 0
                   ? 0
                   : static_cast<uint16_t>((static_cast<int32_t>(barsWidthPx) * 100) / totalModules);
    }

    /// The quiet zone expressed in modules, in hundredths. Reporting only.
    constexpr uint16_t quietZoneCentimodules() const
    {
        return barsWidthPx == 0
                   ? 0
                   : static_cast<uint16_t>(
                         (static_cast<int32_t>(quietPxPerSide) * totalModules * 100) / barsWidthPx);
    }

    /**
     * @brief Does the white margin meet ISO/IEC 15417's ten modules?
     *
     * Cross-multiplied rather than compared as a width in pixels, because the
     * module width is a ratio and dividing first is what makes a short quiet
     * zone look adequate.
     */
    constexpr bool meetsQuietZone() const
    {
        return static_cast<int32_t>(quietPxPerSide) * totalModules
            >= static_cast<int32_t>(kQuietZoneModulesRequired) * barsWidthPx;
    }

    /**
     * @brief Does every module boundary land on a pixel boundary?
     *
     * When it does not, bar edges are anti-aliased, and on a four-level
     * channel that is a visible step rather than a soft edge. This is the
     * question the renderer has to answer, not a cosmetic one.
     */
    constexpr bool modulesAreWholePixels() const
    {
        return totalModules != 0 && (barsWidthPx % totalModules) == 0;
    }

    /// Narrow element in whole pixels, rounded down -- what a scanner has to
    /// resolve in the worst case.
    constexpr uint16_t narrowElementPx() const
    {
        return totalModules == 0 ? 0 : static_cast<uint16_t>(barsWidthPx / totalModules);
    }

    /**
     * @brief The X-dimension in microns: the module width as a physical size.
     *
     * The number that actually decides whether a scanner can read this, and the
     * one no amount of rendering work changes. A module is
     * barsWidthPx/totalModules pixels, each kMicronsPerPixelCenti/100 microns
     * wide; done in one expression so the ratio is never rounded mid-way.
     */
    constexpr int32_t xDimensionMicrons() const
    {
        return totalModules == 0
                   ? 0
                   : (static_cast<int32_t>(barsWidthPx) * kDotPitchMicrons)
                         / static_cast<int32_t>(totalModules);
    }

    /// @retval true The X-dimension reaches ISO/IEC 15417's 0.19mm general
    ///              minimum. False means the symbol is outside the standard --
    ///              it may still read on a good camera, but nothing in the
    ///              rendering can be blamed if it does not.
    constexpr bool meetsMinimumXDimension() const
    {
        return xDimensionMicrons() >= kMinXDimensionMicrons;
    }

    /// @retval true The X-dimension reaches the 0.25mm retail floor.
    constexpr bool meetsRetailXDimension() const
    {
        return xDimensionMicrons() >= kRetailXDimensionMicrons;
    }
};

/// The current layout's verdict for a symbol of @p totalModules modules.
constexpr Scannability scannabilityFor(uint16_t totalModules)
{
    Scannability s{};
    s.totalModules = totalModules;
    return s;
}

} // namespace BarcodeLayout

#endif // BARCODELAYOUT_HPP
