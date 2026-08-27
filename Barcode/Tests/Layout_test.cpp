/**
 ******************************************************************************
 * @file    Layout_test.cpp
 * @brief   The barcode, held to what this display can actually do.
 ******************************************************************************
 *
 * Two properties of the panel decide whether this app works, and neither is
 * negotiable by the app:
 *
 * **It is round.** The framebuffer is 240x240 and every pixel is addressable,
 * but only the inscribed circle is lit. So "fits on screen" is a question
 * about a circle, and a rectangle can be fine in the middle and cut at the
 * corners. The white backing used to be exactly that case; since 0.3.1 it fits
 * whole, and the tests below hold it there. The id beneath the bars is the
 * same problem one row down and was got wrong twice, so it has tests of its
 * own now.
 *
 * **It has four levels a channel.** 8bpp ABGR2222. Anti-aliasing a bar edge
 * has four greys to work with, so a bar boundary that lands mid-pixel does not
 * blur, it steps -- and a stepped edge on a 1.6 px bar is what a scanner has to
 * make sense of. The module-width tests record where that currently stands.
 *
 * The scannability tests are deliberately split into two kinds, and the
 * difference matters when reading a failure:
 *
 *   - **Invariants** -- things that are true now and must stay true. A failure
 *     is a regression.
 *   - **Characterisations** -- things that are true now and *should not be*.
 *     They are named CurrentlyX and carry the standard they fall short of, so
 *     the shortfall is a number in a test rather than a worry in a README. A
 *     failure means somebody improved it, and the test should be tightened to
 *     match.
 *
 * Nothing in this file has seen a panel. See Tests/README.md for what that
 * does and does not buy.
 *
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "BarcodeLayout.hpp"
#include "Code128.hpp"

namespace {

using BarcodeLayout::Scannability;

/// Module count for an id of @p len characters, taken from the real encoder
/// rather than recomputed, so the geometry answers to what will be drawn.
uint16_t modulesFor(size_t len)
{
    const std::string id(len, 'A');
    Code128::Encoded e{};
    EXPECT_TRUE(Code128::encode(id.c_str(), e)) << "length " << len;
    return e.totalModules;
}


// ---------------------------------------------------------------------------
// The id, and the pager marks, beneath the bars
//
// These exist because this is the part that was got wrong -- twice, and both
// times invisibly, because the simulator draws the full 240x240 square and
// only the watch shows a bezel. A 15-character id put 20 pixels of ink outside
// the lit circle and a 16-character one 64.
//
// What is deliberately *not* asserted here: that the id box is lit across its
// full width at every row its glyphs occupy. It is not, and it does not need
// to be -- a glyph's widest point is not on its bottom row, so the box may
// overhang a row the ink does not reach into. Measured renders masked with a
// 120px-radius disc put zero ink outside the circle at every tier; a box-width
// invariant would fail that reality and force the box narrower for nothing.
// The invariants below are the ones that hold without knowing font metrics.
// ---------------------------------------------------------------------------

/// INVARIANT. The id box is centred on the panel. 27 + 187/2 = 120.5, which is
/// the centre of an even-width panel, so the doubled form is exact.
TEST(IdRow, TheIdBoxIsCentredOnThePanel)
{
    EXPECT_EQ(2 * BarcodeLayout::kIdX + BarcodeLayout::kIdW, BarcodeLayout::kPanelWidth + 1);
}

/// INVARIANT. The face-swap threshold is inside the box, so a string that
/// keeps the 20pt face is never clipped by the box as well -- the two limits
/// cannot cross without one of them being wrong.
TEST(IdRow, TheLargeFaceLimitIsInsideTheBox)
{
    EXPECT_LE(BarcodeLayout::kIdInkLimit, static_cast<uint16_t>(BarcodeLayout::kIdW));
}

/// INVARIANT. A split id's two lines sit between the bars and the pager marks
/// without touching either. This is what a layout change would break first.
TEST(IdRow, TheSplitLinesFitBetweenTheBarsAndTheMarks)
{
    EXPECT_GE(BarcodeLayout::kIdLine1Y, BarcodeLayout::kBackingY + BarcodeLayout::kBackingH)
        << "the first id line overlaps the white backing";
    EXPECT_LE(BarcodeLayout::kIdLine2Y + BarcodeLayout::kIdLineH, BarcodeLayout::kMarkY)
        << "the second id line overlaps the pager marks";
    EXPECT_EQ(BarcodeLayout::kIdLine2Y - BarcodeLayout::kIdLine1Y, BarcodeLayout::kIdLineH)
        << "the two lines are meant to be exactly one line-height apart";
}

/// INVARIANT. The single-line id does not reach the marks either.
TEST(IdRow, TheSingleLineIdDoesNotReachTheMarks)
{
    EXPECT_LE(BarcodeLayout::kIdY + BarcodeLayout::kIdH, BarcodeLayout::kMarkY);
}

/// INVARIANT. Every mark of a full row is lit, for every count the app can
/// reach -- and for the seven Commands.hpp allows, so raising kMaxCodes to its
/// ceiling cannot quietly push the row into the bezel.
TEST(PagerMarks, EveryMarkRowIsLitUpToTheCommandsCeiling)
{
    for (int16_t count = 2; count <= 7; count++) {
        const int16_t left  = BarcodeLayout::markRowLeft(count);
        const int16_t width = BarcodeLayout::markRowWidth(count);
        for (int16_t y = BarcodeLayout::kMarkY; y < BarcodeLayout::kMarkY + BarcodeLayout::kMarkH; y++) {
            EXPECT_TRUE(BarcodeLayout::rowIsLit(left, y, width))
                << count << " marks, row " << y << ": the row is cut by the bezel";
        }
    }
}

/// INVARIANT. The row is centred, and six marks span 83px.
TEST(PagerMarks, TheRowIsCentredAndSizedAsDocumented)
{
    EXPECT_EQ(BarcodeLayout::markRowWidth(6), 83);
    EXPECT_EQ(BarcodeLayout::markRowLeft(6), (BarcodeLayout::kPanelWidth - 83) / 2);
    EXPECT_EQ(BarcodeLayout::markRowWidth(1), BarcodeLayout::kMarkW);
}

// ---------------------------------------------------------------------------
// The X-dimension: the module width as a physical size
//
// Table 3-1 of the LS012B7DD06 datasheet gives a 0.126 mm dot pitch, which is
// the number that decides whether any of this can be read. It is also the
// number the pixel arithmetic cannot see: 1.6 px sounds ample and is 0.20 mm.
// ---------------------------------------------------------------------------

TEST(XDimension, TheDotPitchMatchesTheDatasheet)
{
    // 240 dots x 0.126 mm = 30.24 mm, the quoted active-area diameter.
    EXPECT_EQ(BarcodeLayout::kDotPitchMicrons, 126);
    EXPECT_EQ(BarcodeLayout::kActiveDiameterMicrons, 30240);
}

/// INVARIANT, and the one that matters most in this file: the parkrun-shaped
/// id this app exists for clears ISO/IEC 15417's general minimum. Eight
/// characters is 123 modules, 1.62 px, 0.204 mm against a 0.19 mm floor.
/// If this fails, the app has stopped being able to do its job.
TEST(XDimension, AnEightCharacterIdClearsTheGeneralMinimum)
{
    const Scannability s = BarcodeLayout::scannabilityFor(modulesFor(8));
    EXPECT_EQ(s.totalModules, 123);
    EXPECT_EQ(s.xDimensionMicrons(), 204);
    EXPECT_TRUE(s.meetsMinimumXDimension());
}

/// CHARACTERISATION. Nine characters and up are below the 0.19 mm minimum, and
/// no rendering change fixes that -- 268 px of bars would be needed for sixteen
/// characters and the panel is 240 px wide. Subset C, which packs two digits
/// per symbol, is the only lever: it would take a 16-digit id from 211 modules
/// to 123, the same as eight characters above.
TEST(XDimension, CurrentlyNothingBeyondEightCharactersMeetsTheMinimum)
{
    for (size_t len = 9; len <= Code128::kMaxDataLength; len++) {
        const Scannability s = BarcodeLayout::scannabilityFor(modulesFor(len));
        EXPECT_FALSE(s.meetsMinimumXDimension())
            << "length " << len << " now reaches " << s.xDimensionMicrons()
            << " um; if this improved, tighten this test";
    }
}

/// CHARACTERISATION. The retail floor of 0.25 mm is reached only up to five
/// characters, which no real id is.
TEST(XDimension, CurrentlyOnlyVeryShortIdsMeetTheRetailFloor)
{
    EXPECT_TRUE(BarcodeLayout::scannabilityFor(modulesFor(5)).meetsRetailXDimension());
    for (size_t len = 6; len <= Code128::kMaxDataLength; len++) {
        EXPECT_FALSE(BarcodeLayout::scannabilityFor(modulesFor(len)).meetsRetailXDimension())
            << "length " << len;
    }
}

/// Reporting, so a reader gets the whole picture in one place rather than
/// reconstructing it from the tests above.
TEST(XDimension, ReportTheWholeRange)
{
    for (size_t len = 1; len <= Code128::kMaxDataLength; len++) {
        const Scannability s = BarcodeLayout::scannabilityFor(modulesFor(len));
        std::cout << "  len " << (len < 10 ? " " : "") << len
                  << "  modules " << s.totalModules
                  << "  X " << s.xDimensionMicrons() << " um"
                  << (s.meetsRetailXDimension() ? "  retail-ok"
                      : s.meetsMinimumXDimension() ? "  general-ok"
                                                   : "  BELOW 190 um")
                  << "\n";
    }
}

// ---------------------------------------------------------------------------
// The panel is round
// ---------------------------------------------------------------------------

TEST(RoundPanel, CentreIsLitAndCornersAreNot)
{
    EXPECT_TRUE(BarcodeLayout::pixelIsLit(120, 120));
    EXPECT_TRUE(BarcodeLayout::pixelIsLit(119, 119));

    // The framebuffer's four corners are addressable and dark.
    EXPECT_FALSE(BarcodeLayout::pixelIsLit(0, 0));
    EXPECT_FALSE(BarcodeLayout::pixelIsLit(239, 0));
    EXPECT_FALSE(BarcodeLayout::pixelIsLit(0, 239));
    EXPECT_FALSE(BarcodeLayout::pixelIsLit(239, 239));
}

/// The panel is 240 px across but the inscribed circle is not: with an even
/// number of rows there is no centre row, so the widest row is half a pixel off
/// the axis and column 0 is dark everywhere. The usable width is 238, inset one
/// pixel each side.
///
/// Worth its own test because "240 wide" is the number in every document and it
/// is the wrong one to lay anything out against.
TEST(RoundPanel, TheModelsWidestRowIs238PxNot240)
{
    for (int16_t y : { 119, 120 }) {
        EXPECT_FALSE(BarcodeLayout::pixelIsLit(0, y)) << "row " << y;
        EXPECT_TRUE(BarcodeLayout::pixelIsLit(1, y)) << "row " << y;
        EXPECT_TRUE(BarcodeLayout::pixelIsLit(238, y)) << "row " << y;
        EXPECT_FALSE(BarcodeLayout::pixelIsLit(239, y)) << "row " << y;
        EXPECT_TRUE(BarcodeLayout::rowIsLit(1, y, 238)) << "row " << y;
        EXPECT_FALSE(BarcodeLayout::rowIsLit(0, y, 240)) << "row " << y;
    }
}

TEST(RoundPanel, TheLitRegionIsSymmetric)
{
    for (int16_t y = 0; y < BarcodeLayout::kPanelHeight; y++) {
        for (int16_t x = 0; x < BarcodeLayout::kPanelWidth; x++) {
            const bool lit = BarcodeLayout::pixelIsLit(x, y);
            EXPECT_EQ(lit, BarcodeLayout::pixelIsLit(239 - x, y)) << x << "," << y;
            EXPECT_EQ(lit, BarcodeLayout::pixelIsLit(x, 239 - y)) << x << "," << y;
        }
    }
}

/// INVARIANT. Every bar the encoder produces is drawn inside the lit circle.
/// If this fails, part of the symbol is in the dark and the barcode is
/// truncated -- which still scans, as a shorter, wrong number.
TEST(RoundPanel, TheBarsAreEntirelyWithinTheLitCircle)
{
    EXPECT_TRUE(BarcodeLayout::fitsInPanel(BarcodeLayout::kBarsX, BarcodeLayout::kBarsY,
                                           BarcodeLayout::kBarsW, BarcodeLayout::kBarsH))
        << "the bar rectangle's corners must all be lit";
}

/// CHARACTERISATION, and the interesting one. The white backing is *wider and
/// INVARIANT. The backing now fits the circle whole, corners included. It did
/// not always: at 220x110 its four corners sat outside the lit area and the
/// display cut the tips off, which is what the previous version of this test
/// recorded. 0.3.1 shrank the height to 94 -- 2*sqrt(r^2 - halfWidth^2) at 220
/// wide -- and the quiet zone did not shrink with it, which is the thing that
/// change had to not do (kQuietPxEachSide is still 10; see TheBarsSitInsideTheBacking).
TEST(RoundPanel, TheWhiteBackingFitsEntirelyInsideTheLitCircle)
{
    EXPECT_TRUE(BarcodeLayout::fitsInPanel(BarcodeLayout::kBackingX, BarcodeLayout::kBackingY,
                                           BarcodeLayout::kBackingW, BarcodeLayout::kBackingH))
        << "a corner of the white backing is outside the lit circle; if the backing grew, "
           "check what it did to the rows the bars occupy";
}

/// INVARIANT, and the one that makes the cut corners acceptable. In every row
/// the bars occupy, the backing's full width is lit -- so the quiet zone
/// either side of the bars is white for the whole height of the symbol, not
/// just in the middle.
TEST(RoundPanel, EveryBarRowHasTheFullBackingWidthLit)
{
    for (int16_t y = BarcodeLayout::kBarsY;
         y < BarcodeLayout::kBarsY + BarcodeLayout::kBarsH; y++) {
        EXPECT_TRUE(BarcodeLayout::rowIsLit(BarcodeLayout::kBackingX, y, BarcodeLayout::kBackingW))
            << "row " << y << " does not carry the full quiet zone";
    }
}

/// INVARIANT, and stronger than it used to be: there are no cut rows at all
/// now, so no row of the backing is partial. Stated as its own test because
/// the interesting failure is a *future* backing that grows back into the
/// bezel -- this would fail while the corner test above might not.
TEST(RoundPanel, NoBackingRowIsCut)
{
    for (int16_t y = BarcodeLayout::kBackingY;
         y < BarcodeLayout::kBackingY + BarcodeLayout::kBackingH; y++) {
        EXPECT_TRUE(BarcodeLayout::rowIsLit(BarcodeLayout::kBackingX, y, BarcodeLayout::kBackingW))
            << "row " << y << " of the white backing is cut by the bezel";
    }
}

TEST(RoundPanel, TheBarsSitInsideTheBacking)
{
    EXPECT_GE(BarcodeLayout::kBarsX, BarcodeLayout::kBackingX);
    EXPECT_GE(BarcodeLayout::kBarsY, BarcodeLayout::kBackingY);
    EXPECT_LE(BarcodeLayout::kBarsX + BarcodeLayout::kBarsW,
              BarcodeLayout::kBackingX + BarcodeLayout::kBackingW);
    EXPECT_LE(BarcodeLayout::kBarsY + BarcodeLayout::kBarsH,
              BarcodeLayout::kBackingY + BarcodeLayout::kBackingH);
    EXPECT_EQ(BarcodeLayout::kQuietPxEachSide, 10);
}

// ---------------------------------------------------------------------------
// The panel has four levels a channel
// ---------------------------------------------------------------------------

TEST(PanelTechnology, FourLevelsPerChannel)
{
    // ABGR2222: two bits each. Recorded as a test so a port to another format
    // has to come past it -- the anti-aliasing argument below rests on it.
    EXPECT_EQ(BarcodeLayout::kLevelsPerChannel, 4);
}

/// CHARACTERISATION. At every length but one, a module is a fractional number
/// of pixels, so every bar edge is anti-aliased across four grey levels. This
/// is the finding the renderer work exists to fix.
TEST(PanelTechnology, CurrentlyAlmostNoIdLengthGivesWholePixelModules)
{
    for (size_t len = 1; len <= Code128::kMaxDataLength; len++) {
        if (len == 15) {
            continue; // the one exception, below
        }
        const Scannability s = BarcodeLayout::scannabilityFor(modulesFor(len));
        EXPECT_FALSE(s.modulesAreWholePixels())
            << "length " << len << ": " << s.totalModules << " modules across "
            << s.barsWidthPx << " px divides evenly -- tighten this test";
    }
}

/// The exception, and it is not good news. A 15-character id comes to exactly
/// 200 modules across exactly 200 px, so every module is precisely one pixel
/// and no edge is anti-aliased -- the only length where the panel's four levels
/// are not being asked to fake anything.
///
/// It is also the narrowest a bar can possibly be. So the one length that
/// renders cleanly is the one a scanner has least chance of resolving, which is
/// the whole tension of drawing Code 128 on a 240 px display in one number.
TEST(PanelTechnology, AtFifteenCharactersModulesAreExactlyOnePixel)
{
    const Scannability s = BarcodeLayout::scannabilityFor(modulesFor(15));
    EXPECT_EQ(s.totalModules, 200);
    EXPECT_TRUE(s.modulesAreWholePixels());
    EXPECT_EQ(s.moduleWidthCentipx(), 100u);
    EXPECT_EQ(s.narrowElementPx(), 1u);
}

// ---------------------------------------------------------------------------
// Quiet zones, in the unit the standard uses
// ---------------------------------------------------------------------------

TEST(QuietZone, TheStandardIsTenModules)
{
    EXPECT_EQ(BarcodeLayout::kQuietZoneModulesRequired, 10);
}

/// The comparison must be done in modules. Done in pixels it looks fine, which
/// is how a 10 px margin passed for a quiet zone in the first place.
TEST(QuietZone, IsMeasuredInModulesNotPixels)
{
    // 200 px of bars over 100 modules is a 2 px module, so ten modules is
    // 20 px -- more than the 10 px of white available.
    Scannability s{};
    s.totalModules = 100;
    EXPECT_EQ(s.moduleWidthCentipx(), 200u);
    EXPECT_FALSE(s.meetsQuietZone());

    // Widen the margin to the 20 px the standard actually asks for and it
    // passes -- so the predicate is measuring the right thing.
    s.quietPxPerSide = 20;
    EXPECT_TRUE(s.meetsQuietZone());
}

/// CHARACTERISATION, and the headline defect. At every id length the app
/// accepts, the white margin is short of ISO/IEC 15417's ten modules.
///
/// The 16-character case is the one to understand: it is the only length where
/// ten modules fit inside 10 px, and only because the module has shrunk below
/// one pixel. Passing there is not the standard being met, it is the symbol
/// having become too small to draw. Asserted as its own case rather than
/// smoothed into the loop.
TEST(QuietZone, CurrentlyShortOfTheStandardAtEveryUsefulLength)
{
    for (size_t len = 1; len <= 12; len++) {
        const Scannability s = BarcodeLayout::scannabilityFor(modulesFor(len));
        EXPECT_FALSE(s.meetsQuietZone())
            << "length " << len << ": quiet zone is now "
            << s.quietZoneCentimodules() / 100.0 << " modules -- tighten this test";
    }
}

TEST(QuietZone, CurrentlyOnlyMetOnceTheModuleIsSubPixel)
{
    const Scannability s = BarcodeLayout::scannabilityFor(modulesFor(16));
    EXPECT_TRUE(s.meetsQuietZone());
    EXPECT_LT(s.moduleWidthCentipx(), 100u)
        << "the quiet zone only 'passes' here because a module is under a pixel";
    EXPECT_EQ(s.narrowElementPx(), 0u) << "a narrow element that rounds to no pixels at all";
}

/// A parkrun id is a letter and seven digits, which is the length this app
/// exists to draw. Recorded on its own so the numbers are in the suite rather
/// than in a commit message.
TEST(QuietZone, TheParkrunShapedCaseInFull)
{
    const Scannability s = BarcodeLayout::scannabilityFor(modulesFor(8));

    EXPECT_EQ(s.totalModules, 123);
    EXPECT_EQ(s.barsWidthPx, 200);
    EXPECT_EQ(s.moduleWidthCentipx(), 162u);      // 1.62 px per module
    EXPECT_EQ(s.narrowElementPx(), 1u);           // a narrow bar is one pixel
    EXPECT_EQ(s.quietZoneCentimodules(), 615u);   // 6.15 modules of white
    EXPECT_FALSE(s.meetsQuietZone());             // against ten required
    EXPECT_FALSE(s.modulesAreWholePixels());
}

/// INVARIANT. Whatever else is true, the symbol never overruns the white it is
/// drawn on -- there is always *some* quiet zone, and the bars never touch the
/// black.
TEST(QuietZone, TheBarsNeverReachTheEdgeOfTheWhite)
{
    for (size_t len = 1; len <= Code128::kMaxDataLength; len++) {
        const Scannability s = BarcodeLayout::scannabilityFor(modulesFor(len));
        EXPECT_GT(s.quietPxPerSide, 0) << "length " << len;
        EXPECT_LE(s.barsWidthPx + 2 * s.quietPxPerSide, BarcodeLayout::kBackingW)
            << "length " << len;
    }
}

/// A reference table, printed rather than asserted, so a run of the suite
/// shows what the display is being asked to do at each length.
TEST(QuietZone, ReportTheWholeRange)
{
    for (size_t len = 1; len <= Code128::kMaxDataLength; len++) {
        const Scannability s = BarcodeLayout::scannabilityFor(modulesFor(len));
        RecordProperty("len" + std::to_string(len) + "_modules", s.totalModules);
        RecordProperty("len" + std::to_string(len) + "_module_centipx",
                       s.moduleWidthCentipx());
        RecordProperty("len" + std::to_string(len) + "_quiet_centimodules",
                       s.quietZoneCentimodules());
        std::cout << "  len " << (len < 10 ? " " : "") << len
                  << "  modules " << s.totalModules
                  << "  module " << s.moduleWidthCentipx() / 100.0 << " px"
                  << "  quiet " << s.quietZoneCentimodules() / 100.0 << " modules"
                  << (s.meetsQuietZone() ? "" : "  SHORT") << "\n";
    }
}

} // namespace
