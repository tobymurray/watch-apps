/**
 * @file DumpManifest_test.cpp
 * @brief Pins the manifest's exact bytes against the host reassembler's regexes.
 *
 * The expectations here are transcriptions of what `reassemble_dump.py` matches
 * (MANIFEST_HEADER_RE, CHUNK_RE, WHOLE_RE, SPOT_RE). They are written as whole
 * literal lines rather than as field-by-field assertions because the thing
 * under test is a text format: a token renamed, a space lost or an `=` moved
 * makes the line unparseable, and only comparing the whole line catches that.
 */

#include <string>

#include <gtest/gtest.h>

#include "DumpManifest.hpp"

namespace {

std::string textOf(const DumpManifest& manifest)
{
    return std::string(manifest.text(), manifest.length());
}

} // namespace

TEST(DumpManifest, HeaderMatchesTheHostFormat)
{
    DumpManifest manifest;
    manifest.addHeader(0x08000000u, 0x00400000u, 0x00020000u, 0x00001000u, 32);

    EXPECT_EQ("DUMP base=08000000 size=00400000 chunk=00020000 subwrite=00001000 nchunks=32\n",
              textOf(manifest));
    EXPECT_FALSE(manifest.overflowed());
}

TEST(DumpManifest, ChunkLineMatchesTheHostFormat)
{
    DumpManifest manifest;
    manifest.addChunk(0, 32, 0x000000u, 0x020000u, 0x1A2B3C4Du, 131072, true);
    manifest.addChunk(1, 32, 0x020000u, 0x020000u, 0x00000001u, 65536, false);

    EXPECT_EQ("DUMP chunk=0/32 off=00000000 size=00020000 crc32=1A2B3C4D bw=131072 ok=Y\n"
              "DUMP chunk=1/32 off=00020000 size=00020000 crc32=00000001 bw=65536 ok=N\n",
              textOf(manifest));
}

TEST(DumpManifest, WholeImageLineMatchesTheHostFormat)
{
    DumpManifest manifest;
    manifest.addWhole(0xBCD2F8E0u); // The value the one verified prior run produced.

    EXPECT_EQ("DUMP whole_image_crc32=BCD2F8E0\n", textOf(manifest));
}

TEST(DumpManifest, SpotLineIsUppercaseHexWithNoSeparators)
{
    DumpManifest manifest;
    const uint8_t bytes[] = {0x00, 0x00, 0x1F, 0x20, 0x45, 0x1C, 0x00, 0x08,
                            0xFF, 0xFF, 0xFF, 0xFF, 0x0A, 0x0B, 0x0C, 0x0D};
    manifest.addSpot(0x08000000u, bytes, sizeof(bytes));

    // The host compares this field against the reassembled image's hex, so
    // case and the absence of separators are both load-bearing.
    EXPECT_EQ("DUMP spot addr=08000000 bytes=00001F20451C0008FFFFFFFF0A0B0C0D\n",
              textOf(manifest));
}

TEST(DumpManifest, SpotLineClampsToKSpotBytes)
{
    DumpManifest manifest;
    uint8_t bytes[DumpManifest::kSpotBytes * 2];
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        bytes[i] = 0xAB;
    }

    // Twice as many bytes as a spot line carries. Clamped, not overrun: the hex
    // field is exactly kSpotBytes pairs wide, and a shorter-than-asked-for spot
    // still cross-checks correctly because the host reads the byte count back
    // out of the field's own length.
    manifest.addSpot(0x08000000u, bytes, sizeof(bytes));

    std::string expected = "DUMP spot addr=08000000 bytes=";
    for (size_t i = 0; i < DumpManifest::kSpotBytes; ++i) {
        expected += "AB";
    }
    expected += "\n";

    EXPECT_EQ(expected, textOf(manifest));
}

TEST(DumpManifest, ResetClearsEverything)
{
    DumpManifest manifest;
    manifest.addHeader(0x08000000u, 0x00400000u, 0x00020000u, 0x00001000u, 32);
    ASSERT_FALSE(textOf(manifest).empty());

    manifest.reset();
    EXPECT_EQ(0u, manifest.length());
    EXPECT_EQ(std::string(), textOf(manifest));
    EXPECT_FALSE(manifest.overflowed());
}

// A manifest that quietly loses its tail is worse than one that reports
// failure: the reassembler treats missing chunk lines as chunks that never
// happened, zero-fills them and calls the rest a success. So overflow has to be
// visible to the dumper, and the text must never contain a half-written line.
TEST(DumpManifest, OverflowIsReportedAndNeverTruncatesALine)
{
    DumpManifest manifest;
    manifest.addHeader(0x08000000u, 0x10000000u, 0x00001000u, 0x00001000u, 65536);

    for (unsigned i = 0; i < 65536 && !manifest.overflowed(); ++i) {
        manifest.addChunk(i, 65536, i * 0x1000u, 0x1000u, 0xDEADBEEFu, 4096, true);
    }

    ASSERT_TRUE(manifest.overflowed());
    EXPECT_LE(manifest.length(), DumpManifest::kMaxText);

    // Whatever fitted must be whole lines: the last character is a newline and
    // there is no partial record for a regex to half-match.
    const std::string text = textOf(manifest);
    ASSERT_FALSE(text.empty());
    EXPECT_EQ('\n', text.back());
}

TEST(DumpManifest, OverflowStopsAppendingRatherThanWrappingRound)
{
    DumpManifest manifest;
    manifest.addHeader(0x08000000u, 0x10000000u, 0x00001000u, 0x00001000u, 65536);
    for (unsigned i = 0; i < 65536 && !manifest.overflowed(); ++i) {
        manifest.addChunk(i, 65536, i * 0x1000u, 0x1000u, 0xDEADBEEFu, 4096, true);
    }
    ASSERT_TRUE(manifest.overflowed());

    const std::string before = textOf(manifest);
    manifest.addWhole(0x12345678u);
    EXPECT_EQ(before, textOf(manifest)) << "an overflowed manifest must not grow further";
}
