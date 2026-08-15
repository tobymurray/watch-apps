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

} // namespace
