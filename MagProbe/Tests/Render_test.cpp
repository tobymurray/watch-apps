#include "Canvas.hpp"
#include "Font5x7.hpp"
#include "Render.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace {

constexpr uint16_t kW = 240;
constexpr uint16_t kH = 240;

/// A framebuffer with a guard band on each side, so any write outside the
/// declared geometry is detectable rather than merely undefined.
class Frame {
public:
    static constexpr size_t kGuard = 64;
    static constexpr uint8_t kGuardByte = 0xA5;

    Frame(uint16_t w = kW, uint16_t h = kH)
        : mStore(kGuard + static_cast<size_t>(w) * h + kGuard, kGuardByte)
        , mCanvas(mStore.data() + kGuard, w, h, static_cast<size_t>(w) * h)
    {
    }

    Canvas& canvas() { return mCanvas; }

    bool guardsIntact() const
    {
        for (size_t i = 0; i < kGuard; ++i) {
            if (mStore[i] != kGuardByte) {
                return false;
            }
            if (mStore[mStore.size() - 1 - i] != kGuardByte) {
                return false;
            }
        }
        return true;
    }

    size_t countColour(uint8_t colour) const
    {
        size_t n = 0;
        for (size_t i = kGuard; i < mStore.size() - kGuard; ++i) {
            if (mStore[i] == colour) {
                ++n;
            }
        }
        return n;
    }

private:
    std::vector<uint8_t> mStore;
    Canvas               mCanvas;
};

Render::View delivering()
{
    Render::View v;
    v.resolve      = Mag::Resolve::Resolved;
    v.delivery     = Mag::Delivery::Delivering;
    v.shape        = Mag::FrameShape::ThreeAxis;
    v.fieldCount   = 3;
    v.stride       = 24;
    v.samples      = 1200;
    v.batches      = 100;
    v.units        = Mag::Units::Microtesla;
    v.raw          = Mag::Vec3{12.5f, -30.25f, 40.0f};
    v.corrected    = v.raw;
    v.magnitude    = 51.0f;
    v.spreadFraction = 0.02f;
    v.accelResolve = Mag::Resolve::Resolved;
    v.accelFresh   = true;
    v.haveHeading  = true;
    v.headingDeg   = 137.5f;
    v.dipDeg       = 68.2f;
    v.levelled     = true;
    v.calQuality   = Mag::HardIron::Quality::Usable;
    v.calSamples   = 400;
    v.uptimeMs     = 12345;
    return v;
}

} // namespace

TEST(Canvas, ColourEncodingIsAbgr2222WithFullAlpha)
{
    EXPECT_EQ(Canvas::kBlack, 0xC0);
    EXPECT_EQ(Canvas::kWhite, 0xFF);
    EXPECT_EQ(Canvas::rgb(3, 0, 0), 0xC3);
    EXPECT_EQ(Canvas::rgb(0, 3, 0), 0xCC);
    EXPECT_EQ(Canvas::rgb(0, 0, 3), 0xF0);
}

TEST(Canvas, EveryPrimitiveClipsRatherThanOverrunning)
{
    Frame f;
    Canvas& c = f.canvas();

    c.clear(Canvas::kBlack);
    c.pixel(-1, -1, Canvas::kWhite);
    c.pixel(kW, kH, Canvas::kWhite);
    c.pixel(kW + 1000, 5, Canvas::kWhite);
    c.fillRect(-50, -50, 500, 500, Canvas::kWhite);
    c.line(-200, -200, 500, 500, Canvas::kWhite);
    c.text(-100, -100, "OVERRUN", Canvas::kWhite, 4);
    c.text(kW - 4, kH - 4, "OVERRUN", Canvas::kWhite, 4);

    EXPECT_TRUE(f.guardsIntact());
}

TEST(Canvas, AGeometryThatDoesNotFitIsRefusedRatherThanClipped)
{
    uint8_t small[16] = {0};
    Canvas  c(small, 240, 240, sizeof(small));

    EXPECT_FALSE(c.usable());
    EXPECT_EQ(c.width(), 0);

    // And drawing into it is inert rather than fatal.
    c.clear(Canvas::kWhite);
    c.text(0, 0, "HELLO", Canvas::kWhite);
    for (uint8_t b : small) {
        EXPECT_EQ(b, 0);
    }
}

