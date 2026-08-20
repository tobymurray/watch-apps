/**
 * The panel is round and the framebuffer is square. Every widget rectangle the
 * report screen uses has to fit inside the glass, and this file is where that
 * stops being a comment somebody wrote and starts being a thing that fails.
 *
 * It is here because the failure is silent: a box outside the chord is written
 * to the framebuffer exactly as asked, costs no error and no warning, and simply
 * never appears. Nothing in a build, a simulator run or a screenshot taken at
 * the wrong moment catches it. Two widgets shipped clipped -- the honesty line
 * and the caption, which are the two whose entire job is to be read.
 */
#include <gtest/gtest.h>

#include "gui/common/RoundPanel.hpp"
#include "gui/main_screen/MainViewLayout.hpp"

#include "Engine/RestfulnessBand.hpp"

#include <cmath>

namespace {

using namespace RoundPanel;

TEST(RoundPanel, TheChordIsWidestAtTheCentreAndZeroAtTheEdges)
{
    EXPECT_NEAR(halfWidthAt(kRadius), static_cast<float>(kRadius), 0.01f);
    EXPECT_NEAR(halfWidthAt(0), 0.0f, 0.01f);
    EXPECT_NEAR(halfWidthAt(kSize), 0.0f, 0.01f);
    // Symmetric about the centre, which is the property that lets a single
    // inset be used for both edges of a box.
    for (int16_t d = 1; d < kRadius; d += 17) {
        EXPECT_NEAR(halfWidthAt(static_cast<int16_t>(kRadius - d)),
                    halfWidthAt(static_cast<int16_t>(kRadius + d)), 0.01f);
    }
}

TEST(RoundPanel, ABoxIsBoundedByItsWorstRowNotItsFirst)
{
    // A box just below the centre is narrower at its *bottom*, and one just
    // above is narrower at its top. Taking the position's row alone -- which is
    // the natural mistake -- is right in one of those two cases and wrong in the
    // other, and the wrong one is the bottom half of the screen.
    EXPECT_LT(halfWidthOfBox(150, 20), halfWidthAt(150));
    EXPECT_LT(halfWidthOfBox(70, 20), halfWidthAt(89));
}

TEST(RoundPanel, TheInsetIsRoundedInwardSoItIsSafeRatherThanExact)
{
    for (int16_t y = 0; y <= kSize - 20; y = static_cast<int16_t>(y + 4)) {
        const int16_t x = insetFor(y, 20);
        const int16_t w = widthFor(y, 20);
        if (w <= 0) { continue; }
        EXPECT_TRUE(fits(x, y, w, 20))
            << "the widest box this helper offers at y=" << y << " does not fit";
    }
}

// -- The layout itself -----------------------------------------------------------

/// A very conservative mean advance for Poppins Medium 16, in pixels.
///
/// The real mean over the strings below is nearer 8.5 and the widest characters
/// reach 14, so a string that passes at 8 px is not guaranteed to fit and a
/// string that fails certainly does not. Deliberately that way round: the test
/// exists to stop somebody lengthening a string past the glass, and a cheap
/// under-estimate does that without dragging the generated font tables into the
/// host build.
constexpr unsigned kPxPerChar = 8;

/// Every string that can land on the first text row.
///
/// The first row is the honesty line and it is the narrowest row anything
/// substantial is drawn in, so it is the row that decides how long these strings
/// may be. Keep this list in step with `MainView::drawReport`'s first-line
/// selection -- a string added there and not here is a string nobody checked.
const char *const kRowZeroStrings[] = {
    "SLEEP LAB",
    "waiting for service...",
    "INTERRUPTED",
    "INCOMPLETE",
    "UNCONFIRMED",
    "NOT WORN",
    "RECORDING",
    "waiting to settle",
    "LAST NIGHT",
    "SLEEP LAB - idle",
};

TEST(RoundPanel, TheFirstRowHoldsEveryStringThatCanAppearOnIt)
{
    // This is the assertion the layout actually needed, and the one a check of
    // "does the box fit the circle" cannot make: since MainView derives its
    // boxes from RoundPanel, they fit by construction and always will. What can
    // still go wrong is a row that fits the glass and not its contents -- which
    // is what shipped. "INTERRUPTED - clock changed" was 239 px in a row that
    // showed 159, so two thirds of the most important line on the screen was
    // never drawn, and nothing anywhere said so.
    const int16_t w = widthFor(MainViewLayout::kFirstLineY, MainViewLayout::kLineHeight);
    for (const char *s : kRowZeroStrings) {
        unsigned n = 0;
        while (s[n] != '\0') { n++; }
        EXPECT_LE(n * kPxPerChar, static_cast<unsigned>(w))
            << '"' << s << "\" is " << n << " characters and the first row is "
            << w << " px. It will be truncated on the watch, and the first row is "
               "where the night says what is wrong with it.";
    }
}

TEST(RoundPanel, TheStripFitsWhereItIsPinned)
{
    // The strip cannot be narrowed to fit: its width is 100 buckets at 2 px, set
    // by the message format (ledger P13). So it is the one widget that has to be
    // *placed* rather than sized, and the margin is small enough to be worth a
    // test rather than a comment.
    const int16_t x = static_cast<int16_t>((kSize - MainViewLayout::kStripW) / 2);
    EXPECT_TRUE(fits(x, MainViewLayout::kStripY, MainViewLayout::kStripW,
                     MainViewLayout::kStripH))
        << "the strip no longer fits at y=" << MainViewLayout::kStripY;
}

TEST(RoundPanel, TheCaptionStillFitsInTheRowItHas)
{
    // The caption is the one string whose length is a design constraint rather
    // than a consequence: it sits in the narrowest row anything is drawn in, and
    // a caption that does not fit is silently truncated into saying something
    // else. 8 px per character is a deliberate under-estimate of Poppins Medium
    // 16 -- the real mean is nearer 8.5 -- so this passing is necessary and not
    // sufficient, and it is here to catch somebody lengthening the string.
    const int16_t w = widthFor(MainViewLayout::kCaptionY, MainViewLayout::kLineHeight);
    const size_t chars = sizeof(Engine::RestfulnessBand::kCaption) - 1;
    EXPECT_LE(chars * 8u, static_cast<size_t>(w))
        << "kCaption is " << chars << " characters and the row is " << w
        << " px; it will be truncated on the watch and say something it does not "
           "mean";
}

} // namespace
