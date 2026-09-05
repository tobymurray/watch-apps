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

/// What a scanner has to resolve, as reference points -- *not* as a standard.
///
/// ISO/IEC 15417 does not set a minimum X-dimension for Code 128. Its scope
/// says it specifies "...dimensions, decoding algorithms and the parameters to
/// be defined by applications", and X-dimension is one of those application
/// parameters. An earlier version of this file claimed a 0.19 mm ISO floor and
/// was wrong: that figure is not in the symbology standard.
///
/// So the honest question is not "does this meet the standard" but "will the
/// scanner in front of me resolve it". Two sourced reference points:
///
///   127 um (5 mil)  Code 128 is a mid-density symbology and scanner-selection
///                   guidance puts it at 5-10 mil capability, so 5 mil is a
///                   reasonable "any scanner should manage this" line.
///    76 um (3 mil)  The narrow-element minimum on the spec sheet for Zebra's
///                   LS2208 -- a cheap, very widely deployed laser scanner.
///                   Quoted for Code 39, which is the same narrow element.
///
/// Both are resolution figures only. They say nothing about contrast, specular
/// glare, or whether a given scanner will read a screen at all, and those are
/// the things most likely to actually decide it on a reflective LCD.
constexpr int32_t kMidDensityScannerMicrons  = 127;
constexpr int32_t kAggressiveScannerMicrons  = 76;

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
// ITF, which is drawn in the same band by different rules
//
// Two differences from Code 128, both consequences of this panel rather than
// of the symbology:
//
// **Whole pixels.** An ITF element is a whole number of pixels and the symbol
// is centred in what that leaves, instead of being stretched to fill the band.
// Four levels a channel means an edge landing mid-pixel steps rather than
// blends, and a decoder tells a narrow element from a wide one by comparing
// widths -- so an exact 3:1 with hard edges is worth more here than the ~20%
// of width that rounding down costs. The remainder is not wasted: it goes to
// the quiet zone, which was already the tighter constraint.
//
// **Bearer bars.** ITF is continuous and has no check character, so a scan
// that clips the top or bottom of the symbol can decode as a valid *shorter*
// number -- the "plausible value that scans" failure Barcode.hpp is about. A
// bar flush above and below the symbol means such a scan crosses solid ink and
// fails instead. This is a correctness requirement for ITF, not decoration,
// and it is the reason ITF's bars are shorter than Code 128's.
// ---------------------------------------------------------------------------

/// ISO/IEC 16390: the clear margin each side of an ITF symbol is at least ten
/// narrow elements, the same rule Code 128 states in modules.
constexpr uint16_t kItfQuietUnitsRequired = 10;

/// Widest an ITF element may be drawn. Bounded so the bearer bars and the
/// symbol always fit the band whatever the id; the shortest id this app
/// accepts, two digits, would otherwise take a 7px element.
constexpr int16_t kItfMaxUnitPx = 4;

/// Pixels per narrow element for a symbol of @p units units, rounded **down**
/// so every element is whole. Never zero: one pixel is the floor, and from ten
/// digits up it is what the white allows.
///
/// Derived from the **backing** and not from the bars band, because the symbol
/// and its quiet zone have to fit the white together -- outside the backing the
/// screen is black, and a quiet zone has to be light to be a quiet zone. So the
/// budget is kBackingW for `units + 2 * kItfQuietUnitsRequired` elements, not
/// kBarsW for `units`.
///
/// Sizing it the other way looks right and is not: it puts a 4-digit symbol at
/// 3px an element with 20px of white either side where the standard wants 30,
/// and fails the same way at 6 and 10 digits. itfMeetsQuietZone() is the test
/// that catches it, and it caught exactly that.
constexpr int16_t itfUnitPx(uint16_t units)
{
    return units == 0 ? 0
         : (kBackingW / (units + 2 * kItfQuietUnitsRequired)) < 1 ? 1
         : (kBackingW / (units + 2 * kItfQuietUnitsRequired)) > kItfMaxUnitPx ? kItfMaxUnitPx
         : static_cast<int16_t>(kBackingW / (units + 2 * kItfQuietUnitsRequired));
}