TEST(Canvas, ANullBufferIsUnusable)
{
    Canvas c(nullptr, 240, 240, 240 * 240);
    EXPECT_FALSE(c.usable());
    c.clear(Canvas::kWhite);
}

TEST(Canvas, TextWidthMatchesWhatTextActuallyDraws)
{
    Frame f;
    Canvas& c = f.canvas();
    c.clear(Canvas::kBlack);

    // "1" has ink only in its middle columns, so a glyph whose left column is
    // blank would still start at the advertised origin.
    const int32_t end = c.text(10, 10, "WW", Canvas::kWhite);
    EXPECT_EQ(end - 10, Canvas::textWidth("WW"));

    EXPECT_EQ(Canvas::textWidth(""), 0);
    EXPECT_EQ(Canvas::textWidth(nullptr), 0);
    EXPECT_EQ(Canvas::textWidth("A"), Font5x7::kWidth);
    EXPECT_EQ(Canvas::textWidth("AA"), 2 * Font5x7::kWidth + 1);
    EXPECT_EQ(Canvas::textWidth("A", 2), 2 * Font5x7::kWidth);
}

TEST(Canvas, ScaleMultipliesEveryInkPixel)
{
    Frame one;
    Frame two;
    one.canvas().clear(Canvas::kBlack);
    two.canvas().clear(Canvas::kBlack);

    one.canvas().text(10, 10, "MAGPROBE 123", Canvas::kWhite, 1);
    two.canvas().text(10, 10, "MAGPROBE 123", Canvas::kWhite, 2);

    const size_t inkOne = one.countColour(Canvas::kWhite);
    const size_t inkTwo = two.countColour(Canvas::kWhite);

    ASSERT_GT(inkOne, 0u);
    EXPECT_EQ(inkTwo, inkOne * 4) << "a 2x glyph is four times the ink";
}

TEST(Font, TheTableAndTheCharacterRangeAgree)
{
    // The static_assert in the header covers this at compile time; this pins
    // the specific glyphs whose position a table edit would silently shift.
    EXPECT_EQ(Font5x7::glyph('0'), Font5x7::kGlyphs[static_cast<uint8_t>('0' - ' ')]);
    EXPECT_EQ(Font5x7::glyph('A'), Font5x7::kGlyphs[static_cast<uint8_t>('A' - ' ')]);
    EXPECT_EQ(Font5x7::glyph('Z'), Font5x7::kGlyphs[static_cast<uint8_t>('Z' - ' ')]);
}

TEST(Font, LowercaseFoldsOntoUppercase)
{
    EXPECT_EQ(Font5x7::glyph('a'), Font5x7::glyph('A'));
    EXPECT_EQ(Font5x7::glyph('z'), Font5x7::glyph('Z'));
}

// An unmapped character has to be visible as a fault. Rendering it as a space
// would mean the string on the screen is not the string that was written and
// nothing says so.
TEST(Font, UnmappedCharactersRenderAsAVisibleBoxNotASpace)
{
    EXPECT_EQ(Font5x7::glyph('~'), Font5x7::kMissing);
    EXPECT_EQ(Font5x7::glyph('{'), Font5x7::kMissing);
    EXPECT_NE(Font5x7::glyph('~'), Font5x7::glyph(' '));

    Frame f;
    f.canvas().clear(Canvas::kBlack);
    f.canvas().text(10, 10, "~", Canvas::kWhite);
    EXPECT_GT(f.countColour(Canvas::kWhite), 0u);
}

TEST(Render, EveryScreenStaysInsideItsBuffer)
{
    const Render::View views[] = {Render::View{}, delivering()};

    for (const Render::View& v : views) {
        for (uint8_t s = 0; s < Render::kScreenCount; ++s) {
            Frame f;
            Render::render(f.canvas(), static_cast<Render::Screen>(s), v);
            EXPECT_TRUE(f.guardsIntact()) << "screen " << static_cast<int>(s);
        }
    }
}

