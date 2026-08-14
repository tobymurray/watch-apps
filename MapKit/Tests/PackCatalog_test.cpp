/**
 * @file PackCatalog_test.cpp
 * @brief Discovery and the header screen: what reaches the selection rule,
 *        and what is filtered out before it gets there.
 */
#include <gtest/gtest.h>

#include <MapKit/PackCatalog.hpp>
#include <MapKit/PackSelection.hpp>
#include <MapKit/PackTrustReader.hpp>

#include "KernelTestDoubles.hpp"
#include "PackFixture.hpp"

#include <string>

namespace {

using MapKit::PackCatalog;
using MapKit::PackTrustReader;
using namespace MapKitTest;

std::string inMaps(const std::string& name)
{
    return std::string(MapKit::kMapsDir) + "/" + name;
}

struct CatalogFixture : public ::testing::Test {
    SDK::TestSupport::KernelFixture fx;

    void seedPack(const std::string& name, const PackSpec& spec = PackSpec{})
    {
        fx.fileSystem.seedFile(inMaps(name), buildPack(spec));
    }

    void seedMarker(const std::string& packName, uint32_t magic, const PackSpec& spec = PackSpec{})
    {
        const std::string pack = buildPack(spec);
        fx.fileSystem.seedFile(inMaps(packName) + ".trust",
                               buildMarker(magic, pack.size(), declaredCrcOf(pack)));
    }

