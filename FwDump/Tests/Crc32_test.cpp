/**
 * @file Crc32_test.cpp
 * @brief Proves the on-device CRC is byte-compatible with Python's zlib.crc32.
 *
 * This is the test that has to exist before any device run. The host
 * reassembler recomputes every chunk's CRC with zlib.crc32 and reports a
 * disagreement as a corrupt dump, so an incompatible CRC here does not produce
 * a wrong number on a screen -- it condemns every honest dump the app will ever
 * take, and it does so in a way that looks exactly like failing hardware.
 *
 * Every expectation below is a literal, hardcoded value produced by running
 * zlib.crc32 on the same bytes. That matters: a test that compared this
 * implementation against another copy of itself, or that recomputed the
 * expectation with the same table, would pass just as happily with the wrong
 * polynomial.
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Crc32.hpp"

namespace {

std::vector<uint8_t> patternBytes(size_t count)
{
    // Deliberately not all-zero or all-0xFF: a byte sequence that exercises
    // every table entry catches a table built with a bit-reversed polynomial,
    // which uniform input can hide.
    std::vector<uint8_t> out(count);
    for (size_t i = 0; i < count; ++i) {
        out[i] = static_cast<uint8_t>((i * 31 + 7) & 0xFF);
    }
    return out;
}

uint32_t crcOf(const std::string& text)
{
    return Crc32::of(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

} // namespace

// The published check value for CRC-32/ISO-HDLC. If this one line fails, the
// variant is wrong and nothing else in the file matters.
TEST(Crc32, MatchesPublishedCheckValue)
{
    EXPECT_EQ(0xCBF43926u, crcOf("123456789"));
}

TEST(Crc32, MatchesZlibOnEmptyInput)
{
    // zlib.crc32(b"") == 0, which falls out of init ^ xorout rather than being
    // a special case -- worth pinning, because an implementation that seeded
    // with 0 instead of all-ones also returns 0 here and differs everywhere
    // else.
    EXPECT_EQ(0x00000000u, crcOf(""));
}

TEST(Crc32, MatchesZlibOnSingleByte)
{
    EXPECT_EQ(0xE8B7BE43u, crcOf("a"));
}

TEST(Crc32, MatchesZlibOnErasedFlashPattern)
{
    // 0xFF fill is not a curiosity here: roughly half the default 4 MB region
    // is erased flash, so this is the single most common chunk content the app
    // will ever hash.
    const std::vector<uint8_t> ones(16, 0xFF);
    EXPECT_EQ(0x3FB3C61Au, Crc32::of(ones.data(), ones.size()));

    const std::vector<uint8_t> chunk(131072, 0xFF);
    EXPECT_EQ(0x154803CCu, Crc32::of(chunk.data(), chunk.size()));
}

TEST(Crc32, MatchesZlibOnZeroFill)
{
    const std::vector<uint8_t> zeros(4096, 0x00);
    EXPECT_EQ(0xC71C0011u, Crc32::of(zeros.data(), zeros.size()));
}

TEST(Crc32, MatchesZlibOnMixedPattern)
{
    const std::vector<uint8_t> data = patternBytes(4096);
    EXPECT_EQ(0x5D1C4EE3u, Crc32::of(data.data(), data.size()));
}

// The dumper never hashes a chunk in one call: it folds sub-writes in as it
// walks them, and chains the whole-image value across every chunk in order. If
// chunking the input changed the result, the manifest's per-chunk CRCs and its
// whole-image CRC would disagree with the host over the same bytes.
TEST(Crc32, ChunkedUpdateEqualsOneShot)
{
    const std::vector<uint8_t> data = patternBytes(4096);

    for (const size_t block : {1u, 3u, 64u, 512u, 4096u}) {
        uint32_t running = Crc32::kInit;
        for (size_t at = 0; at < data.size(); at += block) {
            const size_t take = std::min(block, data.size() - at);
            running = Crc32::update(running, data.data() + at, take);
        }
        EXPECT_EQ(0x5D1C4EE3u, Crc32::finalise(running)) << "block size " << block;
    }
}

TEST(Crc32, FinaliseIsNotIdempotent)
{
    // Guards a plausible misuse: feeding a finalised value back into update()
    // as if it were a running one. Spelling it out here means the invariant is
    // stated somewhere, not just implied by the dumper happening to get it
    // right.
    const uint32_t running = Crc32::update(Crc32::kInit, nullptr, 0);
    EXPECT_NE(Crc32::finalise(running), Crc32::finalise(Crc32::finalise(running)));
}
