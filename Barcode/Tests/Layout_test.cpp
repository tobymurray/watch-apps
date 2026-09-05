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
#include "Itf.hpp"

namespace {

using BarcodeLayout::Scannability;

/// Module count for an id of @p len characters, taken from the real encoder
/// rather than recomputed, so the geometry answers to what will be drawn.
uint16_t modulesFor(size_t len)
{
    const std::string id(len, 'A');
    Barcode::Encoded e{};
    EXPECT_TRUE(Code128::encode(id.c_str(), e)) << "length " << len;
    return e.totalModules;
}

/// The same, for an id of @p len digits.
///
/// A separate helper because the two diverge: subset C packs a pair of digits
/// into one symbol, so a numeric id costs close to half the modules of an
/// alphabetic one of the same length. Which of the two a wearer has is not a
/// detail -- it is most of the difference in what a scanner sees.
uint16_t digitModulesFor(size_t len)
{
    std::string id;
    for (size_t i = 0; i < len; i++) id += static_cast<char>('0' + (i % 10));
    Barcode::Encoded e{};
    EXPECT_TRUE(Code128::encode(id.c_str(), e)) << "length " << len;
    return e.totalModules;
}

/// Module count for a literal id, for the shapes worth naming.
uint16_t modulesForId(const char *id)
{
    Barcode::Encoded e{};
    EXPECT_TRUE(Code128::encode(id, e)) << id;
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
// Drawability: whether the panel can put this symbol on the glass at all
//
// A separate question from the X-dimension below, and the one a long id runs
// into first. Below one pixel per module a narrow bar cannot own a column, so
// the renderer's four gray levels quantise it to 170,170,170 and the space
// beside it fills in. Scannability::modulesAreAtLeastOnePixel() carries the
// measurement; these hold the boundary it was measured at.
//
// Nothing in the app refuses an id for failing this. The GUI shows a warning
// the wearer dismisses once per id, and then draws it -- so what these tests
// protect is the honesty of the warning, not a gate.
// ---------------------------------------------------------------------------

TEST(Drawability, TheBoundaryIsTheBarsBandInModules)
{
    // One module per pixel, exactly, is the last count that draws cleanly.
    EXPECT_TRUE(BarcodeLayout::scannabilityFor(BarcodeLayout::kBarsW)
                    .modulesAreAtLeastOnePixel());
    EXPECT_FALSE(BarcodeLayout::scannabilityFor(BarcodeLayout::kBarsW + 1)
                     .modulesAreAtLeastOnePixel());
}

/// The measured cliff, in the units the measurement was made in: 200 modules
/// had no faint bar in 4800 sampled ids and 211 had one in 317 of 400. Those
/// are the two module counts Code 128 can actually produce either side of the
/// boundary -- symbols come in steps of 11, so there is nothing in between.
TEST(Drawability, TwoHundredModulesDrawsAndTwoHundredAndElevenDoesNot)
{
    EXPECT_TRUE(BarcodeLayout::scannabilityFor(200).modulesAreAtLeastOnePixel());
    EXPECT_FALSE(BarcodeLayout::scannabilityFor(211).modulesAreAtLeastOnePixel());
}

/// INVARIANT. The reason this app can be asked for a longer id at all: a
/// digit-only id pairs into subset C, so every length the app accepts still
/// draws cleanly. This is the case the limit was raised for.
TEST(Drawability, EveryDigitOnlyIdThisAppAcceptsDrawsCleanly)
{
    for (size_t len = 1; len <= Code128::kMaxDataLength; len++) {
        const Scannability s = BarcodeLayout::scannabilityFor(digitModulesFor(len));
        EXPECT_TRUE(s.modulesAreAtLeastOnePixel())
            << len << " digits is " << s.totalModules << " modules";
    }
}

/// A parkrun id and the two shapes the README names, all well inside.
TEST(Drawability, TheShapesThisAppExistsForDrawCleanly)
{
    for (const char *id : { "A1234567", "12345678", "GYMWORLD12345678" }) {
        EXPECT_TRUE(BarcodeLayout::scannabilityFor(modulesForId(id)).modulesAreAtLeastOnePixel())
            << id;
    }
}

/// CHARACTERISATION. Where a subset-B id crosses, and the fact that the
/// crossing predates this limit being raised: fifteen alphabetic characters
/// draws and sixteen does not, and sixteen was the ceiling before.
TEST(Drawability, FifteenAlphabeticCharactersDrawsAndSixteenDoesNot)
{
    EXPECT_TRUE(BarcodeLayout::scannabilityFor(modulesFor(15)).modulesAreAtLeastOnePixel());
    EXPECT_FALSE(BarcodeLayout::scannabilityFor(modulesFor(16)).modulesAreAtLeastOnePixel());
}

/// REPORT. What each length costs, so a change to the geometry has to answer
/// for which ids stop drawing.
TEST(Drawability, ReportTheWholeRange)
{
    for (size_t len = 1; len <= Code128::kMaxDataLength; len++) {
        const Scannability alpha  = BarcodeLayout::scannabilityFor(modulesFor(len));
        const Scannability digits = BarcodeLayout::scannabilityFor(digitModulesFor(len));
        RecordProperty("len" + std::to_string(len) + "_alpha_modules", alpha.totalModules);
        RecordProperty("len" + std::to_string(len) + "_digit_modules", digits.totalModules);
        std::cout << "  len " << (len < 10 ? " " : "") << len
                  << "  alpha " << alpha.totalModules << " modules"
                  << (alpha.modulesAreAtLeastOnePixel() ? "  draws" : "  FAINT")
                  << "   digits " << digits.totalModules << " modules"
                  << (digits.modulesAreAtLeastOnePixel() ? "  draws" : "  FAINT")
                  << "\n";
    }
}

// ---------------------------------------------------------------------------
// The X-dimension: the module width as a physical size
//
// Table 3-1 of the LS012B7DD06 datasheet gives a 0.126 mm dot pitch, which is
// what turns "1.6 pixels" into a number a scanner cares about.
//
// There is deliberately no test here called "meets the standard", because
// ISO/IEC 15417 does not set a minimum X-dimension -- its scope leaves it among
// "the parameters to be defined by applications". The tests compare against
// scanner *capability* instead, and say where each figure came from.
// ---------------------------------------------------------------------------

TEST(XDimension, TheDotPitchMatchesTheDatasheet)
{
    // 240 dots x 0.126 mm = 30.24 mm, the quoted active-area diameter.
    EXPECT_EQ(BarcodeLayout::kDotPitchMicrons, 126);
    EXPECT_EQ(BarcodeLayout::kActiveDiameterMicrons, 30240);
}

/// INVARIANT. A parkrun id is "A" plus six or seven digits, so seven or eight
/// characters, and that is what this app exists for. Both come out well clear
/// of 5 mil -- 0.225 and 0.204 mm, about 8.9 and 8.0 mil. If this fails the app
/// has stopped being able to do its main job.
TEST(XDimension, AParkrunIdIsComfortablyAboveMidDensity)
{
    for (size_t len : {7u, 8u}) {
        const Scannability s = BarcodeLayout::scannabilityFor(modulesFor(len));
        EXPECT_TRUE(s.resolvableAt(BarcodeLayout::kMidDensityScannerMicrons))
            << "length " << len << " is " << s.xDimensionMicrons() << " um";
    }
    EXPECT_EQ(BarcodeLayout::scannabilityFor(modulesFor(8)).xDimensionMicrons(), 204);
}

/// INVARIANT. Every id length this app accepts stays above the narrow-element
/// minimum on the LS2208's spec sheet, which is the most capable of the
/// reference points and the cheapest scanner. Resolution is not the first
/// thing a long id runs out of here -- Drawability below is, and well before
/// this.
TEST(XDimension, EvenTheLongestIdIsAboveTheAggressiveScannerFigure)
{
    for (size_t len = 1; len <= Code128::kMaxDataLength; len++) {
        const Scannability s = BarcodeLayout::scannabilityFor(modulesFor(len));
        EXPECT_TRUE(s.resolvableAt(BarcodeLayout::kAggressiveScannerMicrons))
            << "length " << len << " is " << s.xDimensionMicrons() << " um";
    }
}

/// CHARACTERISATION: fifteen *non-numeric* characters is where the 5 mil
/// mid-density line is crossed, and every longer subset-B id is below it.
/// Fourteen, at 5.0 mil, is the last one that clears it.
///
/// This used to name Subset C as the fix if it ever mattered, priced by the
/// standard at 5,5 modules per numeric character against 11 for a Subset B
/// one. It is implemented now, and the tests below say what it reached. What
/// remains here is what it cannot touch: a letter has nothing to pair with.
TEST(XDimension, EveryNonNumericIdOfFifteenOrMoreFallsUnderMidDensity)
{
    for (size_t len = 15; len <= Code128::kMaxDataLength; len++) {
        EXPECT_FALSE(BarcodeLayout::scannabilityFor(modulesFor(len))
                         .resolvableAt(BarcodeLayout::kMidDensityScannerMicrons))
            << "length " << len << " now clears 5 mil; tighten this test";
    }
    EXPECT_TRUE(BarcodeLayout::scannabilityFor(modulesFor(14))
                    .resolvableAt(BarcodeLayout::kMidDensityScannerMicrons))
        << "fourteen characters is the longest that clears 5 mil";
}

// ---------------------------------------------------------------------------
// Subset C, and what pairing digits actually bought
//
// The encoder packs a pair of digits into one symbol wherever that shortens
// the barcode -- the 5,5-modules-per-numeric-character figure the standard
// gives, against 11 for a Subset B character. These tests are what that comes
// to on this panel.
// ---------------------------------------------------------------------------

/// INVARIANT. Every numeric id length this app accepts clears the 5 mil
/// mid-density line -- including fifteen and sixteen digits, which are the two
/// lengths the characterisation above records as falling under it when they
/// are letters.
///
/// That is the whole of what Subset C bought, stated as one claim: a long id
/// made of digits is no longer the awkward case. The worst numeric length is
/// fifteen digits at 0.188 mm, which is still half again the 5 mil line.
TEST(XDimension, EveryNumericIdLengthClearsMidDensity)
{
    for (size_t len = 1; len <= Code128::kMaxDataLength; len++) {
        const Scannability s = BarcodeLayout::scannabilityFor(digitModulesFor(len));
        EXPECT_TRUE(s.resolvableAt(BarcodeLayout::kMidDensityScannerMicrons))
            << "length " << len << " is " << s.xDimensionMicrons() << " um";
    }
    EXPECT_EQ(BarcodeLayout::scannabilityFor(digitModulesFor(15)).xDimensionMicrons(), 188);
    EXPECT_EQ(BarcodeLayout::scannabilityFor(digitModulesFor(16)).xDimensionMicrons(), 204);
}

/// INVARIANT. Sixteen digits cost exactly what eight letters cost -- 123
/// modules -- which is the 5,5-against-11 pricing showing up end to end.
TEST(XDimension, SixteenDigitsCostWhatEightLettersCost)
{
    EXPECT_EQ(digitModulesFor(16), 123);
    EXPECT_EQ(modulesFor(8), 123);
}

/// INVARIANT, and the least obvious thing in this file: **adding a digit to an
/// odd-length numeric id makes the barcode narrower.**
///
/// Subset C pairs digits, so an odd count strands one of them. That last digit
/// costs a symbol of its own *and* a switch back into subset B to carry it --
/// two symbols to say what a pair says in one. Nine digits are 101 modules and
/// ten are 90; fifteen are 134 and sixteen are 123.
///
/// It is not an optimisation the encoder is missing and there is no encoding
/// that avoids it: an odd number of digits cannot be paired. It is worth
/// pinning because it inverts the intuition every other length rule on this
/// screen follows, and because it is actionable -- a numeric id with a leading
/// zero to spare renders wider bars with it than without.
TEST(XDimension, AnOddNumberOfDigitsIsWiderThanOneMoreDigit)
{
    for (size_t len = 3; len < Code128::kMaxDataLength; len += 2) {
        EXPECT_GT(digitModulesFor(len), digitModulesFor(len + 1))
            << len << " digits should cost more than " << (len + 1);
    }
}

/// INVARIANT. A real parkrun id -- a letter and seven digits -- is the shape
/// Subset C helps least, since the letter and the odd leading digit both stay
/// in subset B and only the remaining six pair up. It still goes from 123
/// modules to 101: 0.204 mm to 0.249 mm, about 8.0 mil to 9.8 mil.
///
/// The test above this one measures eight *letters*, which is the pessimistic
/// stand-in. This is the actual id, and it is a fifth wider than that test
/// implies.
TEST(XDimension, ARealParkrunIdIsWiderThanItsLengthSuggests)
{
    const Scannability s = BarcodeLayout::scannabilityFor(modulesForId("A1234567"));
    EXPECT_EQ(s.totalModules, 101);
    EXPECT_EQ(s.xDimensionMicrons(), 249);
    EXPECT_TRUE(s.resolvableAt(BarcodeLayout::kMidDensityScannerMicrons));

    // Comfortably wider than the same length in letters.
    EXPECT_GT(s.xDimensionMicrons(),
              BarcodeLayout::scannabilityFor(modulesFor(8)).xDimensionMicrons());
}

/// Reporting, so a reader gets the whole picture in one place.
TEST(XDimension, ReportTheWholeRange)
{
    for (size_t len = 1; len <= Code128::kMaxDataLength; len++) {
        const Scannability s = BarcodeLayout::scannabilityFor(modulesFor(len));
        std::cout << "  len " << (len < 10 ? " " : "") << len
                  << "  modules " << s.totalModules
                  << "  X " << s.xDimensionMicrons() << " um"
                  << (s.resolvableAt(BarcodeLayout::kMidDensityScannerMicrons)
                          ? "  clears 5 mil"
                          : "  under 5 mil")
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


// ---------------------------------------------------------------------------
// The square symbol, and the panel that is a bad fit for it
//
// A linear symbology spends width, which this panel has. A matrix one spends
// area in both directions, and a circle is the worst possible container for a
// square. Every number in this section is arithmetic over pixelIsLit's model,
// so it can be argued about at a desk -- which is the point, because the
// simulator draws the full 240x240 square and cannot show any of it.
// ---------------------------------------------------------------------------

/// The largest square of lit pixels anywhere on the panel. Docs/SYMBOLOGIES.md
/// says "about 169 px" and it is 168; the difference does not change any
/// decision, but this file is where the number is measured rather than guessed.
int largestLitSquare(int bottomLimit = BarcodeLayout::kPanelHeight - 1)
{
    for (int side = BarcodeLayout::kPanelHeight; side > 0; side--) {
        for (int top = 0; top + side - 1 <= bottomLimit; top++) {
            const int left = (BarcodeLayout::kPanelWidth - side) / 2;
            if (BarcodeLayout::fitsInPanel(left, top, side, side)) {
                return side;
            }
        }
    }
    return 0;
}

TEST(QrPanel, TheLargestSquareOnTheWholePanelIs168px)
{
    EXPECT_EQ(largestLitSquare(), 168);
}

/// THE NUMBER THAT DECIDES THE VERSION. The panel's largest square is not what
/// a QR symbol may use, because the human-readable id sits below it and a
/// barcode without its id underneath is half a barcode. What is available is
/// the largest square ending above kIdLine1Y, and that is 144.
TEST(QrPanel, AboveTheIdRowOnly144pxIsAvailable)
{
    EXPECT_EQ(largestLitSquare(BarcodeLayout::kIdLine1Y - 1), 144);
}

/// CHARACTERISATION, and the one worth knowing. A 5 px module misses fitting by
/// a single pixel row: version 1 with its mandatory quiet zone is 29 modules,
/// and 29 * 5 = 145 against the 144 available. Recorded rather than rounded
/// away, because reclaiming those pixels means re-opening a layout measured
/// from rendered ink -- see the tables in README.md -- which has clipped on
/// device twice.
TEST(QrPanel, AFivePixelModuleMissesByOnePixelRow)
{
    const int available = largestLitSquare(BarcodeLayout::kIdLine1Y - 1);
    const int version1AtFive = (21 + 2 * BarcodeLayout::kQrQuietModules) * 5;

    EXPECT_EQ(version1AtFive, 145);
    EXPECT_EQ(version1AtFive - available, 1) << "if this is ever 0, a 5 px module fits";
    EXPECT_EQ(BarcodeLayout::kQrModulePx, 4);
}

TEST(QrPanel, TheNextVersionUpWouldNotFit)
{
    // Version 3 is 29 modules, 37 with the quiet zone, 148 px at 4 px a module.
    const int version3 = (29 + 2 * BarcodeLayout::kQrQuietModules) * BarcodeLayout::kQrModulePx;
    EXPECT_GT(version3, largestLitSquare(BarcodeLayout::kIdLine1Y - 1));
}

TEST(QrPanel, TheSymbolIsTheVersionPlusItsQuietZone)
{
    EXPECT_EQ(BarcodeLayout::kQrModules, 25) << "version 2 is 17 + 4 * 2";
    EXPECT_EQ(BarcodeLayout::kQrQuietModules, 4) << "ISO/IEC 18004 requires four modules";
    EXPECT_EQ(BarcodeLayout::kQrSide, 132);
    EXPECT_EQ(BarcodeLayout::kQrSide,
              (BarcodeLayout::kQrModules + 2 * BarcodeLayout::kQrQuietModules)
                  * BarcodeLayout::kQrModulePx);

    // The dark modules are inset by the quiet zone, and take the middle 100 px.
    EXPECT_EQ(BarcodeLayout::kQrInkX - BarcodeLayout::kQrX, 16);
    EXPECT_EQ(BarcodeLayout::kQrModules * BarcodeLayout::kQrModulePx, 100);
}

/// INVARIANT, and the strongest form of it: not the four corners but every
/// pixel. A corner test is enough for a rectangle whose meaning is carried by
/// whole rows; a QR symbol's meaning is carried by individual modules, so a
/// clipped one is not a shorter symbol, it is an unreadable one.
TEST(QrPanel, EveryPixelOfTheSymbolIsLit)
{
    for (int16_t y = BarcodeLayout::kQrY; y < BarcodeLayout::kQrY + BarcodeLayout::kQrSide; y++) {
        for (int16_t x = BarcodeLayout::kQrX; x < BarcodeLayout::kQrX + BarcodeLayout::kQrSide; x++) {
            EXPECT_TRUE(BarcodeLayout::pixelIsLit(x, y)) << x << "," << y;
        }
    }
}

TEST(QrPanel, TheSymbolIsCentredAndEndsAboveTheIdRow)
{
    EXPECT_EQ(BarcodeLayout::kQrX,
              (BarcodeLayout::kPanelWidth - BarcodeLayout::kQrSide) / 2);
    EXPECT_LT(BarcodeLayout::kQrY + BarcodeLayout::kQrSide, BarcodeLayout::kIdLine1Y)
        << "the symbol must not overlap the human-readable id";
}

/// WHY A QR CODE SHOWS NO NAME. Not a preference and not an oversight: the
/// symbol overlaps the caption band, and every size that would clear it is
/// smaller. MainView::showBarcode() hides the caption for a matrix format, and
/// this is the arithmetic that forces it.
TEST(QrPanel, TheSymbolOverlapsTheCaptionBandSoTheNameCannotBeShown)
{
    const int captionBottom = BarcodeLayout::kCaptionY + BarcodeLayout::kCaptionH - 1;
    EXPECT_LT(BarcodeLayout::kQrY, captionBottom) << "if this ever fails, the caption could stay";

    // And clearing it is not affordable: below the caption there are only
    // kIdLine1Y - captionBottom - 1 rows, which is less than the symbol needs.
    const int belowTheCaption = BarcodeLayout::kIdLine1Y - captionBottom - 1;
    EXPECT_LT(belowTheCaption, BarcodeLayout::kQrSide);
}

TEST(QrPanel, TheModuleIsHalfAMillimetre)
{
    const BarcodeLayout::QrScannability s{};

    EXPECT_EQ(s.moduleMicrons(), 504) << "4 px at a 126 um dot pitch";
    EXPECT_EQ(s.symbolMicrons(), 132 * 126);

    // Four times the X-dimension of this app's worst-case Code 128 id, which is
    // the whole density argument for the format. Not a claim that it scans --
    // that needs a camera, and Tests/README.md says so.
    const Scannability worstCase = BarcodeLayout::scannabilityFor(modulesFor(16));
    EXPECT_GT(s.moduleMicrons(), worstCase.xDimensionMicrons() * 4);
    RecordProperty("qr_module_um", s.moduleMicrons());
    RecordProperty("code128_16char_xdim_um", worstCase.xDimensionMicrons());
}

TEST(QrPanel, ModulesAreWholePixelsSoNoEdgeIsAntiAliased)
{
    // The same question modulesAreWholePixels() asks of the bars, and for a
    // matrix symbology it is answered by construction rather than by luck:
    // the module size is declared in pixels, so every edge lands on one.
    EXPECT_EQ(BarcodeLayout::kQrSide % BarcodeLayout::kQrModulePx, 0);
    EXPECT_EQ((BarcodeLayout::kQrInkX - BarcodeLayout::kQrX) % BarcodeLayout::kQrModulePx, 0);
    EXPECT_EQ(BarcodeLayout::kLevelsPerChannel, 4)
        << "the reason whole pixels matter at all";
}

// ---------------------------------------------------------------------------
// ITF, which shares the band with Code 128 and is laid out by different rules
//
// Two of them, and both come from the panel rather than the symbology: whole
// pixels, because four levels a channel makes a mid-pixel edge step rather
// than blend; and bearer bars, because ITF has no check character and a scan
// that clips the symbol can decode as a valid shorter number.
// ---------------------------------------------------------------------------

/// The digit counts ITF accepts, which is the even ones.
std::vector<size_t> itfLengths()
{
    std::vector<size_t> out;
    for (size_t d = 2; d <= Itf::kMaxDataLength; d += 2) out.push_back(d);
    return out;
}

/// INVARIANT, and the one that decides whether ITF is drawn correctly at all:
/// **the quiet zone meets ten narrow elements at every length**.
///
/// This is the test that caught the layout being wrong. Sizing the element
/// from the bars band -- 200 px for the symbol alone -- passes at 2, 8, 12, 14
/// and 16 digits and fails at 4, 6 and 10, because the symbol grows into the
/// white the margin needed. The element has to be sized from the *backing*,
/// with the quiet zone in the budget: kBackingW for units + 2 * 10 elements.
TEST(ItfPanel, TheQuietZoneMeetsTenElementsAtEveryLength)
{
    for (size_t d : itfLengths()) {
        const uint16_t units = Itf::unitsFor(d);
        EXPECT_TRUE(BarcodeLayout::itfMeetsQuietZone(units))
            << d << " digits: " << BarcodeLayout::itfQuietPx(units) << " px of white against "
            << BarcodeLayout::kItfQuietUnitsRequired * BarcodeLayout::itfUnitPx(units)
            << " px required";
    }
}

/// INVARIANT. The symbol and both quiet zones fit inside the white backing,
/// which is the only light surface on the screen -- outside it the panel is
/// black and a quiet zone there is not a quiet zone.
TEST(ItfPanel, TheSymbolAndItsQuietZonesFitTheBacking)
{
    for (size_t d : itfLengths()) {
        const uint16_t units = Itf::unitsFor(d);
        const int16_t left  = BarcodeLayout::itfLeftPx(units);
        const int16_t width = BarcodeLayout::itfWidthPx(units);

        EXPECT_GE(left, BarcodeLayout::kBackingX) << d << " digits start left of the white";
        EXPECT_LE(left + width, BarcodeLayout::kBackingX + BarcodeLayout::kBackingW)
            << d << " digits end right of the white";
    }
}

/// INVARIANT. Every element is a whole number of pixels, so no bar edge is
/// ever anti-aliased. That is the whole reason ITF is laid out differently
/// from Code 128, and the panel's four levels a channel is why it matters.
TEST(ItfPanel, EveryElementIsAWholeNumberOfPixels)
{
    for (size_t d : itfLengths()) {
        const int16_t unitPx = BarcodeLayout::itfUnitPx(Itf::unitsFor(d));
        EXPECT_GE(unitPx, 1) << d << " digits";
        EXPECT_LE(unitPx, BarcodeLayout::kItfMaxUnitPx) << d << " digits";
    }
    EXPECT_EQ(BarcodeLayout::kLevelsPerChannel, 4) << "the reason whole pixels matter";
}

/// INVARIANT. The symbol is centred on the panel, not on the band, to within
/// the half pixel an even panel allows.
///
/// A symbol an odd number of pixels wide cannot sit exactly centred on a
/// 240px panel -- the centre is 119.5, the same half pixel the lit-circle
/// model already carries -- so the left margin is the floor and the spare
/// pixel goes to the right. itfQuietPx() reports the left side for that
/// reason: it is the smaller of the two, so a quiet-zone claim made from it
/// holds on both.
TEST(ItfPanel, TheSymbolIsCentredOnThePanelToWithinAPixel)
{
    for (size_t d : itfLengths()) {
        const uint16_t units = Itf::unitsFor(d);
        const int16_t left  = BarcodeLayout::itfLeftPx(units);
        const int16_t width = BarcodeLayout::itfWidthPx(units);

        const int16_t rightMargin = BarcodeLayout::kPanelWidth - (left + width);
        EXPECT_GE(rightMargin, left) << d << " digits lean right";
        EXPECT_LE(rightMargin - left, 1) << d << " digits are more than a pixel off centre";
        EXPECT_EQ(width % 2, 2 * left + width == BarcodeLayout::kPanelWidth ? 0 : 1)
            << d << " digits: only an odd width may be a pixel off";
    }
}

/// INVARIANT. Both bearer bars and the bars between them fit the band, and the
/// bars keep a usable height. A bearer that ate the symbol would defeat itself.
TEST(ItfPanel, TheBearerBarsAndTheBarsFitTheBand)
{
    for (size_t d : itfLengths()) {
        const int16_t unitPx = BarcodeLayout::itfUnitPx(Itf::unitsFor(d));
        const int16_t bearer = BarcodeLayout::itfBearerPx(unitPx);
        const int16_t bars   = BarcodeLayout::itfBarsHeightPx(unitPx);

        EXPECT_GE(bearer, 2 * unitPx) << d << " digits: the bearer is under two elements";
        EXPECT_GE(bearer, 4) << d << " digits: the bearer is thinner than the 4px floor";
        EXPECT_EQ(2 * bearer + bars, BarcodeLayout::kBarsH) << d << " digits";
        EXPECT_GT(bars, BarcodeLayout::kBarsH / 2)
            << d << " digits: the bearers have taken more than half the height";
    }
}

/// INVARIANT. Every row the ITF symbol occupies is lit across the full backing
/// width, so nothing the bezel cuts can clip a bar or a bearer. The band is
/// shared with Code 128, which already holds this, but ITF puts ink on the top
/// and bottom rows of it and Code 128 does not.
TEST(ItfPanel, EveryRowOfTheBandIsLitAcrossTheBacking)
{
    for (int16_t y = BarcodeLayout::kBarsY; y < BarcodeLayout::kBarsY + BarcodeLayout::kBarsH; y++) {
        EXPECT_TRUE(BarcodeLayout::rowIsLit(BarcodeLayout::kBackingX, y, BarcodeLayout::kBackingW))
            << "row " << y << " is cut by the bezel";
    }
}

/// CHARACTERISATION. What each length actually comes out at. The element drops
/// to a single pixel from ten digits up, which is 126 um -- the 5 mil
/// mid-density reference exactly, and the floor for this symbology on this
/// panel. There is no encoding trick left: the quiet zone is already the
/// binding constraint and the backing cannot grow without leaving the circle.
TEST(ItfPanel, CurrentlyTenDigitsAndUpAreAOnePixelElement)
{
    for (size_t d : itfLengths()) {
        const int16_t unitPx = BarcodeLayout::itfUnitPx(Itf::unitsFor(d));
        const int16_t expected = d <= 2 ? 4 : d <= 4 ? 3 : d <= 8 ? 2 : 1;
        EXPECT_EQ(unitPx, expected)
            << d << " digits; if this improved, tighten this test";
    }
}

/// Reporting, so the whole picture is in one place.
TEST(ItfPanel, ReportTheWholeRange)
{
    for (size_t d : itfLengths()) {
        const uint16_t units  = Itf::unitsFor(d);
        const int16_t  unitPx = BarcodeLayout::itfUnitPx(units);
        std::cout << "  " << (d < 10 ? " " : "") << d << " digits"
                  << "  units " << units
                  << "  element " << unitPx << " px"
                  << "  X " << unitPx * BarcodeLayout::kDotPitchMicrons << " um"
                  << "  width " << BarcodeLayout::itfWidthPx(units) << " px"
                  << "  quiet " << BarcodeLayout::itfQuietPx(units) << " px"
                  << "  bearer " << BarcodeLayout::itfBearerPx(unitPx) << " px"
                  << std::endl;
    }
}

} // namespace
