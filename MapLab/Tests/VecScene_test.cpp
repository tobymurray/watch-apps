/**
 * @file VecScene_test.cpp
 * @brief The draft wire format: what it accepts, what it refuses, and what it
 *        must never do quietly.
 *
 * The format is a stand-in, but its *validation* is not a stand-in for
 * anything -- every rule here is one a device reader would have to enforce
 * anyway, and a benchmark timing a reader that skipped them would be timing a
 * reader nobody could ship. The tests that matter most are the negative ones.
 */
#include <gtest/gtest.h>

#include <VecScene.hpp>

#include <cstring>
#include <vector>

namespace {

using namespace MapLab;

std::vector<uint8_t> oneLineTile(const int32_t* xy, int count)
{
    std::vector<uint8_t> buf(4096, 0);
    SceneWriter w(buf.data(), static_cast<uint32_t>(buf.size()));
    EXPECT_TRUE(w.beginLayer(Slot::RoadMajor, Kind::Polyline));
    EXPECT_TRUE(w.addFeature(xy, count));
    EXPECT_TRUE(w.endLayer());
    EXPECT_TRUE(w.finish());
    buf.resize(w.size());
    return buf;
}

TEST(VecScene, RoundTripsGeometryThroughTheScreenTransform)
{
    const int32_t xy[6] = { 0, 0, 2048, 1024, 4095, 4095 };
    const std::vector<uint8_t> tile = oneLineTile(xy, 3);

    SceneReader r;
    ASSERT_TRUE(r.open(tile.data(), static_cast<uint32_t>(tile.size())));
    ASSERT_EQ(r.layerCount(), 1);
    EXPECT_EQ(r.layer(0).klass, static_cast<uint8_t>(Slot::RoadMajor));
    EXPECT_EQ(r.layer(0).featureCount, 1);

    Pt got[8];
    int seen = 0;
    const int features = r.forEachFeature(0, got, 8, 0, 0, 240,
                                          [&](const Pt*, int n) { seen = n; });
    EXPECT_EQ(features, 1);
    ASSERT_EQ(seen, 3);
    // screen = origin + (local * tilePx >> 12)
    EXPECT_EQ(got[0].x, 0);
    EXPECT_EQ(got[1].x, (2048 * 240) >> kExtentShift);
    EXPECT_EQ(got[2].y, (4095 * 240) >> kExtentShift);
}

TEST(VecScene, OriginOffsetsEveryPoint)
{
    const int32_t xy[4] = { 0, 0, 4096, 4096 };
    const std::vector<uint8_t> tile = oneLineTile(xy, 2);
    SceneReader r;
    ASSERT_TRUE(r.open(tile.data(), static_cast<uint32_t>(tile.size())));
    Pt got[4];
    r.forEachFeature(0, got, 4, -50, 25, 240, [](const Pt*, int) {});
    EXPECT_EQ(got[0].x, -50);
    EXPECT_EQ(got[0].y, 25);
    EXPECT_EQ(got[1].x, -50 + 240);
}

TEST(VecScene, NegativeDeltasSurviveZigZag)
{
    // Clipped features routinely walk backwards, and a zigzag bug shows up as
    // geometry that is subtly mirrored rather than as a failure.
    const int32_t xy[6] = { 4000, 4000, 100, 3000, 2000, 10 };
    const std::vector<uint8_t> tile = oneLineTile(xy, 3);
    SceneReader r;
    ASSERT_TRUE(r.open(tile.data(), static_cast<uint32_t>(tile.size())));
    Pt got[4];
    r.forEachFeature(0, got, 4, 0, 0, 4096, [](const Pt*, int) {});
    EXPECT_EQ(got[0].x, 4000);
    EXPECT_EQ(got[1].x, 100);
    EXPECT_EQ(got[2].y, 10);
}

TEST(VecScene, BadMagicVersionOrExtentIsRefused)
{
    const int32_t xy[4] = { 0, 0, 100, 100 };
    std::vector<uint8_t> tile = oneLineTile(xy, 2);
    SceneReader r;
    ASSERT_TRUE(r.open(tile.data(), static_cast<uint32_t>(tile.size())));

    std::vector<uint8_t> bad = tile;
    bad[0] = 'X';
    EXPECT_FALSE(r.open(bad.data(), static_cast<uint32_t>(bad.size())));

    bad = tile;
    bad[1] = 99;
    EXPECT_FALSE(r.open(bad.data(), static_cast<uint32_t>(bad.size())));

    bad = tile;
    bad[2] = 0x00;  // extent 256, which the shift-based transform is wrong for
    bad[3] = 0x01;
    EXPECT_FALSE(r.open(bad.data(), static_cast<uint32_t>(bad.size())));
}

TEST(VecScene, ATruncatedTileIsRefusedRatherThanDecoded)
{
    const int32_t xy[4] = { 0, 0, 100, 100 };
    const std::vector<uint8_t> tile = oneLineTile(xy, 2);
    SceneReader r;
    for (uint32_t n = 0; n < tile.size(); n += 7) {
        EXPECT_FALSE(r.open(tile.data(), n)) << "accepted a " << n << "-byte tile";
    }
    EXPECT_TRUE(r.open(tile.data(), static_cast<uint32_t>(tile.size())));
}

TEST(VecScene, ALayerPointingOutsideTheTileIsRefused)
{
    const int32_t xy[4] = { 0, 0, 100, 100 };
    std::vector<uint8_t> tile = oneLineTile(xy, 2);
    // Directory entry 0's length field, made larger than the file.
    uint8_t* len = tile.data() + kHeaderBytes + 4;
    len[0] = 0xFF;
    len[1] = 0xFF;
    SceneReader r;
    EXPECT_FALSE(r.open(tile.data(), static_cast<uint32_t>(tile.size())));
}

TEST(VecScene, AnUnknownClassIsSkippedNotRejected)
{
    // The forward-compatibility hinge: an old watch must draw a newer pack
    // minus what it does not understand, rather than refusing the pack.
    const int32_t xy[4] = { 0, 0, 100, 100 };
    std::vector<uint8_t> tile = oneLineTile(xy, 2);
    tile[kHeaderBytes] = 200;   // a class from a future schema
    SceneReader r;
    ASSERT_TRUE(r.open(tile.data(), static_cast<uint32_t>(tile.size())));
    EXPECT_EQ(r.layerCount(), 1);
    EXPECT_EQ(r.layer(0).featureCount, 0) << "unknown class must draw nothing";
}

TEST(VecScene, TruncatedPayloadIsReportedByTheDecoderNotIgnored)
{
    // Header and directory intact, feature bytes cut short: open() cannot see
    // this, so forEachFeature has to.
    const int32_t xy[8] = { 0, 0, 500, 500, 900, 100, 1500, 1500 };
    std::vector<uint8_t> tile = oneLineTile(xy, 4);
    // Shorten the layer's length to cut the last coordinates off.
    uint8_t* len = tile.data() + kHeaderBytes + 4;
    const uint16_t shorter = static_cast<uint16_t>((len[0] | (len[1] << 8)) - 3);
    len[0] = static_cast<uint8_t>(shorter & 0xFF);
    len[1] = static_cast<uint8_t>(shorter >> 8);

    SceneReader r;
    ASSERT_TRUE(r.open(tile.data(), static_cast<uint32_t>(tile.size())));
    Pt got[8];
    EXPECT_EQ(r.forEachFeature(0, got, 8, 0, 0, 240, [](const Pt*, int) {}), -1);
}

TEST(VecScene, TheWriterRefusesAFeatureBiggerThanTheCap)
{
    std::vector<uint8_t> buf(64 * 1024, 0);
    std::vector<int32_t> huge(2 * (kMaxPointsPerFeature + 1), 0);
    SceneWriter w(buf.data(), static_cast<uint32_t>(buf.size()));
    ASSERT_TRUE(w.beginLayer(Slot::Path, Kind::Polyline));
    EXPECT_FALSE(w.addFeature(huge.data(), kMaxPointsPerFeature + 1));
    EXPECT_FALSE(w.ok());
}

TEST(VecScene, TheWriterRefusesToOverrunItsBuffer)
{
    // Never a partial tile: a truncated scene would be a faster benchmark and
    // a wrong one.
    uint8_t small[300];
    SceneWriter w(small, sizeof(small));
    ASSERT_TRUE(w.beginLayer(Slot::RoadMinor, Kind::Polyline));
    int32_t xy[2 * 64];
    for (int i = 0; i < 64; ++i) {
        xy[2 * i]     = i * 60;
        xy[2 * i + 1] = i * 61;
    }
    bool refused = false;
    for (int i = 0; i < 20 && !refused; ++i) {
        refused = !w.addFeature(xy, 64);
    }
    EXPECT_TRUE(refused);
    EXPECT_FALSE(w.ok());
}

TEST(VecScene, GenerationIsDeterministic)
{
    // Two runs of a bench must measure the same geometry, or a regression is
    // indistinguishable from a different scene.
    std::vector<uint8_t> a(48 * 1024, 0);
    std::vector<uint8_t> b(48 * 1024, 0);
    const uint32_t na = generateScene(a.data(), static_cast<uint32_t>(a.size()),
                                      SceneParams::cityCentre());
    const uint32_t nb = generateScene(b.data(), static_cast<uint32_t>(b.size()),
                                      SceneParams::cityCentre());
    ASSERT_GT(na, 0u);
    EXPECT_EQ(na, nb);
    EXPECT_EQ(std::memcmp(a.data(), b.data(), na), 0);
}

TEST(VecScene, EveryPresetFitsTheBufferTheAppGivesIt)
{
    // The app hands the suite a 24 KiB scene buffer. A preset that outgrew it
    // would report "scene-too-large" on the watch instead of a measurement,
    // and this is where that gets caught instead.
    const SceneParams presets[] = { SceneParams::rural(), SceneParams::suburban(),
                                    SceneParams::cityCentre() };
    for (const SceneParams& p : presets) {
        std::vector<uint8_t> buf(24 * 1024, 0);
        const uint32_t n = generateScene(buf.data(), static_cast<uint32_t>(buf.size()), p);
        EXPECT_GT(n, 0u);
        EXPECT_LE(n, 24u * 1024u);

        SceneReader r;
        ASSERT_TRUE(r.open(buf.data(), n));
        uint32_t features = 0;
        for (int i = 0; i < r.layerCount(); ++i) {
            features += r.layer(i).featureCount;
        }
        EXPECT_EQ(features, p.featureTotal());
    }
}

TEST(VecScene, AnEmptyBufferIsRefusedRatherThanHalfWritten)
{
    uint8_t tiny[8];
    EXPECT_EQ(generateScene(tiny, sizeof(tiny), SceneParams::rural()), 0u);
}

} // namespace
