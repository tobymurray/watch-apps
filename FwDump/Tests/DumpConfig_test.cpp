/**
 * @file DumpConfig_test.cpp
 * @brief The optional region override, and every way it can be wrong.
 *
 * The rule the tests below all check one way or another: a bad config never
 * stops the app and never half-applies. The app's whole reason to exist is
 * dumping flash, and flash is the default -- so falling back to it is always
 * the right answer to a file nobody can parse.
 */

#include <string>

#include <gtest/gtest.h>

#include "DumpConfig.hpp"
#include "KernelTestDoubles.hpp"

namespace {

DumpConfig::Result loadWith(SDK::TestSupport::KernelFixture& fixture, const std::string& json)
{
    fixture.fileSystem.seedFile(DumpConfig::kPath, json);
    return DumpConfig::load(fixture.kernel);
}

void expectDefaultRegion(const DumpConfig::Result& result)
{
    const DumpRegion expected;
    EXPECT_EQ(expected.base, result.region.base);
    EXPECT_EQ(expected.size, result.region.size);
    EXPECT_EQ(expected.chunk, result.region.chunk);
    EXPECT_EQ(expected.subwrite, result.region.subwrite);
    EXPECT_TRUE(result.region.valid());
}

} // namespace

TEST(DumpConfigTest, NoFileMeansTheFlashDefault)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpConfig::Result result = DumpConfig::load(fixture.kernel);

    EXPECT_EQ(DumpConfig::Status::Default, result.status);
    expectDefaultRegion(result);
}

TEST(DumpConfigTest, AppliesAWellFormedOverride)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpConfig::Result result = loadWith(fixture, R"({
        "schema": 1,
        "base": "20000000",
        "size": "00040000",
        "chunk": "00010000",
        "subwrite": "00001000"
    })");

    ASSERT_EQ(DumpConfig::Status::Ok, result.status);
    EXPECT_EQ(0x20000000u, result.region.base);
    EXPECT_EQ(0x00040000u, result.region.size);
    EXPECT_EQ(0x00010000u, result.region.chunk);
    EXPECT_EQ(0x00001000u, result.region.subwrite);
    EXPECT_EQ(4u, result.region.nchunks());
}

TEST(DumpConfigTest, OmittedFieldsKeepTheirDefaults)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpConfig::Result result = loadWith(fixture, R"({
        "schema": 1,
        "size": "00080000"
    })");

    ASSERT_EQ(DumpConfig::Status::Ok, result.status);
    const DumpRegion defaults;
    EXPECT_EQ(defaults.base, result.region.base);
    EXPECT_EQ(0x00080000u, result.region.size);
    EXPECT_EQ(defaults.chunk, result.region.chunk);
    EXPECT_EQ(defaults.subwrite, result.region.subwrite);
}

TEST(DumpConfigTest, HexIsCaseInsensitiveAndNeedsNoPadding)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpConfig::Result result = loadWith(fixture, R"({
        "schema": 1,
        "base": "8000000",
        "size": "40000",
        "chunk": "10000",
        "subwrite": "1000"
    })");

    ASSERT_EQ(DumpConfig::Status::Ok, result.status);
    EXPECT_EQ(0x08000000u, result.region.base);
    EXPECT_EQ(0x00040000u, result.region.size);
}

TEST(DumpConfigTest, RejectsAnUnknownSchema)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpConfig::Result result = loadWith(fixture, R"({
        "schema": 99,
        "base": "20000000",
        "size": "00040000",
        "chunk": "00010000",
        "subwrite": "00001000"
    })");

    EXPECT_EQ(DumpConfig::Status::WrongSchema, result.status);
    expectDefaultRegion(result);
}

TEST(DumpConfigTest, RejectsAMissingSchema)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpConfig::Result result = loadWith(fixture, R"({"base": "20000000"})");

    EXPECT_EQ(DumpConfig::Status::WrongSchema, result.status);
    expectDefaultRegion(result);
}

TEST(DumpConfigTest, RejectsGarbage)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpConfig::Result result = loadWith(fixture, "this is not json at all");

    EXPECT_EQ(DumpConfig::Status::NotJson, result.status);
    expectDefaultRegion(result);
}

// The case parseHex32 exists for. strtoul would read "0x8000000z" as 0x8000000
// and stop, handing back a plausible address from a field that is plainly
// broken -- and this field names memory the app is about to dereference.
TEST(DumpConfigTest, RejectsAFieldThatIsOnlyPartlyHex)
{
    SDK::TestSupport::KernelFixture fixture;

    for (const char* bad : {"\"0x8000000\"", "\"8000000z\"", "\"\"", "\" 8000000\"",
                            "\"-8000000\"", "\"123456789\""}) {
        fixture.fileSystem.files.clear();
        const std::string json = std::string("{\"schema\": 1, \"base\": ") + bad + "}";
        const DumpConfig::Result result = loadWith(fixture, json);

        EXPECT_EQ(DumpConfig::Status::BadField, result.status) << "accepted base=" << bad;
        expectDefaultRegion(result);
    }
}

TEST(DumpConfigTest, RejectsAGeometryThatDoesNotTile)
{
    SDK::TestSupport::KernelFixture fixture;

    // A size that is not a whole number of chunks.
    DumpConfig::Result result = loadWith(fixture, R"({
        "schema": 1, "size": "00030000", "chunk": "00020000", "subwrite": "00001000"
    })");
    EXPECT_EQ(DumpConfig::Status::BadGeometry, result.status);
    expectDefaultRegion(result);

    // A chunk that is not a whole number of sub-writes.
    fixture.fileSystem.files.clear();
    result = loadWith(fixture, R"({
        "schema": 1, "size": "00040000", "chunk": "00020000", "subwrite": "00000300"
    })");
    EXPECT_EQ(DumpConfig::Status::BadGeometry, result.status);
    expectDefaultRegion(result);

    // A sub-write larger than the dumper's read-back buffer.
    fixture.fileSystem.files.clear();
    result = loadWith(fixture, R"({
        "schema": 1, "size": "00040000", "chunk": "00020000", "subwrite": "00008000"
    })");
    EXPECT_EQ(DumpConfig::Status::BadGeometry, result.status);
    expectDefaultRegion(result);
}

TEST(DumpConfigTest, RejectsAnOversizedFile)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpConfig::Result result =
        loadWith(fixture, std::string(DumpConfig::kMaxFileBytes + 1, 'x'));

    EXPECT_EQ(DumpConfig::Status::TooLarge, result.status);
    expectDefaultRegion(result);
}

TEST(DumpConfigTest, EveryStatusHasADescription)
{
    // The screen shows describe() verbatim, so a status added without a case
    // here would render as "config unknown" to the one person who most needs to
    // know which of these it was.
    for (const DumpConfig::Status status :
         {DumpConfig::Status::Default, DumpConfig::Status::Ok, DumpConfig::Status::TooLarge,
          DumpConfig::Status::NotJson, DumpConfig::Status::WrongSchema,
          DumpConfig::Status::BadField, DumpConfig::Status::BadGeometry}) {
        const char* text = DumpConfig::describe(status);
        ASSERT_NE(nullptr, text);
        EXPECT_STRNE("config unknown", text);
        EXPECT_LT(std::string(text).size(), 32u) << "must fit a line of the screen";
    }
}
