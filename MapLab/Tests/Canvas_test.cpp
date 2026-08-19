/**
 * @file Canvas_test.cpp
 * @brief The rasteriser, pinned by exact pixel counts.
 *
 * Counting pixels rather than eyeballing a screenshot is the point. Every bug
 * this file guards against -- an inclusive span, a clip that lets one pixel
 * through, a dash phase that drifts -- is invisible on a 240 px panel and
 * fatal to a benchmark, because a rasteriser that draws slightly less is
 * measurably faster and looks identical.
 */
#include <gtest/gtest.h>

#include <Canvas.hpp>

#include <vector>

namespace {

using namespace MapLab;

constexpr uint8_t kBg = 0xFF;
constexpr uint8_t kFg = 0xC0;

struct Surface {
    std::vector<uint8_t> px;
    Canvas               canvas;

    Surface(int16_t w = 64, int16_t h = 64)
        : px(static_cast<size_t>(w) * h, kBg), canvas(px.data(), w, h) {}

    int count(uint8_t code) const
    {
        int n = 0;
        for (uint8_t v : px) {
            if (v == code) {
                ++n;
            }
        }
        return n;
    }
    uint8_t at(int16_t x, int16_t y) const
    {
        return px[static_cast<size_t>(y) * canvas.width() + x];
    }
};

TEST(Canvas, ClearFillsExactlyTheBuffer)
{
    Surface s(10, 7);
    s.canvas.clear(kFg);
    EXPECT_EQ(s.count(kFg), 70);
    EXPECT_EQ(s.canvas.byteCount(), 70u);
}

TEST(Canvas, EveryPrimitiveClips)
{
    Surface s;
    s.canvas.plot(-1, 5, kFg);
    s.canvas.plot(5, -1, kFg);
    s.canvas.plot(64, 5, kFg);
    s.canvas.plot(5, 64, kFg);
    EXPECT_EQ(s.count(kFg), 0);

    s.canvas.fillRect(-5, -5, 10, 10, kFg);
    EXPECT_EQ(s.count(kFg), 25);        // the 5x5 that is on-screen
    EXPECT_EQ(s.at(4, 4), kFg);
    EXPECT_EQ(s.at(5, 5), kBg);

    const Pt away[4] = { { -500, -500 }, { -400, -500 }, { -400, -400 }, { -500, -400 } };
    EXPECT_TRUE(s.canvas.fillPolygon(away, 4, kFg));
    EXPECT_EQ(s.count(kFg), 25);        // unchanged
}

TEST(Canvas, AxisAlignedLineIsExactlyItsLength)
{
    Surface s;
    s.canvas.thickLine(0, 3, 9, 3, 1, kFg);
    EXPECT_EQ(s.count(kFg), 10);
}

TEST(Canvas, ThickLineStampsASquareBrush)
{
    Surface s;
    s.canvas.thickLine(10, 10, 13, 10, 3, kFg);
    // Four steps of a 3x3 brush along one axis: a 6x3 block.
    EXPECT_EQ(s.count(kFg), 6 * 3);
}

TEST(Canvas, DashPhaseIsThreeOnThreeOff)
{
    Surface s;
    s.canvas.dashedLine(0, 2, 11, 2, 1, 3, 3, kFg);
    for (int16_t x = 0; x < 12; ++x) {
        const bool on = ((x % 6) < 3);
        EXPECT_EQ(s.at(x, 2), on ? kFg : kBg) << "x=" << x;
    }
}

TEST(Canvas, PolygonFillIsHalfOpenInBothAxes)
{
    // The property that matters for a map: two polygons sharing an edge must
    // paint the shared column once between them, not twice.
    Surface s;
    const Pt square[4] = { { 10, 10 }, { 30, 10 }, { 30, 30 }, { 10, 30 } };
    ASSERT_TRUE(s.canvas.fillPolygon(square, 4, kFg));
    EXPECT_EQ(s.count(kFg), 20 * 20);
    EXPECT_EQ(s.at(10, 10), kFg);
    EXPECT_EQ(s.at(29, 29), kFg);
    EXPECT_EQ(s.at(30, 20), kBg);   // right edge excluded
    EXPECT_EQ(s.at(20, 30), kBg);   // bottom edge excluded
    EXPECT_EQ(s.canvas.dropped(), 0u);
}

TEST(Canvas, AdjacentPolygonsDoNotOverlap)
{
    Surface s;
    const Pt left[4]  = { { 0, 0 }, { 20, 0 }, { 20, 20 }, { 0, 20 } };
    const Pt right[4] = { { 20, 0 }, { 40, 0 }, { 40, 20 }, { 20, 20 } };
    s.canvas.fillPolygon(left, 4, kFg);
    s.canvas.fillPolygon(right, 4, 0xC1);
    // If the fill were inclusive in x, column 20 would belong to both and the
    // first colour would survive in it.
    EXPECT_EQ(s.at(20, 5), 0xC1);
    EXPECT_EQ(s.count(kFg), 20 * 20);
    EXPECT_EQ(s.count(0xC1), 20 * 20);
}

TEST(Canvas, DegenerateShapesAreNotFailures)
{
    Surface s;
    const Pt two[2] = { { 1, 1 }, { 5, 5 } };
    EXPECT_TRUE(s.canvas.fillPolygon(two, 2, kFg));
    EXPECT_TRUE(s.canvas.fillPolygon(nullptr, 9, kFg));
    EXPECT_EQ(s.count(kFg), 0);
    EXPECT_EQ(s.canvas.dropped(), 0u);
}

TEST(Canvas, ASinglePointPolylineIsDrawnNotDropped)
{
    Surface s;
    const Pt one[1] = { { 8, 8 } };
    s.canvas.polyline(one, 1, 3, kFg);
    EXPECT_EQ(s.count(kFg), 9);
}

TEST(Canvas, CrossingOverflowIsCountedNotSwallowed)
{
    // A star with more crossings per scanline than the budget allows. The
    // shape is then drawn wrong -- what must not happen is that it is drawn
    // wrong *quietly*, because the timing beside it would look like a result.
    Surface s(64, 64);
    std::vector<Pt> comb;
    for (int i = 0; i < Canvas::kMaxCrossings + 8; ++i) {
        const int16_t x = static_cast<int16_t>(i);
        comb.push_back({ x, static_cast<int16_t>((i % 2) ? 5 : 40) });
    }
    const bool ok = s.canvas.fillPolygon(comb.data(), static_cast<int>(comb.size()), kFg);
    EXPECT_FALSE(ok);
    EXPECT_GT(s.canvas.dropped(), 0u);

    s.canvas.resetDropped();
    EXPECT_EQ(s.canvas.dropped(), 0u);
}

TEST(Canvas, LutIsAppliedToEveryPixelAndIsIndexedByTheLowSixBits)
{
    Surface s(4, 4);
    s.canvas.clear(0xFF);
    uint8_t lut[kLutEntries];
    for (int i = 0; i < kLutEntries; ++i) {
        lut[i] = 0xC0;
    }
    s.canvas.applyLut(lut);
    EXPECT_EQ(s.count(0xC0), 16);
}

} // namespace