    /// Index of the named pack in the catalog, or npos.
    static size_t indexOf(const PackCatalog& c, const char* name)
    {
        for (size_t i = 0; i < c.count(); ++i) {
            if (std::string(c.at(i).name) == name) {
                return i;
            }
        }
        return static_cast<size_t>(-1);
    }
};

TEST_F(CatalogFixture, NoDirectoryIsNotAnError)
{
    // SharedData/maps does not exist until somebody deploys a pack there.
    PackCatalog c(fx.kernel);
    EXPECT_EQ(c.rescan(), 0u);
    EXPECT_TRUE(c.scanned()) << "scanned() must distinguish 'looked, found nothing' from 'not looked'";
}

TEST_F(CatalogFixture, ScannedIsFalseBeforeTheFirstRescan)
{
    PackCatalog c(fx.kernel);
    EXPECT_FALSE(c.scanned());
}

TEST_F(CatalogFixture, FindsPacksAndNamesThemWithoutTheirDirectory)
{
    seedPack("city.rawtiles");
    seedPack("region.rawtiles");
    PackCatalog c(fx.kernel);
    ASSERT_EQ(c.rescan(), 2u);
    EXPECT_NE(indexOf(c, "city.rawtiles"), static_cast<size_t>(-1));
    EXPECT_NE(indexOf(c, "region.rawtiles"), static_cast<size_t>(-1));
}

TEST_F(CatalogFixture, IgnoresTrustMarkersAndUnrelatedFiles)
{
    // Map Manager writes its markers into the same directory, so this is the
    // normal case, not a hypothetical one.
    seedPack("city.rawtiles");
    seedMarker("city.rawtiles", PackTrustReader::kMagicGood);
    fx.fileSystem.seedFile(inMaps("notes.txt"), "hello");
    fx.fileSystem.seedFile(inMaps("rawtiles"), "not one either");

    PackCatalog c(fx.kernel);
    ASSERT_EQ(c.rescan(), 1u);
    EXPECT_STREQ(c.at(0).name, "city.rawtiles");
}

TEST_F(CatalogFixture, ReadsBboxAndZoomMaxFromTheHeader)
{
    PackSpec spec;
    spec.zoomMax = 15;
    spec.minLon = -76100000; spec.minLat = 44500000;
    spec.maxLon = -75900000; spec.maxLat = 44700000;
    seedPack("city.rawtiles", spec);

    PackCatalog c(fx.kernel);
    ASSERT_EQ(c.rescan(), 1u);
    EXPECT_EQ(c.at(0).zoomMax, 15);
    EXPECT_EQ(c.at(0).bboxMinLonUDeg, -76100000);
    EXPECT_EQ(c.at(0).bboxMaxLatUDeg, 44700000);
    EXPECT_TRUE(c.at(0).contains(44600000, -76000000));
}

TEST_F(CatalogFixture, RejectsBadMagic)
{
    PackSpec spec;
    spec.goodMagic = false;
    seedPack("impostor.rawtiles", spec);
    PackCatalog c(fx.kernel);
    EXPECT_EQ(c.rescan(), 0u);
}

TEST_F(CatalogFixture, RejectsAFormatMajorThisReaderDoesNotKnow)
{
    PackSpec spec;
    spec.formatMajor = 2;
    seedPack("future.rawtiles", spec);
    PackCatalog c(fx.kernel);
    EXPECT_EQ(c.rescan(), 0u);
}

TEST_F(CatalogFixture, RejectsATileDimensionTheMosaicArithmeticCannotHandle)
{
    // The mosaic addresses sub-tile offsets by shifting MapMath::TILE_SHIFT
    // bits, which is only the same thing as a divide when tile_dim is 256.
    PackSpec spec;
    spec.tileDim = 64;
    seedPack("tiny-tiles.rawtiles", spec);
    PackCatalog c(fx.kernel);
    EXPECT_EQ(c.rescan(), 0u);
}

TEST_F(CatalogFixture, RejectsAPixelFormatBlitCopyIsNotBeingHanded)
{
    PackSpec spec;
    spec.pixelFormat = 2;   // RGB565
    seedPack("rgb565.rawtiles", spec);
    PackCatalog c(fx.kernel);
    EXPECT_EQ(c.rescan(), 0u);
}

TEST_F(CatalogFixture, RejectsAProjectionMapMathDoesNotCompute)
{
    PackSpec spec;
    spec.projection = 3;    // LocalLinear
    spec.addressing = 2;    // SingleImage, the legal pairing for it
    seedPack("local.rawtiles", spec);
    PackCatalog c(fx.kernel);
    EXPECT_EQ(c.rescan(), 0u);
}

TEST_F(CatalogFixture, RejectsAnInvertedBoundingBox)
{
    PackSpec spec;
    spec.minLon = 1000; spec.maxLon = -1000;
    seedPack("inverted.rawtiles", spec);
    PackCatalog c(fx.kernel);
    EXPECT_EQ(c.rescan(), 0u);
}

TEST_F(CatalogFixture, RejectsAFileTooShortToBeAPack)
{
    fx.fileSystem.seedFile(inMaps("stub.rawtiles"), std::string(100, '\0'));
    PackCatalog c(fx.kernel);
    EXPECT_EQ(c.rescan(), 0u);
}

TEST_F(CatalogFixture, MarksAPackKnownCorruptWhenMapManagerSaysSo)
{
    seedPack("broken.rawtiles");
    seedMarker("broken.rawtiles", PackTrustReader::kMagicBad);

    PackCatalog c(fx.kernel);
    ASSERT_EQ(c.rescan(), 1u);
    EXPECT_TRUE(c.at(0).knownCorrupt);
    // ...and the selection rule then refuses to choose it.
    EXPECT_EQ(MapKit::selectPack(c.facts(), c.count(), 44600000, -76000000),
              MapKit::kNoPack);
}

TEST_F(CatalogFixture, AGoodMarkerDoesNotMarkAPackCorrupt)
{
    seedPack("fine.rawtiles");
    seedMarker("fine.rawtiles", PackTrustReader::kMagicGood);
    PackCatalog c(fx.kernel);
    ASSERT_EQ(c.rescan(), 1u);
    EXPECT_FALSE(c.at(0).knownCorrupt);
}

TEST_F(CatalogFixture, AStaleBadMarkerDoesNotCondemnAReplacedPack)
{
    // The pack on disk has this spec; the marker describes a different one.
    // Without the (size, crc) guard this would read as corrupt forever.
    seedPack("replaced.rawtiles");
    const std::string other = buildPack();
    fx.fileSystem.seedFile(inMaps("replaced.rawtiles") + ".trust",
                           buildMarker(PackTrustReader::kMagicBad,
                                       other.size() + 4096, declaredCrcOf(other)));
    PackCatalog c(fx.kernel);
    ASSERT_EQ(c.rescan(), 1u);
    EXPECT_FALSE(c.at(0).knownCorrupt);
}

TEST_F(CatalogFixture, StopsAtTheHardCapRatherThanLettingADirectorySetMemoryUse)
{
    for (size_t i = 0; i < PackCatalog::kMaxPacks + 5; ++i) {
        seedPack("pack" + std::to_string(i) + ".rawtiles");
    }
    PackCatalog c(fx.kernel);
    EXPECT_EQ(c.rescan(), PackCatalog::kMaxPacks);
}

TEST_F(CatalogFixture, RescanReplacesTheRosterRatherThanAppendingToIt)
{
    seedPack("city.rawtiles");
    PackCatalog c(fx.kernel);
    ASSERT_EQ(c.rescan(), 1u);
    EXPECT_EQ(c.rescan(), 1u);
    EXPECT_EQ(c.rescan(), 1u);
}

TEST_F(CatalogFixture, NamesRemainValidAfterTheCallReturns)
{
    // PackFacts::name points into the catalog's own storage. Selection runs on
    // facts() after rescan() has returned, so this has to hold.
    seedPack("city.rawtiles");
    PackCatalog c(fx.kernel);
    ASSERT_EQ(c.rescan(), 1u);
    const MapKit::PackFacts* facts = c.facts();
    EXPECT_STREQ(facts[0].name, "city.rawtiles");
}

TEST_F(CatalogFixture, LeavesNoOpenFileHandlesBehind)
{
    // FatFs has a finite lock table; a scan that leaked a handle per pack
    // would eventually stop being able to open anything.
    seedPack("a.rawtiles");
    seedPack("b.rawtiles");
    seedMarker("a.rawtiles", PackTrustReader::kMagicGood);

    PackCatalog c(fx.kernel);
    ASSERT_EQ(c.rescan(), 2u);
    for (const auto& [path, open] : fx.fileSystem.openHandles) {
        EXPECT_EQ(open, 0u) << "still open: " << path;
    }
}

TEST_F(CatalogFixture, FullPathIsRelativeToTheSharedMapDirectory)
{
    // Sandbox-relative, never volume-prefixed: absolute paths do not resolve
    // from inside an app on hardware.
    char out[128] = {};
    ASSERT_TRUE(PackCatalog::fullPathFor("city.rawtiles", out, sizeof(out)));
    EXPECT_STREQ(out, "../SharedData/maps/city.rawtiles");
}

TEST_F(CatalogFixture, RefusesToTruncateALongPath)
{
    char out[16] = {};
    EXPECT_FALSE(PackCatalog::fullPathFor("city.rawtiles", out, sizeof(out)));
}

} // namespace
