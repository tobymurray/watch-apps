/**
 * @file TileRequestLog_test.cpp
 * @brief Quantising, de-duplication and the bounds -- the three things that
 *        decide whether this is a useful artifact or a way to wear out flash.
 */
#include <gtest/gtest.h>

#include <MapKit/MapMath.hpp>
#include <MapKit/TileRequestLog.hpp>

#include "KernelTestDoubles.hpp"

#include <string>

namespace {

using MapKit::TileRequestLog;

const char* const kPath = "../SharedData/maps/requested-tiles.txt";

struct RequestFixture : public ::testing::Test {
    SDK::TestSupport::KernelFixture fx;

    std::string contents() const { return fx.fileSystem.readFile(kPath); }

    size_t dataLines() const
    {
        const std::string s = contents();
        size_t n = 0, start = 0;
        while (start < s.size()) {
            size_t nl = s.find('\n', start);
            if (nl == std::string::npos) break;
            if (s[start] != '#') ++n;
            start = nl + 1;
        }
        return n;
    }

    /// A longitude offset guaranteed to land in the next tile at
    /// kRequestZoom, derived rather than guessed: one tile spans
    /// 360 degrees / 2^z of longitude.
    static int32_t oneTileOfLonUDeg()
    {
        const int64_t tilesAcross = 1LL << TileRequestLog::kRequestZoom;
        return static_cast<int32_t>(360000000LL / tilesAcross) + 1;
    }
};

TEST_F(RequestFixture, NothingIsWrittenUntilSomethingIsNoted)
{
    TileRequestLog log(fx.kernel, "RunMap");
    EXPECT_EQ(log.count(), 0u);
    EXPECT_TRUE(contents().empty());
}

TEST_F(RequestFixture, TheFirstGapWritesAHeaderAndOneLine)
{
    TileRequestLog log(fx.kernel, "RunMap");
    EXPECT_TRUE(log.note(44600000, -76000000));

    const std::string s = contents();
    EXPECT_NE(s.find("# mapkit-requested-tiles v1"), std::string::npos);
    EXPECT_EQ(dataLines(), 1u);
    EXPECT_NE(s.find("RunMap"), std::string::npos) << "the line names the app that wanted it";
    EXPECT_NE(s.find("12/"), std::string::npos) << "recorded at kRequestZoom";
}

TEST_F(RequestFixture, StayingInTheSameTileWritesNothingMore)
{
    // The property the whole design turns on. At 1 Hz, an activity standing
    // still outside coverage must not append once a second for hours.
    TileRequestLog log(fx.kernel, "RunMap");
    ASSERT_TRUE(log.note(44600000, -76000000));
    for (int i = 0; i < 500; ++i) {
        EXPECT_FALSE(log.note(44600000 + i, -76000000 + i));
    }
    EXPECT_EQ(dataLines(), 1u);
    EXPECT_EQ(log.count(), 1u);
}

TEST_F(RequestFixture, EnteringANewTileWritesAgain)
{
    TileRequestLog log(fx.kernel, "HikeMap");
    ASSERT_TRUE(log.note(44600000, -76000000));
    ASSERT_TRUE(log.note(44600000, -76000000 + oneTileOfLonUDeg()));
    EXPECT_EQ(dataLines(), 2u);
    EXPECT_EQ(log.count(), 2u);
}

TEST_F(RequestFixture, ReturningToAnEarlierTileDoesNotWriteAgain)
{
    // Out and back is the shape of most activities.
    TileRequestLog log(fx.kernel, "HikeMap");
    const int32_t step = oneTileOfLonUDeg();
    ASSERT_TRUE(log.note(44600000, -76000000));
    ASSERT_TRUE(log.note(44600000, -76000000 + step));
    EXPECT_FALSE(log.note(44600000, -76000000));
    EXPECT_EQ(dataLines(), 2u);
}

TEST_F(RequestFixture, TheHeaderIsWrittenOnlyOnce)
{
    TileRequestLog log(fx.kernel, "BikeMap");
    const int32_t step = oneTileOfLonUDeg();
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(log.note(44600000, -76000000 + step * i));
    }
    const std::string s = contents();
    EXPECT_EQ(s.find("# mapkit-requested-tiles"), s.rfind("# mapkit-requested-tiles"));
    EXPECT_EQ(dataLines(), 4u);
}