TEST(Render, EveryScreenDrawsSomething)
{
    for (uint8_t s = 0; s < Render::kScreenCount; ++s) {
        Frame f;
        Render::render(f.canvas(), static_cast<Render::Screen>(s), delivering());

        const size_t black = f.countColour(Canvas::kBlack);
        EXPECT_LT(black, static_cast<size_t>(kW) * kH)
            << "screen " << static_cast<int>(s) << " is entirely background";
    }
}

TEST(Render, ItRendersOnASmallerDisplayToo)
{
    // The GUI queries the display geometry rather than assuming it, so the
    // renderer has to cope with whatever comes back.
    Frame f(180, 180);
    Render::render(f.canvas(), Render::Screen::Verdict, delivering());
    EXPECT_TRUE(f.guardsIntact());
}

// The property that matters most on the verdict screen: an unanswered question
// must never look like a yes.
TEST(Render, AnUnansweredVerdictIsNeverDrawnGreen)
{
    Render::View v;  // NotAsked, nothing measured
    Frame f;
    Render::render(f.canvas(), Render::Screen::Verdict, v);

    EXPECT_EQ(f.countColour(Canvas::kGreen), 0u);
    EXPECT_GT(f.countColour(Canvas::kYellow), 0u);
}

TEST(Render, ADefiniteNoIsDrawnRedAndNeverGreen)
{
    Render::View v;
    v.resolve = Mag::Resolve::NoProducer;

    Frame f;
    Render::render(f.canvas(), Render::Screen::Verdict, v);

    EXPECT_GT(f.countColour(Canvas::kRed), 0u);
    EXPECT_EQ(f.countColour(Canvas::kGreen), 0u);
}

TEST(Render, DeliveringZerosIsNotAYes)
{
    // A driver that resolves and delivers frames of zeros passes every check
    // except the one that matters.
    Render::View v = delivering();
    v.units     = Mag::Units::AllZero;
    v.raw       = Mag::Vec3{};
    v.magnitude = 0.0f;

    Frame f;
    Render::render(f.canvas(), Render::Screen::Verdict, v);
    EXPECT_EQ(f.countColour(Canvas::kGreen), 0u);
}

TEST(Render, TheCompassSaysWhyItHasNoHeadingRatherThanDrawingNothing)
{
    Render::View v = delivering();
    v.haveHeading  = false;
    v.accelResolve = Mag::Resolve::NoProducer;

    Frame f;
    Render::render(f.canvas(), Render::Screen::Compass, v);

    EXPECT_TRUE(f.guardsIntact());
    EXPECT_GT(f.countColour(Canvas::kRed), 0u) << "the reason is drawn in red";
}

TEST(Render, TheNeedleMovesWithTheHeading)
{
    Render::View north = delivering();
    north.headingDeg   = 0.0f;

    Render::View east = delivering();
    east.headingDeg   = 90.0f;

    Frame fn;
    Frame fe;
    Render::render(fn.canvas(), Render::Screen::Compass, north);
    Render::render(fe.canvas(), Render::Screen::Compass, east);

    // Same amount of needle, in a different place.
    EXPECT_GT(fn.countColour(Canvas::kRed), 0u);
    EXPECT_GT(fe.countColour(Canvas::kRed), 0u);

    bool differs = false;
    for (int32_t y = 0; y < kH && !differs; ++y) {
        for (int32_t x = 0; x < kW; ++x) {
            if (fn.canvas().at(x, y) != fe.canvas().at(x, y)) {
                differs = true;
                break;
            }
        }
    }
    EXPECT_TRUE(differs);
}

TEST(Render, AMovingOrUncalibratedHeadingIsMarked)
{
    Render::View moving = delivering();
    moving.levelled     = false;

    Frame f;
    Render::render(f.canvas(), Render::Screen::Compass, moving);
    EXPECT_GT(f.countColour(Canvas::kYellow), 0u);
}

TEST(Render, ScreenNamesAreStableAndDistinct)
{
    for (uint8_t a = 0; a < Render::kScreenCount; ++a) {
        for (uint8_t b = 0; b < Render::kScreenCount; ++b) {
            if (a == b) {
                continue;
            }
            EXPECT_STRNE(Render::name(static_cast<Render::Screen>(a)),
                         Render::name(static_cast<Render::Screen>(b)));
        }
    }
}

