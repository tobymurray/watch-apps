/**
 * @file MapSession_test.cpp
 * @brief The state machine the map face reads: which of "no fix", "no pack",
 *        "verifying", "corrupt", "off coverage" and "live" is true, and what
 *        makes each one change.
 *
 * The brief these apps were built to put it plainly: "no pack" and "pack not
 * yet verified" must not look alike, and neither may look like a crash. This
 * file is where that is pinned.
 *
 * The packs here carry zero tiles, which is legal (see PackFixture.hpp) and is
 * why the "live" case cannot be reached from a fixture: a pack with no tiles
 * is honestly off-coverage everywhere. Tile serving belongs to the vendored
 * reader and is covered upstream; what is tested here is everything around it.
 */
#include <gtest/gtest.h>

#include <MapKit/MapSession.hpp>
#include <MapKit/PackTrustReader.hpp>
#include <MapKit/TileCache.hpp>

#include "KernelTestDoubles.hpp"
#include "PackFixture.hpp"

#include <string>

namespace {

using MapKit::MapSession;
using MapKit::MapStatus;
using MapKit::PackTrustReader;
using MapKit::TileCache;
using namespace MapKitTest;

// Inside the default PackSpec bbox.
constexpr float kLat = 44.60F;
constexpr float kLon = -76.00F;
// Well outside it.
constexpr float kFarLat = 51.50F;
constexpr float kFarLon = -0.12F;

std::string inMaps(const std::string& name)
{
    return std::string(MapKit::kMapsDir) + "/" + name;
}

struct SessionFixture : public ::testing::Test {
    SDK::TestSupport::KernelFixture fx;
    TileCache cache;

    void seedPack(const std::string& name, const PackSpec& spec = PackSpec{})
    {
        fx.fileSystem.seedFile(inMaps(name), buildPack(spec));
    }

    void seedMarker(const std::string& name, uint32_t magic, const PackSpec& spec = PackSpec{})
    {
        const std::string pack = buildPack(spec);
        fx.fileSystem.seedFile(inMaps(name) + ".trust",
                               buildMarker(magic, pack.size(), declaredCrcOf(pack)));
    }
};

TEST_F(SessionFixture, BeforeAnyFixTheQuestionHasNoAnswer)
{
    // Which pack covers you is unanswerable without a position, so the status
    // says so rather than guessing or blaming the map.
    seedPack("city.rawtiles");
    seedMarker("city.rawtiles", PackTrustReader::kMagicGood);
    MapSession s(fx.kernel, cache);
    EXPECT_EQ(s.status(), MapStatus::NoFix);

    s.onPosition(0.0F, 0.0F, /*fix=*/false, /*recording=*/false);
    EXPECT_EQ(s.status(), MapStatus::NoFix) << "a sample without a fix is not a fix";
    EXPECT_FALSE(s.renderable());
}

TEST_F(SessionFixture, NoPackCoveringThePositionIsSaidPlainly)
{
    seedPack("city.rawtiles");
    seedMarker("city.rawtiles", PackTrustReader::kMagicGood);
    MapSession s(fx.kernel, cache);
    s.onPosition(kFarLat, kFarLon, true, false);
    EXPECT_EQ(s.status(), MapStatus::NoPack);
    EXPECT_FALSE(s.renderable());
    EXPECT_EQ(s.packName(), nullptr);
}

TEST_F(SessionFixture, NoPacksDeployedAtAllIsAlsoJustNoPack)
{
    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);
    EXPECT_EQ(s.status(), MapStatus::NoPack);
}

TEST_F(SessionFixture, APackWithNoVerdictYetReportsVerifyingAndWithholdsTiles)
{
    // The single most important state to get right: expected, temporary, and
    // resolves on its own. Not an error, and definitely not a blank screen
    // with no explanation.
    seedPack("city.rawtiles");
    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);

    EXPECT_EQ(s.status(), MapStatus::Verifying);
    EXPECT_FALSE(s.renderable()) << "tiles must be withheld until the CRC verdict lands";
    EXPECT_STREQ(s.packName(), "city.rawtiles");
    EXPECT_EQ(s.packErrorText(), nullptr) << "verifying is not an error";
}

TEST_F(SessionFixture, VerifyingBecomesTrustedWithoutAnythingElseHappening)
{
    // Map Manager finishes its background pass while the app is running. The
    // next GPS sample has to notice, or "verifying" would be a dead end.
    seedPack("city.rawtiles");
    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);
    ASSERT_EQ(s.status(), MapStatus::Verifying);

    seedMarker("city.rawtiles", PackTrustReader::kMagicGood);
    s.onPosition(kLat, kLon, true, false);

    EXPECT_TRUE(s.renderable());
    // Zero-tile fixture, so the honest answer at the crosshair is off-coverage.
    EXPECT_EQ(s.status(), MapStatus::OffCoverage);
}

