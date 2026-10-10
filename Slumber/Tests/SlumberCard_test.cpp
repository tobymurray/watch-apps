#include <gtest/gtest.h>

#include <cstring>

#include "SlumberCard.hpp"

using namespace SlumberCard;

namespace
{

Card sample()
{
    Card c;
    c.seq             = 7;
    c.writtenUtc      = 1'791'500'000u;
    c.heartbeatUtc    = 1'791'500'900u;
    c.validUntilUtc   = 1'791'560'000u;
    c.serviceStartUtc = 1'791'400'000u;
    c.launchNo        = 3;
    c.state           = State::Report;
    c.sleepMin        = 424;
    c.stillMin        = 401;
    c.deltaVsUsualMin = -12;
    c.onsetMinOfDay   = 23 * 60 + 12;
    c.wakeMinOfDay    = 6 * 60 + 41;
    return c;
}

} // namespace

TEST(SlumberCard, RoundTripsEveryField)
{
    uint8_t bytes[kSize];
    encode(sample(), bytes);

    Card out;
    ASSERT_EQ(decode(bytes, kSize, out), Decode::Ok);
    const Card in = sample();
    EXPECT_EQ(out.seq, in.seq);
    EXPECT_EQ(out.writtenUtc, in.writtenUtc);
    EXPECT_EQ(out.heartbeatUtc, in.heartbeatUtc);
    EXPECT_EQ(out.validUntilUtc, in.validUntilUtc);
    EXPECT_EQ(out.serviceStartUtc, in.serviceStartUtc);
    EXPECT_EQ(out.launchNo, in.launchNo);
    EXPECT_EQ(out.state, in.state);
    EXPECT_EQ(out.sleepMin, in.sleepMin);
    EXPECT_EQ(out.stillMin, in.stillMin);
    EXPECT_EQ(out.deltaVsUsualMin, in.deltaVsUsualMin);
    EXPECT_EQ(out.onsetMinOfDay, in.onsetMinOfDay);
    EXPECT_EQ(out.wakeMinOfDay, in.wakeMinOfDay);
}

/// The layout is a contract between two separately installed apps, so the
/// bytes themselves are pinned, not just the round trip.
TEST(SlumberCard, LayoutIsLittleEndianAndPinned)
{
    uint8_t bytes[kSize];
    encode(sample(), bytes);
    EXPECT_EQ(std::memcmp(bytes, "SLMB", 4), 0);
    EXPECT_EQ(bytes[4], 1);   // schema
    EXPECT_EQ(bytes[6], 48);  // length
    EXPECT_EQ(bytes[8], 7);   // seq
    EXPECT_EQ(bytes[32], static_cast<uint8_t>(State::Report));
    EXPECT_EQ(bytes[38], 0xF4);  // -12, low byte
    EXPECT_EQ(bytes[39], 0xFF);
}

TEST(SlumberCard, CrcMatchesZlib)
{
    const char *text = "123456789";
    EXPECT_EQ(crc32(reinterpret_cast<const uint8_t *>(text), 9), 0xCBF43926u);
}

TEST(SlumberCard, EveryTornLengthIsRejected)
{
    uint8_t bytes[kSize];
    encode(sample(), bytes);
    Card out;
    for (size_t len = 0; len < kSize; ++len) {
        EXPECT_EQ(decode(bytes, len, out), Decode::Short) << len;
    }
}

TEST(SlumberCard, AnyFlippedByteIsRejected)
{
    for (size_t i = 0; i < kSize; ++i) {
        uint8_t bytes[kSize];
        encode(sample(), bytes);
        bytes[i] ^= 0x01;
        Card out;
        EXPECT_NE(decode(bytes, kSize, out), Decode::Ok) << "byte " << i;
    }
}

TEST(SlumberCard, AnotherSchemaIsRefusedNotMisread)
{
    uint8_t bytes[kSize];
    encode(sample(), bytes);
    bytes[4] = 2;
    Card out;
    EXPECT_EQ(decode(bytes, kSize, out), Decode::BadSchema);
}

TEST(SlumberCard, AFailedDecodeLeavesTheOutputAlone)
{
    uint8_t bytes[kSize];
    encode(sample(), bytes);
    bytes[kSize - 1] ^= 0xFF;
    Card out;
    out.seq = 99;
    ASSERT_EQ(decode(bytes, kSize, out), Decode::BadCrc);
    EXPECT_EQ(out.seq, 99u);
}