/// Drawn width of a symbol of @p units units, and where it starts so that it
/// is centred on the panel rather than on the band.
constexpr int16_t itfWidthPx(uint16_t units)
{
    return static_cast<int16_t>(units * itfUnitPx(units));
}
constexpr int16_t itfLeftPx(uint16_t units)
{
    return static_cast<int16_t>((kPanelWidth - itfWidthPx(units)) / 2);
}

/// Quiet zone the drawn symbol actually gets, in pixels each side, measured to
/// the edge of the white backing. Wider than Code 128's fixed 10px whenever
/// the rounding leaves anything over, which is most lengths.
constexpr int16_t itfQuietPx(uint16_t units)
{
    return static_cast<int16_t>(itfLeftPx(units) - kBackingX);
}

/// @retval true The quiet zone meets the ten narrow elements ISO/IEC 16390
///              asks for. Compared in elements, not pixels, because that is
///              the unit the rule is written in.
constexpr bool itfMeetsQuietZone(uint16_t units)
{
    return itfQuietPx(units) >= static_cast<int16_t>(kItfQuietUnitsRequired) * itfUnitPx(units);
}

/// Bearer bar thickness for a symbol drawn at @p unitPx pixels an element.
/// Two elements is the usual minimum for a top-and-bottom bearer; the 4px
/// floor keeps it visible when an element is a single pixel.
constexpr int16_t itfBearerPx(int16_t unitPx)
{
    return unitPx * 2 < 4 ? 4 : static_cast<int16_t>(unitPx * 2);
}

/// Height left for the bars once both bearers are taken out of the band.
constexpr int16_t itfBarsHeightPx(int16_t unitPx)
{
    return static_cast<int16_t>(kBarsH - 2 * itfBearerPx(unitPx));
}

// ---------------------------------------------------------------------------
// The caption, above the bars
//
// This code's name, in the band between the button ticks the bezel container
// draws down either side. It lives here rather than in MainView.cpp because a
// QR symbol has to be laid out against it -- see below, where it turns out
// there is no room for both.
// ---------------------------------------------------------------------------

constexpr int16_t kCaptionX = 40, kCaptionY = 48, kCaptionW = 160, kCaptionH = 24;

// ---------------------------------------------------------------------------
// Where a QR symbol sits
//
// Square, so it is a much worse fit for this panel than the bars are, and the
// arithmetic below is the whole reason Docs/QR.md fixes the version at 2.
//
// The largest square of lit pixels anywhere on the panel is 168 px. That is
// not the number that binds: the id row starts at kIdLine1Y, so a symbol that
// leaves the human-readable id alone must end above it, and the largest
// centred square that does is **144 px**. A QR module has to be a whole number
// of pixels for the same four-grey-levels reason modulesAreWholePixels() asks
// of the bars, so the achievable sizes are the version's module count plus its
// mandatory 4-module quiet zone each side, times a whole number of pixels:
//
//   version 1, 21 + 8 = 29 modules   4 px -> 116 px      5 px -> 145 px
//   version 2, 25 + 8 = 33 modules   4 px -> 132 px      5 px -> 165 px
//   version 3, 29 + 8 = 37 modules   4 px -> 148 px
//
// **A 5 px module misses by one pixel row.** 145 px needs its bottom edge at
// y = 169 and 169 is where the id starts. That is worth stating rather than
// rounding away: reclaiming those three pixels means re-opening a layout whose
// numbers came from measuring rendered ink, not from arithmetic, and which has
// clipped on device twice. So the module is 4 px, and version 3 does not fit.
//
// Every size that does fit overlaps the caption band, so **a QR code shows no
// name**. The id text and the pager marks stay.
//
// The 4-module quiet zone is inside the square: the white backing *is* the
// quiet zone, exactly as it is for the bars, so 16 px of the 132 is margin on
// each side and the dark modules occupy the middle 100 px.
// ---------------------------------------------------------------------------