TEST_F(SessionFixture, APackAlreadyKnownCorruptIsNeverSelected)
{
    // Known-corrupt is screened out at selection, so with only one pack
    // deployed there is nothing left to choose. That is the right answer:
    // "no map for here" is true, and it is more useful than naming a pack the
    // app is never going to draw.
    seedPack("broken.rawtiles");
    seedMarker("broken.rawtiles", PackTrustReader::kMagicBad);
    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);

    EXPECT_EQ(s.status(), MapStatus::NoPack);
    EXPECT_FALSE(s.renderable());
}

TEST_F(SessionFixture, APackThatTurnsCorruptWhileWaitingIsReportedAsCorrupt)
{
    // The path that actually reaches MapStatus::Corrupt: the pack was picked
    // while its verdict was still Absent, and Map Manager's background pass
    // then failed it. A corrupt pack opens *fine* structurally -- the CRC is
    // the only thing wrong with it -- so describing its OpenResult would print
    // "ok" for a broken map. Corrupt is its own state for exactly that reason.
    seedPack("city.rawtiles");
    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);
    ASSERT_EQ(s.status(), MapStatus::Verifying);

    seedMarker("city.rawtiles", PackTrustReader::kMagicBad);
    s.onPosition(kLat, kLon, true, false);

    EXPECT_EQ(s.status(), MapStatus::Corrupt);
    EXPECT_FALSE(s.renderable());
    EXPECT_EQ(s.packErrorText(), nullptr)
        << "the open result is 'ok'; the corruption is reported as its own state";
}

TEST_F(SessionFixture, ACorruptVerdictIsFinalAndStopsThePolling)
{
    // Sticky both ways: a file confirmed corrupt does not become whole by
    // being read again, so the session must not flip back to Verifying.
    seedPack("city.rawtiles");
    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);
    seedMarker("city.rawtiles", PackTrustReader::kMagicBad);
    s.onPosition(kLat, kLon, true, false);
    ASSERT_EQ(s.status(), MapStatus::Corrupt);

    fx.fileSystem.files.erase(inMaps("city.rawtiles") + ".trust");
    s.onPosition(kLat, kLon, true, false);
    EXPECT_EQ(s.status(), MapStatus::Corrupt);
}

TEST_F(SessionFixture, ACorruptPackDoesNotBlockAWorkingOne)
{
    seedPack("broken.rawtiles");
    seedMarker("broken.rawtiles", PackTrustReader::kMagicBad);
    seedPack("fine.rawtiles");
    seedMarker("fine.rawtiles", PackTrustReader::kMagicGood);

    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);
    EXPECT_STREQ(s.packName(), "fine.rawtiles");
    EXPECT_TRUE(s.renderable());
}

TEST_F(SessionFixture, TheDeeperPackIsChosenWhenSeveralCover)
{
    PackSpec shallow; shallow.zoomMax = 13;
    PackSpec deep;    deep.zoomMax    = 17;
    seedPack("region.rawtiles", shallow);
    seedPack("city.rawtiles", deep);

    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);
    EXPECT_STREQ(s.packName(), "city.rawtiles");
}

TEST_F(SessionFixture, ZoomStartsAtThePacksFinestLevel)
{
    PackSpec spec; spec.zoomMin = 12; spec.zoomMax = 16;
    seedPack("city.rawtiles", spec);
    seedMarker("city.rawtiles", PackTrustReader::kMagicGood, spec);

    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);
    EXPECT_EQ(s.zoom(), 16) << "the wearer is looking at where they are";
}

TEST_F(SessionFixture, ZoomCyclesThroughThePacksOwnRangeAndWraps)
{
    PackSpec spec; spec.zoomMin = 12; spec.zoomMax = 14;
    seedPack("city.rawtiles", spec);
    seedMarker("city.rawtiles", PackTrustReader::kMagicGood, spec);

    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);
    ASSERT_EQ(s.zoom(), 14);
    s.cycleZoom();  EXPECT_EQ(s.zoom(), 12) << "wraps to the pack's coarsest";
    s.cycleZoom();  EXPECT_EQ(s.zoom(), 13);
    s.cycleZoom();  EXPECT_EQ(s.zoom(), 14);
}

TEST_F(SessionFixture, ZoomDoesNothingWithNoPack)
{
    MapSession s(fx.kernel, cache);
    s.onPosition(kFarLat, kFarLon, true, false);
    const uint8_t before = s.zoom();
    s.cycleZoom();
    EXPECT_EQ(s.zoom(), before);
}

