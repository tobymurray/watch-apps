#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "FirmwareStrings.hpp"

namespace {

constexpr uint32_t kBase = 0x08000000u;
constexpr uint32_t kVtor = 0x08060000u;

void put(std::vector<uint8_t>& image, uint32_t address, const char* bytes, size_t length)
{
    std::memcpy(image.data() + (address - kBase), bytes, length);
}

/// Erased flash with the two version strings where the 1.4.0 dump has them,
/// and the bytes either side of each exactly as that dump has them.
std::vector<uint8_t> shapedLike140()
{
    std::vector<uint8_t> image(0x00200000u, 0xFF);
    put(image, 0x08019190u, "ice\0" "0.1.4\0" "Starti", 16);
    put(image, 0x08168250u, "06lu\0" "1.4.0\0" "virtu", 16);
    return image;
}

/// The same, where the 1.5.0 dump has them.
std::vector<uint8_t> shapedLike150()
{
    std::vector<uint8_t> image(0x00200000u, 0xFF);
    put(image, 0x08019190u, "ice\0" "0.1.4\0" "Starti", 16);
    put(image, 0x0817AB81u, "%06lu\0" "1.5.0\0" "vir", 15);
    return image;
}

} // namespace

TEST(FirmwareStrings, NamesTheKernelIn150AsWell)
{
    const std::vector<uint8_t> image = shapedLike150();
    const FirmwareStrings::Result result = FirmwareStrings::scan(image.data(), kBase, image.size());

    ASSERT_EQ(2u, result.total);
    EXPECT_EQ(0x0817AB87u, result.matches[1].address);
    const FirmwareStrings::Match* kernel = result.kernel(kVtor);
    ASSERT_NE(nullptr, kernel);
    EXPECT_STREQ("1.5.0", kernel->text);
}

TEST(FirmwareStrings, FindsBothStringsThe140ImageCarries)
{
    const std::vector<uint8_t> image = shapedLike140();
    const FirmwareStrings::Result result = FirmwareStrings::scan(image.data(), kBase, image.size());

    ASSERT_EQ(2u, result.total);
    EXPECT_EQ(0x08019194u, result.matches[0].address);
    EXPECT_STREQ("0.1.4", result.matches[0].text);
    EXPECT_EQ(0x08168255u, result.matches[1].address);
    EXPECT_STREQ("1.4.0", result.matches[1].text);
}

TEST(FirmwareStrings, TheKernelsIsTheOneAboveItsVectorTable)
{
    const std::vector<uint8_t> image = shapedLike140();
    const FirmwareStrings::Result result = FirmwareStrings::scan(image.data(), kBase, image.size());

    const FirmwareStrings::Match* kernel = result.kernel(kVtor);
    ASSERT_NE(nullptr, kernel);
    EXPECT_STREQ("1.4.0", kernel->text);
}

TEST(FirmwareStrings, NamesNoKernelVersionWhenTwoCandidatesAreAboveVtor)
{
    std::vector<uint8_t> image = shapedLike140();
    put(image, 0x08170000u, "\0" "2.0.1\0", 7);
    const FirmwareStrings::Result result = FirmwareStrings::scan(image.data(), kBase, image.size());

    EXPECT_EQ(3u, result.total);
    EXPECT_EQ(nullptr, result.kernel(kVtor));
}

TEST(FirmwareStrings, NamesNoKernelVersionWhenMatchesWereDropped)
{
    std::vector<uint8_t> image(0x1000u, 0x00);
    for (uint32_t i = 0; i < FirmwareStrings::kMaxFound + 1; ++i) {
        put(image, kBase + 0x10u + i * 8u, "1.2.3", 5);
    }
    const FirmwareStrings::Result result = FirmwareStrings::scan(image.data(), kBase, image.size());

    EXPECT_EQ(FirmwareStrings::kMaxFound, result.kept);
    EXPECT_EQ(FirmwareStrings::kMaxFound + 1, result.total);
    EXPECT_EQ(nullptr, result.kernel(kBase));
}

TEST(FirmwareStrings, IgnoresWhatOnlyLooksLikeAVersion)
{
    std::vector<uint8_t> image(0x100u, 0x00);
    const char* notVersions[] = {"1.4", "1.4.0.2", "v1.4.0", "1.4.0a", "1..4", "1234.0.0", ".1.4.0"};
    uint32_t at = kBase + 1;
    for (const char* text : notVersions) {
        put(image, at, text, std::strlen(text));
        at += static_cast<uint32_t>(std::strlen(text)) + 1;
    }
    const FirmwareStrings::Result result = FirmwareStrings::scan(image.data(), kBase, image.size());

    EXPECT_EQ(0u, result.total);
}

TEST(FirmwareStrings, NeedsANulOnBothSides)
{
    std::vector<uint8_t> image(0x40u, 0xFF);
    put(image, kBase, "1.4.0\0", 6);          // No NUL before: the start of the window.
    put(image, kBase + 0x10u, "\0" "1.5.0", 6); // No NUL after: erased flash.
    const FirmwareStrings::Result result = FirmwareStrings::scan(image.data(), kBase, image.size());

    EXPECT_EQ(0u, result.total);
}
