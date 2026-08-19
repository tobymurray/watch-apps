/**
 * @file SceneRender_test.cpp
 * @brief That the renderer draws the specified map, and says so when it did
 *        not.
 *
 * The stats are the contract. A render that clipped a feature or dropped a
 * span is faster than one that did not, so the benchmark beside it is only
 * meaningful if incompleteness is reported rather than absorbed.
 */
#include <gtest/gtest.h>

#include <Canvas.hpp>
#include <SceneRender.hpp>
#include <VecScene.hpp>

#include <vector>

namespace {

using namespace MapLab;

struct Fixture {
    std::vector<uint8_t> scene;
    std::vector<uint8_t> px;
    std::vector<Pt>      scratch;
    Canvas               canvas;
    SceneReader          reader;
    uint32_t             bytes = 0;

    explicit Fixture(const SceneParams& p)
        : scene(48 * 1024, 0), px(240 * 240, code(Slot::Paper)),
          scratch(kMaxPointsPerFeature), canvas(px.data(), 240, 240)
    {
        bytes = generateScene(scene.data(), static_cast<uint32_t>(scene.size()), p);
        EXPECT_GT(bytes, 0u);
        EXPECT_TRUE(reader.open(scene.data(), bytes));
    }

    RenderStats render()
    {
        canvas.clear(code(Slot::Paper));
        return renderScene(reader, canvas, scratch.data(),
                           static_cast<int>(scratch.size()), 0, 0, 240);
    }

    int inked() const
    {
        int n = 0;
        for (uint8_t v : px) {
            if (v != code(Slot::Paper)) {
                ++n;
            }
        }
        return n;
    }
};

TEST(SceneRender, StrokesComeFromTheSpecTable)
{
    // MAP_CARTOGRAPHY_SPEC.md § 4, which is the whole style: 4 px major road
    // with a 7 px halo, 2 px minor with 5, a dashed 2 px path, 1 px contour.
    EXPECT_EQ(strokeFor(Slot::RoadMajor).width, 4);
    EXPECT_EQ(strokeFor(Slot::RoadMajor).casing, 7);
    EXPECT_EQ(strokeFor(Slot::RoadMinor).width, 2);
    EXPECT_EQ(strokeFor(Slot::RoadMinor).casing, 5);
    EXPECT_EQ(strokeFor(Slot::Path).dashOn, 3);
    EXPECT_EQ(strokeFor(Slot::Path).dashOff, 3);
    EXPECT_EQ(strokeFor(Slot::Path).casing, 0);
    EXPECT_EQ(strokeFor(Slot::Contour).width, 1);
}

TEST(SceneRender, DrawsEveryFeatureAndSaysHowMany)
{
    const SceneParams p = SceneParams::suburban();
    Fixture f(p);
    const RenderStats st = f.render();
    EXPECT_EQ(st.features, p.featureTotal());
    EXPECT_GT(st.points, 0u);
    EXPECT_EQ(st.clipped, 0u);
    EXPECT_EQ(st.droppedSpans, 0u);
    EXPECT_GT(f.inked(), 240 * 240 / 20) << "a scene that inks almost nothing "
                                            "is not a map and not a benchmark";
}

TEST(SceneRender, CasingIsASecondPassOverTheSameLayer)
{
    // Cased layers are decoded twice, once for halo and once for ink. That is
    // a real cost the benchmark must not hide, so the point count reflects it.
    SceneParams p = SceneParams::rural();
    p.roadMinor = 0; p.paths = 0; p.contours = 0; p.waterLines = 0;
    p.waterPolys = 0; p.woodPolys = 0; p.landusePolys = 0; p.buildings = 0;
    p.roadMajor = 4;
    Fixture f(p);
    const RenderStats st = f.render();
    // Four roads of pointsPerLine points, decoded twice.
    EXPECT_EQ(st.points, 4u * p.pointsPerLine * 2u);
}

TEST(SceneRender, RenderingIsRepeatable)
{
    Fixture f(SceneParams::cityCentre());
    const RenderStats a = f.render();
    const std::vector<uint8_t> first = f.px;
    const RenderStats b = f.render();
    EXPECT_EQ(a.points, b.points);
    EXPECT_EQ(first, f.px);
}

TEST(SceneRender, ASmallScratchBufferIsReportedAsClipping)
{
    // The scratch buffer is what a device renderer has, and one too small for
    // a feature draws part of it. That must show up in the stats, because the
    // timing next to it would otherwise look like a win.
    std::vector<uint8_t> scene(48 * 1024, 0);
    SceneParams p = SceneParams::rural();
    p.pointsPerLine = 40;
    const uint32_t bytes = generateScene(scene.data(),
                                         static_cast<uint32_t>(scene.size()), p);
    ASSERT_GT(bytes, 0u);
    SceneReader r;
    ASSERT_TRUE(r.open(scene.data(), bytes));

    std::vector<uint8_t> px(240 * 240, code(Slot::Paper));
    Canvas canvas(px.data(), 240, 240);
    Pt tiny[8];
    const RenderStats st = renderScene(r, canvas, tiny, 8, 0, 0, 240);
    EXPECT_GT(st.clipped, 0u);
}

TEST(SceneRender, OverzoomDrawsTheSameGeometryLarger)
{
    // A sparse zoom ladder means drawing a tile at more pixels than it was
    // generalised for. The renderer must handle it as a scale, not refuse it;
    // whether it *looks* right is card C6's question, not a test's.
    Fixture f(SceneParams::suburban());
    f.canvas.clear(code(Slot::Paper));
    const RenderStats a = renderScene(f.reader, f.canvas, f.scratch.data(),
                                      static_cast<int>(f.scratch.size()), 0, 0, 240);
    const int inkedNative = f.inked();

    f.canvas.clear(code(Slot::Paper));
    const RenderStats b = renderScene(f.reader, f.canvas, f.scratch.data(),
                                      static_cast<int>(f.scratch.size()),
                                      -120, -120, 480);
    EXPECT_EQ(a.features, b.features);
    EXPECT_GT(f.inked(), 0);
    EXPECT_GT(inkedNative, 0);
}

} // namespace
