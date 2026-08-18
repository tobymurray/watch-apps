/**
 * @file Container_test.cpp
 * @brief Getting the attribution back out of a pack.
 *
 * The vendored reader already *validated* `ATTR` — § 11 #38's UTF-8 and text
 * rules, and the duplicate-tag check — and then threw the location away, so
 * there was no way to display the credit the pack's licence obliges the watch
 * to show. These cover the accessor that closes that gap.
 *
 * The cases worth arguing with are the negative ones. A pack with no `ATTR`
 * and a buffer too small to hold one both return false rather than producing
 * a partial string, because a clipped or invented attribution looks like the
 * obligation was met when it was not.
 */
#include <gtest/gtest.h>

#include <SDK/RawTiles/Container.hpp>

#include "KernelTestDoubles.hpp"
#include "PackFixture.hpp"

namespace {

using SDK::RawTiles::Container;
using SDK::RawTiles::OpenResult;
using namespace MapKitTest;

/// The string a pack built from Protomaps' basemap actually carries — the
/// `MAP_COMPLIANCE_APPENDIX.md` § 4 wording for that pipeline, and what
/// slippypack's browser front-end writes.
const char* const kProtomapsAttr =
    "Map data from OpenStreetMap (ODbL) \xC2\xB7 basemap \xC2\xA9 Protomaps";

/// Opens a pack held in memory. Deliberately the memory backend: attribution
/// has nothing to do with the filesystem, and the accessor should not need
/// the kernel doubles to be exercised.
Container openInMemory(const std::string& bytes, OpenResult& out)
{
    Container c;
    out = c.openFromMemory(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(),
                           /*skipCrcVerify=*/false);
    return c;
}

TEST(ContainerAttribution, ReadsBackTheStringThePackCarries)
{
    PackSpec spec;
    spec.attribution = kProtomapsAttr;
    OpenResult r;
    Container  c = openInMemory(buildPack(spec), r);
    ASSERT_EQ(r, OpenResult::Ok);

    EXPECT_EQ(c.attributionLength(), std::strlen(kProtomapsAttr));

    char buf[128] = { 'x' };
    ASSERT_TRUE(c.attribution(buf, sizeof buf));
    EXPECT_STREQ(buf, kProtomapsAttr);
}

/// The payload is not NUL-terminated on disk, so the accessor owes the caller
/// a terminator it can hand straight to text rendering.
TEST(ContainerAttribution, NulTerminatesWhatItCopies)
{
    PackSpec spec;
    spec.attribution = "Map data from OpenStreetMap (ODbL)";
    OpenResult r;
    Container  c = openInMemory(buildPack(spec), r);
    ASSERT_EQ(r, OpenResult::Ok);

    char buf[64];
    std::memset(buf, 'Z', sizeof buf);
    ASSERT_TRUE(c.attribution(buf, sizeof buf));
    EXPECT_EQ(buf[spec.attribution.size()], '\0');
    EXPECT_EQ(std::strlen(buf), spec.attribution.size());
}

TEST(ContainerAttribution, APackWithoutAttrReportsNone)
{
    OpenResult r;
    Container  c = openInMemory(buildPack(PackSpec{}), r);  // attribution empty
    ASSERT_EQ(r, OpenResult::Ok);

    EXPECT_EQ(c.attributionLength(), 0u);
    char buf[64] = { 'x' };
    EXPECT_FALSE(c.attribution(buf, sizeof buf));
}

/// The case the header's note is about. A buffer one byte short of holding
/// the string and its NUL gets nothing, not a prefix.
TEST(ContainerAttribution, RefusesToTruncateIntoATooSmallBuffer)
{
    PackSpec spec;
    spec.attribution = kProtomapsAttr;
    OpenResult r;
    Container  c = openInMemory(buildPack(spec), r);
    ASSERT_EQ(r, OpenResult::Ok);

    const size_t exact = c.attributionLength() + 1;
    std::string  scratch(exact, 'Z');

    EXPECT_FALSE(c.attribution(&scratch[0], exact - 1));
    EXPECT_EQ(scratch[0], 'Z') << "a refused copy must not have written anything";

    EXPECT_TRUE(c.attribution(&scratch[0], exact)) << "exactly enough room must succeed";
}

TEST(ContainerAttribution, AClosedContainerReportsNothing)
{
    PackSpec spec;
    spec.attribution = kProtomapsAttr;
    OpenResult r;
    Container  c = openInMemory(buildPack(spec), r);
    ASSERT_EQ(r, OpenResult::Ok);
    ASSERT_NE(c.attributionLength(), 0u);

    c.close();
    EXPECT_EQ(c.attributionLength(), 0u);
    char buf[128];
    EXPECT_FALSE(c.attribution(buf, sizeof buf));
}

/// Reusing one Container across packs must not leave the previous pack's
/// credit attached to the next one — which would attribute a map to a source
/// it did not come from.
TEST(ContainerAttribution, ReopeningOnAPackWithoutAttrClearsTheOldOne)
{
    PackSpec attributed;
    attributed.attribution = kProtomapsAttr;
    const std::string withAttr    = buildPack(attributed);
    const std::string withoutAttr = buildPack(PackSpec{});

    Container c;
    ASSERT_EQ(c.openFromMemory(reinterpret_cast<const uint8_t*>(withAttr.data()),
                               withAttr.size(), false),
              OpenResult::Ok);
    ASSERT_NE(c.attributionLength(), 0u);

    ASSERT_EQ(c.openFromMemory(reinterpret_cast<const uint8_t*>(withoutAttr.data()),
                               withoutAttr.size(), false),
              OpenResult::Ok);
    EXPECT_EQ(c.attributionLength(), 0u);
    char buf[128];
    EXPECT_FALSE(c.attribution(buf, sizeof buf));
}

/// Multi-source packs separate their per-source strings with LF (§ 7.3). The
/// accessor hands back the payload verbatim; splitting it for display is the
/// caller's job, and this pins that it is not silently mangled.
TEST(ContainerAttribution, LfSeparatedMultiSourceStringsSurviveVerbatim)
{
    PackSpec spec;
    spec.attribution = "Map data from OpenStreetMap (ODbL)\nelevation \xC2\xA9 Copernicus";
    OpenResult r;
    Container  c = openInMemory(buildPack(spec), r);
    ASSERT_EQ(r, OpenResult::Ok);

    char buf[128];
    ASSERT_TRUE(c.attribution(buf, sizeof buf));
    EXPECT_EQ(std::string(buf), spec.attribution);
    EXPECT_NE(std::string(buf).find('\n'), std::string::npos);
}

/// A pack whose ATTR breaks § 11 #38 must not open at all, so there is no
/// question of displaying it. Trailing LF is the rule most likely to be
/// tripped by a writer that builds the string by concatenation.
TEST(ContainerAttribution, APackWithATrailingLfIsRejectedAtOpen)
{
    PackSpec spec;
    spec.attribution = "Map data from OpenStreetMap (ODbL)\n";
    OpenResult r;
    Container  c = openInMemory(buildPack(spec), r);
    EXPECT_EQ(r, OpenResult::BadSrcdOrAttrText);
    EXPECT_FALSE(c.isOpen());
}

/// The length accessor exists so a caller can size a buffer; a caller that
/// trusts it must get a copy that fits exactly.
TEST(ContainerAttribution, ReportedLengthIsExactlyWhatACopyNeeds)
{
    for (const char* s : { "a",
                           "Map data from OpenStreetMap (ODbL)",
                           kProtomapsAttr }) {
        PackSpec spec;
        spec.attribution = s;
        OpenResult r;
        Container  c = openInMemory(buildPack(spec), r);
        ASSERT_EQ(r, OpenResult::Ok) << s;

        std::string buf(c.attributionLength() + 1, '\0');
        ASSERT_TRUE(c.attribution(&buf[0], buf.size())) << s;
        EXPECT_STREQ(buf.c_str(), s);
    }
}

// ---------------------------------------------------------------------------
// peekAttribution -- the same string, without paying for a structural open.
// ---------------------------------------------------------------------------

const char* const kPackPath = "../SharedData/maps/city.rawtiles";

struct PeekFixture : public ::testing::Test {
    SDK::TestSupport::KernelFixture fx;