TEST_F(SessionFixture, TheTraceGrowsOnlyWhileRecording)
{
    // A paused activity must not draw a straight line across the gap where
    // the wearer stood still, or across a drive home.
    seedPack("city.rawtiles");
    MapSession s(fx.kernel, cache);

    s.onPosition(kLat, kLon, true, /*recording=*/false);
    EXPECT_EQ(s.trace().count(), 0u);

    s.onPosition(kLat, kLon, true, /*recording=*/true);
    EXPECT_EQ(s.trace().count(), 1u);

    s.onPosition(kLat + 0.01F, kLon + 0.01F, true, /*recording=*/false);
    EXPECT_EQ(s.trace().count(), 1u);
}

TEST_F(SessionFixture, TheTraceIgnoresSamplesWithNoFix)
{
    seedPack("city.rawtiles");
    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, true);
    s.onPosition(0.0F, 0.0F, /*fix=*/false, true);
    EXPECT_EQ(s.trace().count(), 1u) << "a no-fix sample must not append (0, 0)";
}

TEST_F(SessionFixture, ResetTraceClearsTheBreadcrumbButKeepsThePack)
{
    seedPack("city.rawtiles");
    seedMarker("city.rawtiles", PackTrustReader::kMagicGood);
    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, true);
    ASSERT_EQ(s.trace().count(), 1u);
    ASSERT_TRUE(s.renderable());

    s.resetTrace();
    EXPECT_EQ(s.trace().count(), 0u);
    EXPECT_TRUE(s.renderable()) << "a new activity does not re-verify the map";
}

TEST_F(SessionFixture, WalkingOffOnePackOntoAnotherReselects)
{
    PackSpec here;  // default bbox, eastern Ontario
    PackSpec there;
    there.minLon = -1000000; there.minLat =  51000000;
    there.maxLon =  1000000; there.maxLat =  52000000;   // around London

    seedPack("ontario.rawtiles", here);
    seedPack("london.rawtiles", there);
    seedMarker("ontario.rawtiles", PackTrustReader::kMagicGood, here);
    seedMarker("london.rawtiles", PackTrustReader::kMagicGood, there);

    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);
    ASSERT_STREQ(s.packName(), "ontario.rawtiles");

    s.onPosition(kFarLat, kFarLon, true, false);
    EXPECT_STREQ(s.packName(), "london.rawtiles");
    EXPECT_TRUE(s.renderable());
}

TEST_F(SessionFixture, TheDirectoryIsScannedExactlyOncePerLaunch)
{
    // Regression, found by running the simulator with the maps directory
    // emptied. Nothing keyed on "no pack selected" may rescan, because that
    // condition persists for as long as the wearer stays outside coverage --
    // rescanning there would mean a directory walk plus a header, footer and
    // marker read per pack, once a second, for a whole activity, on the GUI
    // thread, to re-learn what it already knew.
    //
    // Asserted by its visible consequence: a pack that appears *after* the
    // scan is not picked up. That is the accepted cost, and it costs nothing
    // real -- a pack can only arrive over USB, and connecting USB terminates
    // every running app, so no pack has ever appeared under a running one.
    // Valid, and covering neither of this file's two reference positions, so
    // the scan finds something without that something ever being selectable.
    PackSpec elsewhere;
    elsewhere.minLon = 130000000; elsewhere.minLat = -40000000;
    elsewhere.maxLon = 140000000; elsewhere.maxLat = -30000000;
    seedPack("elsewhere.rawtiles", elsewhere);
    MapSession s(fx.kernel, cache);

    s.onPosition(kFarLat, kFarLon, true, false);
    ASSERT_EQ(s.status(), MapStatus::NoPack);

    seedPack("city.rawtiles");          // would cover kLat/kLon, but arrives late
    for (int i = 0; i < 30; ++i) {
        s.onPosition(kLat, kLon, true, false);
    }
    EXPECT_EQ(s.status(), MapStatus::NoPack)
        << "a rescan on every sample would have found the late pack";
    EXPECT_EQ(s.packName(), nullptr);
}

TEST_F(SessionFixture, WalkingIntoCoverageStillFindsThePack)
{
    // The other half of scanning once: re-selection has to keep running, or a
    // wearer who started outside every pack would never get a map.
    seedPack("city.rawtiles");
    seedMarker("city.rawtiles", PackTrustReader::kMagicGood);
    MapSession s(fx.kernel, cache);

    s.onPosition(kFarLat, kFarLon, true, false);
    ASSERT_EQ(s.status(), MapStatus::NoPack);

    s.onPosition(kLat, kLon, true, false);
    EXPECT_STREQ(s.packName(), "city.rawtiles");
    EXPECT_TRUE(s.renderable());
}