/// ISO/IEC 18004 requires four modules of light on every side of a QR symbol.
constexpr uint8_t kQrQuietModules = 4;

/// Whole pixels per module. See the table above for why it is not 5.
constexpr int16_t kQrModulePx = 4;

/// Modules a side for the version this app draws, excluding the quiet zone.
constexpr int16_t kQrModules = 25;

constexpr int16_t kQrSide = (kQrModules + 2 * kQrQuietModules) * kQrModulePx;
constexpr int16_t kQrX    = (kPanelWidth - kQrSide) / 2;
constexpr int16_t kQrY    = 33;

/// Where the dark modules start, inset by the quiet zone.
constexpr int16_t kQrInkX = kQrX + kQrQuietModules * kQrModulePx;
constexpr int16_t kQrInkY = kQrY + kQrQuietModules * kQrModulePx;

/**
 * @brief What a QR module comes out as on this screen.
 *
 * Deliberately not Scannability: that struct answers questions about a linear
 * symbology -- quiet zones in modules, whether a module boundary lands on a
 * pixel -- and for a matrix symbology at a whole-pixel module most of them are
 * either constant or meaningless. What is left is the one number that decides
 * whether a scanner can resolve it.
 */
struct QrScannability
{
    int16_t modulePx = kQrModulePx;

    /// The module as a physical size. The only figure here that a scanner cares
    /// about, and the one no amount of rendering work changes.
    constexpr int32_t moduleMicrons() const
    {
        return static_cast<int32_t>(modulePx) * kDotPitchMicrons;
    }

    /// The whole symbol, quiet zone included, in microns.
    constexpr int32_t symbolMicrons() const
    {
        return static_cast<int32_t>(kQrModules + 2 * kQrQuietModules) * moduleMicrons();
    }
};

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

    /**
     * @brief Is every module at least a whole pixel wide?
     *
     * The line this panel actually draws cleanly either side of, and the one
     * a long id crosses first. Below a pixel a narrow bar cannot own a column
     * outright, so the renderer's four grey levels quantise it to 170,170,170
     * and the space beside it fills in -- a bar that is there in the widths
     * and not there on the glass.
     *
     * Measured by rendering sampled ids through the real renderer and reading
     * the ink back out of the framebuffer, 400 ids per module count:
     *
     *   <= 200 modules  (>= 1.000 px)  0 of 4800 ids had a faint bar or a
     *                                  filled space
     *      211 modules  (0.948 px)     317 of 400 faint, 207 of 400 filled
     *      222 modules  (0.901 px)     379 of 400 faint, 375 of 400 filled
     *
     * The cliff is that sharp because it is the pixel grid. It coincides with
     * the resolution one: a module is a pixel is kDotPitchMicrons, so 200
     * modules is 126 um, a micron under the 5 mil reference point above.
     *
     * Not a refusal anywhere in this app. A dense id is stored, encoded and
     * drawn; the GUI shows what this predicate found and the wearer decides
     * whether their scanner agrees. Rerun Tests/Layout_test.cpp's
     * drawability cases to falsify the boundary.
     */
    constexpr bool modulesAreAtLeastOnePixel() const
    {
        return totalModules != 0 && totalModules <= static_cast<uint16_t>(barsWidthPx);
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

    /// @retval true A scanner that resolves @p scannerMicrons can resolve this
    ///              symbol's narrow element. Parameterised rather than fixed,
    ///              because there is no standard number to compare against --
    ///              see the reference points above.
    constexpr bool resolvableAt(int32_t scannerMicrons) const
    {
        return xDimensionMicrons() >= scannerMicrons;
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