    bool peek(const std::string& packBytes, char* dst, size_t dstSize)
    {
        fx.fileSystem.seedFile(kPackPath, packBytes);
        return Container::peekAttribution(fx.fileSystem, kPackPath, dst, dstSize);
    }
};

TEST_F(PeekFixture, ReadsTheSameStringAFullOpenWould)
{
    PackSpec spec;
    spec.attribution = kProtomapsAttr;
    const std::string bytes = buildPack(spec);

    char peeked[128] = { 'x' };
    ASSERT_TRUE(peek(bytes, peeked, sizeof peeked));

    // The differential that matters: the cheap path and the thorough path
    // must agree, or one of them is lying about what the pack credits.
    OpenResult r;
    Container  full = openInMemory(bytes, r);
    ASSERT_EQ(r, OpenResult::Ok);
    char opened[128] = { 'y' };
    ASSERT_TRUE(full.attribution(opened, sizeof opened));

    EXPECT_STREQ(peeked, opened);
    EXPECT_STREQ(peeked, kProtomapsAttr);
}

TEST_F(PeekFixture, APackWithoutAttrPeeksAsNone)
{
    char buf[128] = { 'x' };
    EXPECT_FALSE(peek(buildPack(PackSpec{}), buf, sizeof buf));
}

TEST_F(PeekFixture, AMissingFileIsNotAnError)
{
    char buf[128];
    EXPECT_FALSE(Container::peekAttribution(fx.fileSystem, "../SharedData/maps/absent.rawtiles",
                                            buf, sizeof buf));
}

TEST_F(PeekFixture, SomethingThatIsNotAPackPeeksAsNone)
{
    PackSpec spec;
    spec.attribution = kProtomapsAttr;
    spec.goodMagic   = false;
    char buf[128];
    EXPECT_FALSE(peek(buildPack(spec), buf, sizeof buf));
}

/// The peek skips the tile index, so it must not skip the *text* rules too --
/// otherwise the cheap path would display bytes the thorough path rejects.
TEST_F(PeekFixture, StillEnforcesTheAttrTextRules)
{
    PackSpec spec;
    spec.attribution = "Map data from OpenStreetMap (ODbL)\n";  // trailing LF
    char buf[128];
    EXPECT_FALSE(peek(buildPack(spec), buf, sizeof buf));
}

TEST_F(PeekFixture, RefusesToTruncateJustAsTheFullPathDoes)
{
    PackSpec spec;
    spec.attribution = kProtomapsAttr;
    char tooSmall[8] = { 'x' };
    EXPECT_FALSE(peek(buildPack(spec), tooSmall, sizeof tooSmall));
    EXPECT_EQ(tooSmall[0], 'x');
}

/// A truncated file must be refused rather than read past the end.
TEST_F(PeekFixture, ATruncatedPackPeeksAsNone)
{
    PackSpec spec;
    spec.attribution = kProtomapsAttr;
    std::string bytes = buildPack(spec);
    bytes.resize(bytes.size() / 2);

    char buf[128];
    EXPECT_FALSE(peek(bytes, buf, sizeof buf));
}

} // namespace