TEST_F(SessionFixture, APackThatTurnsCorruptFallsBackToAnotherThatCovers)
{
    // A verdict landing mid-activity should not cost the wearer a map they
    // could otherwise have had.
    PackSpec deep;    deep.zoomMax = 17;
    PackSpec shallow; shallow.zoomMax = 13;
    seedPack("detailed.rawtiles", deep);
    seedPack("coarse.rawtiles", shallow);
    seedMarker("coarse.rawtiles", PackTrustReader::kMagicGood, shallow);

    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);
    ASSERT_STREQ(s.packName(), "detailed.rawtiles");
    ASSERT_EQ(s.status(), MapStatus::Verifying);

    seedMarker("detailed.rawtiles", PackTrustReader::kMagicBad, deep);
    s.onPosition(kLat, kLon, true, false);

    EXPECT_STREQ(s.packName(), "coarse.rawtiles");
    EXPECT_TRUE(s.renderable());
}

TEST_F(SessionFixture, WithNothingToFallBackToACorruptPackKeepsItsOwnDiagnosis)
{
    // "map pack corrupt" tells whoever has to fix it more than "no map for
    // here", so with no alternative the session stays put rather than
    // reselecting into nothing.
    seedPack("city.rawtiles");
    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);
    ASSERT_EQ(s.status(), MapStatus::Verifying);

    seedMarker("city.rawtiles", PackTrustReader::kMagicBad);
    s.onPosition(kLat, kLon, true, false);

    EXPECT_EQ(s.status(), MapStatus::Corrupt);
    EXPECT_STREQ(s.packName(), "city.rawtiles");
}

TEST_F(SessionFixture, StayingInsideThePackDoesNotReopenIt)
{
    // Re-selection walks the directory and re-peeks every header. Doing that
    // on every GPS sample would put it on the GUI thread once a second.
    seedPack("city.rawtiles");
    seedMarker("city.rawtiles", PackTrustReader::kMagicGood);
    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);
    ASSERT_TRUE(s.renderable());

    // Remove the pack from under the session. If it re-opened, this would fail.
    fx.fileSystem.files.erase(inMaps("city.rawtiles"));
    s.onPosition(kLat + 0.001F, kLon + 0.001F, true, false);
    EXPECT_TRUE(s.renderable());
    EXPECT_STREQ(s.packName(), "city.rawtiles");
}

TEST_F(SessionFixture, AStructurallyBrokenPackIsAnErrorWithAReason)
{
    // Passes the catalog's cheap header screen (magic, format, tile dim, bbox
    // are all fine) but fails the structural open, which checks the tile index
    // the peek never looks at.
    std::string pack = buildPack();
    pack[92] = static_cast<char>(0xFF);   // index_offset must be exactly 292
    fx.fileSystem.seedFile(inMaps("mangled.rawtiles"), pack);

    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);

    EXPECT_EQ(s.status(), MapStatus::PackError);
    EXPECT_FALSE(s.renderable());
    ASSERT_NE(s.packErrorText(), nullptr);
    EXPECT_STRNE(s.packErrorText(), "ok") << "an error must not describe itself as ok";
    EXPECT_STREQ(s.packName(), "mangled.rawtiles") << "say which pack failed";
}

TEST_F(SessionFixture, OpensPacksWithoutRunningTheWholeFileCrcScan)
{
    // The load-bearing performance property, stated as behaviour: a pack whose
    // body no longer matches its declared CRC still opens structurally. If the
    // GUI-thread open ever went back to verifying, this would fail -- and on
    // hardware a 201 MB pack would trip the app-liveness watchdog and restart
    // the watch.
    std::string pack = buildPack();
    pack[kHeaderSize] = static_cast<char>(pack[kHeaderSize] ^ 0xFF);   // break the footer
    fx.fileSystem.seedFile(inMaps("city.rawtiles"), pack);

    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);
    EXPECT_EQ(s.status(), MapStatus::Verifying)
        << "structural open must succeed; trust is Map Manager's to grant";
}

TEST_F(SessionFixture, AMarkerForDifferentBytesLeavesTheSessionWaiting)
{
    // The (size, crc) guard, seen from the outside: a stale Good marker must
    // not make a replaced pack renderable.
    seedPack("city.rawtiles");
    const std::string pack = buildPack();
    fx.fileSystem.seedFile(inMaps("city.rawtiles") + ".trust",
                           buildMarker(PackTrustReader::kMagicGood,
                                       pack.size() + 1, declaredCrcOf(pack)));

    MapSession s(fx.kernel, cache);
    s.onPosition(kLat, kLon, true, false);
    EXPECT_EQ(s.status(), MapStatus::Verifying);
    EXPECT_FALSE(s.renderable());
}

} // namespace
