/**
 * @file PackTrustReader_test.cpp
 * @brief The (size, crc) guard, which is the part of Map Manager's marker
 *        contract a consumer can silently omit and still appear to work.
 *
 * Every case here that ends in Absent is a case where a reader that skipped
 * the guard would have said Good or Bad instead -- which is exactly the
 * failure the contract exists to prevent: a verdict about bytes that are no
 * longer there.
 */
#include <gtest/gtest.h>

#include <MapKit/PackTrustReader.hpp>

#include "KernelTestDoubles.hpp"
#include "PackFixture.hpp"

namespace {

using MapKit::PackTrustReader;
using Trust = MapKit::PackTrustReader::Trust;
using namespace MapKitTest;

constexpr uint32_t kGood = PackTrustReader::kMagicGood;
constexpr uint32_t kBad  = PackTrustReader::kMagicBad;

const char* const kMarkerPath = "../SharedData/maps/city.rawtiles.trust";

struct TrustFixture : public ::testing::Test {
    SDK::TestSupport::KernelFixture fx;

    // The file the verdict is supposed to be about.
    uint64_t packSize = 0;
    uint32_t packCrc  = 0;

    void SetUp() override
    {
        const std::string pack = buildPack();
        packSize = pack.size();
        packCrc  = declaredCrcOf(pack);
    }

    Trust verdictWith(const std::string& markerBytes)
    {
        fx.fileSystem.seedFile(kMarkerPath, markerBytes);
        return PackTrustReader(fx.kernel, kMarkerPath).verdictFor(packSize, packCrc);
    }
};

TEST(PackFixture, Crc32MatchesSpecVector)
{
    // rawtiles spec § 10 pins the check value for ASCII "123456789".
    const uint8_t input[] = "123456789";
    EXPECT_EQ(crc32(input, 9), 0xCBF43926u);
}

TEST_F(TrustFixture, NoMarkerAtAllIsAbsentNotAnError)
{
    // The normal state for the first minutes after a pack is deployed. It
    // means "keep waiting", not "no pack" and not "bad pack".
    EXPECT_EQ(PackTrustReader(fx.kernel, kMarkerPath).verdictFor(packSize, packCrc),
              Trust::Absent);
}

TEST_F(TrustFixture, GoodMarkerForTheseExactBytesIsGood)
{
    EXPECT_EQ(verdictWith(buildMarker(kGood, packSize, packCrc)), Trust::Good);
}

TEST_F(TrustFixture, BadMarkerForTheseExactBytesIsBad)
{
    // Bad has to be distinguishable from Absent, or the screen would say
    // "verifying" forever about a file that will never pass.
    EXPECT_EQ(verdictWith(buildMarker(kBad, packSize, packCrc)), Trust::Bad);
}

TEST_F(TrustFixture, GoodMarkerWhoseSizeNoLongerMatchesIsAbsent)
{
    // The pack was replaced since it was scanned. Same path, different file;
    // the old verdict says nothing about it.
    EXPECT_EQ(verdictWith(buildMarker(kGood, packSize + 1, packCrc)), Trust::Absent);
}

TEST_F(TrustFixture, GoodMarkerWhoseCrcNoLongerMatchesIsAbsent)
{
    EXPECT_EQ(verdictWith(buildMarker(kGood, packSize, packCrc ^ 0xFFFFFFFFu)), Trust::Absent);
}

TEST_F(TrustFixture, StaleBadMarkerIsAlsoAbsent)
{
    // The guard cuts both ways. A pack that failed, was re-copied correctly,
    // and now has a fresh CRC must not stay condemned by the old verdict.
    EXPECT_EQ(verdictWith(buildMarker(kBad, packSize, packCrc + 1)), Trust::Absent);
}

TEST_F(TrustFixture, UnknownMagicIsAbsent)
{
    EXPECT_EQ(verdictWith(buildMarker(0x21545050u, packSize, packCrc)), Trust::Absent);
}

TEST_F(TrustFixture, WrongLengthIsAbsent)
{
    // A torn write from a concurrent writer, or a truncated file. Absent is
    // always the safe fallback: never a false trust, and self-correcting the
    // moment Map Manager's next pass overwrites it.
    std::string shortMarker = buildMarker(kGood, packSize, packCrc);
    shortMarker.resize(15);
    EXPECT_EQ(verdictWith(shortMarker), Trust::Absent);

    std::string longMarker = buildMarker(kGood, packSize, packCrc);
    longMarker.push_back('\0');
    EXPECT_EQ(verdictWith(longMarker), Trust::Absent);
}

TEST_F(TrustFixture, EmptyMarkerIsAbsent)
{
    EXPECT_EQ(verdictWith(std::string{}), Trust::Absent);
}

TEST_F(TrustFixture, RawReadStillReportsAVerdictTheGuardWouldReject)
{
    // read() is the unguarded primitive; verdictFor() is what callers should
    // use. Pinned so the difference between them stays deliberate.
    fx.fileSystem.seedFile(kMarkerPath, buildMarker(kGood, packSize + 999, packCrc));
    uint64_t size = 0;
    uint32_t crc  = 0;
    EXPECT_EQ(PackTrustReader(fx.kernel, kMarkerPath).read(size, crc), Trust::Good);
    EXPECT_EQ(size, packSize + 999);
    EXPECT_EQ(PackTrustReader(fx.kernel, kMarkerPath).verdictFor(packSize, packCrc),
              Trust::Absent);
}

TEST(PackTrustReaderPaths, MarkerPathIsTheFullNamePlusSuffix)
{
    char out[64] = {};
    ASSERT_TRUE(PackTrustReader::markerPathFor("maps/city.rawtiles", out, sizeof(out)));
    EXPECT_STREQ(out, "maps/city.rawtiles.trust");
}

TEST(PackTrustReaderPaths, RefusesToTruncateRatherThanNameTheWrongFile)
{
    char out[8] = {};
    EXPECT_FALSE(PackTrustReader::markerPathFor("maps/city.rawtiles", out, sizeof(out)));
}

} // namespace