TEST(Render, TheLivenessMarkerMovesWithTheClockNotTheFrameCount)
{
    Render::View a = delivering();
    a.uptimeMs     = 0;
    a.frames       = 0;

    Render::View b = delivering();
    b.uptimeMs     = 1600;  // half a lap
    b.frames       = 0;     // same frame count on purpose

    Frame fa;
    Frame fb;
    Render::render(fa.canvas(), Render::Screen::Verdict, a);
    Render::render(fb.canvas(), Render::Screen::Verdict, b);

    // The whole frame, rather than the marker's row: uptime drives nothing else
    // on this screen, so any difference at all is the marker having moved, and
    // the assertion survives the marker being relocated.
    bool differs = false;
    for (int32_t y = 0; y < kH && !differs; ++y) {
        for (int32_t x = 0; x < kW; ++x) {
            if (fa.canvas().at(x, y) != fb.canvas().at(x, y)) {
                differs = true;
                break;
            }
        }
    }
    EXPECT_TRUE(differs) << "the marker is positioned from uptime";
}

// The bug this file did not catch until a screen dump was looked at.
//
// The panel masks a circle out of the square framebuffer, so a fixed
// rectangular inset is drawn off the glass near the top and bottom, where the
// circle is narrow. At y = 10 the visible width is under 90 px, and a line
// inset 22 px from the left started 50 px inside the mask: on the device the
// header and the first letters of every label were simply not there.
//
// Ink outside the mask is invisible on hardware and invisible in every host
// test that only counts pixels, which is exactly why this asserts geometry.
TEST(Render, NothingIsDrawnWhereTheBezelWouldHideIt)
{
    // Exhaustive over the verdict states rather than over a couple of hand-made
    // fixtures. The first version of this test used two fixtures, neither of
    // which produced a long verdict string, so it passed while the longest
    // headline on the app's most important screen was being drawn off the glass.
    std::vector<Render::View> views;
    views.push_back(Render::View{});
    views.push_back(delivering());

    for (uint8_t r = 0; r <= static_cast<uint8_t>(Mag::Resolve::Resolved); ++r) {
        for (uint8_t d = 0; d <= static_cast<uint8_t>(Mag::Delivery::Stalled); ++d) {
            for (uint8_t sh = 0; sh <= static_cast<uint8_t>(Mag::FrameShape::Wider); ++sh) {
                for (uint8_t u = 0; u <= static_cast<uint8_t>(Mag::Units::Unclassified); ++u) {
                    Render::View v = delivering();
                    v.resolve  = static_cast<Mag::Resolve>(r);
                    v.delivery = static_cast<Mag::Delivery>(d);
                    v.shape    = static_cast<Mag::FrameShape>(sh);
                    v.units    = static_cast<Mag::Units>(u);
                    views.push_back(v);
                }
            }
        }
    }

    // Every calibration state too, since those strings differ in length.
    for (uint8_t q = 0; q <= static_cast<uint8_t>(Mag::HardIron::Quality::Usable); ++q) {
        Render::View v = delivering();
        v.calQuality  = static_cast<Mag::HardIron::Quality>(q);
        v.calibrating = (q % 2) == 0;
        v.haveHeading = false;
        views.push_back(v);
    }

    // Same circle the panel masks with, and the same margin Canvas reserves.
    const double cx = (kW - 1) / 2.0;
    const double cy = (kH - 1) / 2.0;
    const double r  = kW / 2.0 - Canvas::kBezelMargin;

    for (const Render::View& v : views) {
        for (uint8_t sc = 0; sc < Render::kScreenCount; ++sc) {
            Frame f;
            Render::render(f.canvas(), static_cast<Render::Screen>(sc), v);

            size_t  outside = 0;
            int32_t worstX  = -1;
            int32_t worstY  = -1;

            for (int32_t y = 0; y < kH; ++y) {
                for (int32_t x = 0; x < kW; ++x) {
                    if (f.canvas().at(x, y) == Canvas::kBlack) {
                        continue;
                    }
                    const double dx = x - cx;
                    const double dy = y - cy;
                    if (dx * dx + dy * dy > r * r) {
                        ++outside;
                        if (worstX < 0) {
                            worstX = x;
                            worstY = y;
                        }
                    }
                }
            }

            EXPECT_EQ(outside, 0u)
                << "screen " << Render::name(static_cast<Render::Screen>(sc))
                << " draws " << outside << " pixels under the bezel, first at ("
                << worstX << ", " << worstY << ")";
        }
    }
}