TEST_F(RequestFixture, AppendsToAFileAnEarlierSessionLeftBehind)
{
    // Two activities, two TileRequestLog objects, one file. The second must
    // not truncate the first's findings.
    const int32_t step = oneTileOfLonUDeg();
    {
        TileRequestLog first(fx.kernel, "RunMap");
        ASSERT_TRUE(first.note(44600000, -76000000));
    }
    {
        TileRequestLog second(fx.kernel, "BikeMap");
        ASSERT_TRUE(second.note(44600000, -76000000 + step));
    }
    EXPECT_EQ(dataLines(), 2u);
    const std::string s = contents();
    EXPECT_NE(s.find("RunMap"), std::string::npos);
    EXPECT_NE(s.find("BikeMap"), std::string::npos);
}

TEST_F(RequestFixture, StopsAtTheSessionCapRatherThanGrowingWithTheActivity)
{
    TileRequestLog log(fx.kernel, "RunMap");
    const int32_t step = oneTileOfLonUDeg();
    for (size_t i = 0; i < TileRequestLog::kMaxTilesPerSession + 20; ++i) {
        log.note(44600000, -76000000 + step * static_cast<int32_t>(i));
    }
    EXPECT_EQ(log.count(), TileRequestLog::kMaxTilesPerSession);
    EXPECT_EQ(dataLines(), TileRequestLog::kMaxTilesPerSession);
}

TEST_F(RequestFixture, RecordsTheTileCentreRatherThanTheFix)
{
    // What is written is the tile, not where the wearer was standing. Two
    // fixes a few hundred metres apart inside one tile produce the same line,
    // which is the point: it is a request for an area, and it discloses an
    // area.
    TileRequestLog a(fx.kernel, "RunMap");
    ASSERT_TRUE(a.note(44600000, -76000000));
    const std::string first = contents();

    fx.fileSystem.files.erase(kPath);
    TileRequestLog b(fx.kernel, "RunMap");
    ASSERT_TRUE(b.note(44603000, -76003000));   // same z12 tile, ~300 m away
    EXPECT_EQ(contents(), first);
}

TEST_F(RequestFixture, WritesNothingIntoAFileThatHasReachedItsCap)
{
    // Stops rather than rotating: an old request is exactly as valid as a new
    // one, so dropping the oldest to make room would discard the thing being
    // collected.
    fx.fileSystem.seedFile(kPath, std::string(TileRequestLog::kMaxBytes, 'x'));
    TileRequestLog log(fx.kernel, "RunMap");
    EXPECT_FALSE(log.note(44600000, -76000000));
    EXPECT_EQ(contents().size(), TileRequestLog::kMaxBytes);
}

TEST_F(RequestFixture, LeavesNoOpenFileHandlesBehind)
{
    TileRequestLog log(fx.kernel, "RunMap");
    const int32_t step = oneTileOfLonUDeg();
    for (int i = 0; i < 5; ++i) {
        log.note(44600000, -76000000 + step * i);
    }
    for (const auto& [path, open] : fx.fileSystem.openHandles) {
        EXPECT_EQ(open, 0u) << "still open: " << path;
    }
}

TEST(TileRequestPaths, DoesNotUseAnExtensionMapManagerWouldTryToVerify)
{
    // The file sits in the directory Map Manager scans. If it ended in
    // .rawtiles, Map Manager would adopt it as a pack and CRC-verify a text
    // file forever, and MapKit's own catalog would try to open it.
    const std::string path = kPath;
    EXPECT_EQ(path.rfind(".rawtiles"), std::string::npos);
    EXPECT_NE(path.rfind(".txt"), std::string::npos);
}

} // namespace