TEST(Canvas, SafeLeftFollowsTheCircle)
{
    Frame f;
    const Canvas& c = f.canvas();

    // Widest at the centre row, narrowest at the extremes.
    const int32_t middle = c.safeLeft(kH / 2, 1);
    const int32_t high   = c.safeLeft(20, 7);
    const int32_t low    = c.safeLeft(kH - 27, 7);

    EXPECT_GE(middle, 0);
    EXPECT_LT(middle, high) << "the centre row has more room than the top";
    EXPECT_LT(middle, low) << "and more than the bottom";

    // A run's constraint comes from whichever end is farther from the centre.
    EXPECT_GE(c.safeLeft(20, 7), c.safeLeft(20, 1));
}

TEST(Canvas, SafeLeftRefusesARowWithNoWidth)
{
    Frame f;
    EXPECT_LT(f.canvas().safeLeft(-40, 7), 0);
    EXPECT_LT(f.canvas().safeLeft(kH + 40, 7), 0);
}

TEST(Canvas, SafeLeftIsSafeOnAnUnusableCanvas)
{
    Canvas c(nullptr, 240, 240, 240 * 240);
    EXPECT_LT(c.safeLeft(100, 7), 0);
    EXPECT_LT(c.safeRight(100, 7), 0);
}

TEST(Canvas, SafeRightMirrorsSafeLeft)
{
    Frame f;
    const Canvas& c = f.canvas();
    for (int32_t y = 20; y < kH - 20; y += 17) {
        const int32_t l = c.safeLeft(y, 7);
        const int32_t rr = c.safeRight(y, 7);
        ASSERT_GE(l, 0);
        EXPECT_EQ(rr, static_cast<int32_t>(kW) - l);
    }
}

// Every verdict headline has to be readable, which means it has to fit. The one
// that did not was the longest, and the longest is the one that matters most:
// "no compass" is the answer this app exists to be able to give.
TEST(Render, EveryVerdictHeadlineFitsAtDoubleSize)
{
    for (uint8_t r = 0; r <= static_cast<uint8_t>(Mag::Resolve::Resolved); ++r) {
        for (uint8_t d = 0; d <= static_cast<uint8_t>(Mag::Delivery::Stalled); ++d) {
            for (uint8_t sh = 0; sh <= static_cast<uint8_t>(Mag::FrameShape::Wider); ++sh) {
                const Mag::Verdict v = Mag::verdict(static_cast<Mag::Resolve>(r),
                                                    static_cast<Mag::Delivery>(d),
                                                    static_cast<Mag::FrameShape>(sh));

                Frame f;
                const uint8_t scale =
                    f.canvas().textCentredFitted(40, v.headline, Canvas::kWhite, 2);

                EXPECT_EQ(scale, 2u)
                    << "headline '" << v.headline << "' does not fit at double size";
                EXPECT_TRUE(f.guardsIntact());
            }
        }
    }
}

TEST(Canvas, FittedTextShrinksRatherThanOverflowing)
{
    Frame f;
    Canvas& c = f.canvas();

    // Short enough for double size.
    EXPECT_EQ(c.textCentredFitted(110, "NORTH", Canvas::kWhite, 2), 2u);

    // Too wide at double, fits at single.
    c.clear(Canvas::kBlack);
    EXPECT_EQ(c.textCentredFitted(110, "A STRING OF SOME LENGTH", Canvas::kWhite, 2), 1u);

    // Too wide even at single: nothing is drawn rather than something clipped.
    c.clear(Canvas::kBlack);
    const char* huge = "THIS STRING IS FAR TOO LONG TO FIT ON A TINY ROUND DISPLAY";
    EXPECT_EQ(c.textCentredFitted(110, huge, Canvas::kWhite, 2), 0u);
    EXPECT_EQ(f.countColour(Canvas::kWhite), 0u);
    EXPECT_TRUE(f.guardsIntact());
}
