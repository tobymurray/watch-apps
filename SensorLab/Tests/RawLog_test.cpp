/**
 ******************************************************************************
 * @file    RawLog_test.cpp
 * @brief   That the raw log keeps everything, and says so when it does not.
 ******************************************************************************
 *
 * Two claims, and the second matters as much as the first:
 *
 *   1. every batch handed to it comes back byte-identical;
 *   2. a batch that did *not* reach storage is counted, so a file that parses
 *      cleanly can never be mistaken for a complete one.
 *
 * The second is the one a raw log usually gets wrong. A capture that stopped at
 * a cap and said nothing would leave a file that decodes without error, ends at
 * a plausible-looking record, and is missing eleven hours -- and nothing in it
 * would say so.
 *
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "KernelTestDoubles.hpp"

#include "Profile/RawLog.hpp"
#include "Profile/RunLog.hpp"
#include "Stats/Histogram.hpp"

using namespace SensorLab::Profile;

namespace
{

/// Read a little-endian field out of a chunk, the way `raw_decode.py` does.
uint32_t rd32(const std::string &b, size_t off)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(b[off]))
         | (static_cast<uint32_t>(static_cast<uint8_t>(b[off + 1])) << 8)
         | (static_cast<uint32_t>(static_cast<uint8_t>(b[off + 2])) << 16)
         | (static_cast<uint32_t>(static_cast<uint8_t>(b[off + 3])) << 24);
}

uint16_t rd16(const std::string &b, size_t off)
{
    return static_cast<uint16_t>(
        static_cast<uint32_t>(static_cast<uint8_t>(b[off]))
        | (static_cast<uint32_t>(static_cast<uint8_t>(b[off + 1])) << 8));
}

/// A batch of @p count samples with @p fields fields, values derived from a seed
/// so a test can assert the bytes came back rather than merely that some bytes
/// did.
struct Batch
{
    std::vector<uint8_t> bytes;
    uint16_t             count  = 0;
    uint16_t             stride = 0;

    Batch(uint16_t n, uint16_t fields, uint32_t seed)
    {
        stride = static_cast<uint16_t>(sizeof(SDK::Sensor::Data)
                                       + (fields - 1) * sizeof(SDK::Sensor::Data::Field));
        count  = n;
        bytes.assign(static_cast<size_t>(count) * stride, 0);
        for (uint16_t i = 0; i < count; i++) {
            auto *d = reinterpret_cast<SDK::Sensor::Data *>(
                bytes.data() + static_cast<size_t>(i) * stride);
            d->mTimeStamp   = seed + i * 21u;
            d->mTimeStampUs = seed % 1000u;
            for (uint16_t f = 0; f < fields; f++) {
                d->mValue[f].u32 = seed * 1000u + i * 10u + f;
            }
        }
    }

    const SDK::Sensor::Data *base() const
    {
        return reinterpret_cast<const SDK::Sensor::Data *>(bytes.data());
    }
};

struct Fixture
{
    SDK::TestSupport::KernelFixture fx;
    RawLog                          log { fx.kernel };

    std::string chunk(uint32_t seq) const
    {
        char p[40];
        std::snprintf(p, sizeof(p), "raw/1-%lu.bin",
                      static_cast<unsigned long>(seq));
        auto it = fx.fileSystem.files.find(p);
        return (it == fx.fileSystem.files.end()) ? std::string()
                                                 : it->second.content;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// It keeps everything
// ---------------------------------------------------------------------------

TEST(RawLog, AChunkHeaderIsWhatTheFormatSpecSays)
{
    Fixture f;
    f.log.begin(1, 1024 * 1024, 512 * 1024, 12345, 1755553500);
    f.log.end();

    const std::string c = f.chunk(0);
    ASSERT_GE(c.size(), kRawChunkHeader);
    EXPECT_EQ(c.compare(0, 4, "SLRW"), 0);
    EXPECT_EQ(rd32(c, 4),  kRawSchema);
    EXPECT_EQ(rd32(c, 8),  1u);       // run id
    EXPECT_EQ(rd32(c, 12), 0u);       // seq
    EXPECT_EQ(rd32(c, 16), 12345u);   // uptime at open
    EXPECT_EQ(rd32(c, 20), static_cast<uint32_t>(1755553500));  // wall, low half
}

TEST(RawLog, EveryByteOfEveryBatchComesBackIdentical)
{
    // The whole claim of the file, and the only way to test it is byte by byte.
    Fixture f;
    f.log.begin(1, 1024 * 1024, 512 * 1024, 1000, 42);

    std::vector<Batch> sent;
    for (uint32_t i = 0; i < 20; i++) {
        sent.emplace_back(static_cast<uint16_t>(1 + (i % 9)), 3, 100 + i);
        ASSERT_TRUE(f.log.write(0x10, 0x1234, 5000 + i * 195,
                                sent.back().count, sent.back().stride,
                                sent.back().base()))
            << "batch " << i;
    }
    f.log.end();

    EXPECT_EQ(f.log.dropped(), 0u);
    EXPECT_EQ(f.log.failures(), 0u);
    EXPECT_EQ(f.log.batches(), 20u);

    const std::string c = f.chunk(0);
    size_t off = kRawChunkHeader;
    for (size_t i = 0; i < sent.size(); i++) {
        ASSERT_LE(off + kRawRecordHeader, c.size()) << "record " << i;
        EXPECT_EQ(rd32(c, off),      0x10u)  << "type of record " << i;
        // Full 32 bits. A handle above 255 is the case `SDK::Sensor::Connection`
        // truncates, and this file is the evidence that it was not truncated here.
        EXPECT_EQ(rd32(c, off + 4),  0x1234u) << "handle of record " << i;
        EXPECT_EQ(rd32(c, off + 8),  5000u + static_cast<uint32_t>(i) * 195u);
        EXPECT_EQ(rd16(c, off + 12), sent[i].count);
        EXPECT_EQ(rd16(c, off + 14), sent[i].stride);

        const size_t payload = static_cast<size_t>(sent[i].count) * sent[i].stride;
        ASSERT_LE(off + kRawRecordHeader + payload, c.size());
        EXPECT_EQ(std::memcmp(c.data() + off + kRawRecordHeader,
                              sent[i].bytes.data(), payload), 0)
            << "payload of record " << i << " is not byte-identical";
        off += kRawRecordHeader + payload;
    }
    // Nothing after the last record, and nothing between them.
    EXPECT_EQ(off, c.size());
}

TEST(RawLog, TheSampleCountIsTheSumOfTheBatches)
{
    Fixture f;
    f.log.begin(1, 1024 * 1024, 512 * 1024, 0, -1);
    uint64_t expected = 0;
    for (uint32_t i = 0; i < 30; i++) {
        Batch b(static_cast<uint16_t>(1 + i % 10), 3, i);
        f.log.write(0x10, 1, i, b.count, b.stride, b.base());
        expected += b.count;
    }
    f.log.end();
    EXPECT_EQ(f.log.samples(), expected);
}

TEST(RawLog, AFrameWithAMalformedStrideIsStillRecorded)
{
    // The single most valuable frame this app could capture: a stride the kernel
    // never intended. The service rejects it for *parsing* and records it here,
    // because a profiler that discarded it to protect a statistic would be
    // throwing away its best finding.
    Fixture f;
    f.log.begin(1, 1024 * 1024, 512 * 1024, 0, -1);

    // 13 bytes: not sizeof(Data) + n * sizeof(Field) for any n.
    uint8_t junk[13 * 2];
    for (size_t i = 0; i < sizeof(junk); i++) {
        junk[i] = static_cast<uint8_t>(i * 7);
    }
    ASSERT_TRUE(f.log.write(0x30, 9, 777, 2, 13,
                            reinterpret_cast<const SDK::Sensor::Data *>(junk)));
    f.log.end();

    const std::string c = f.chunk(0);
    ASSERT_EQ(c.size(), kRawChunkHeader + kRawRecordHeader + sizeof(junk));
    EXPECT_EQ(rd16(c, kRawChunkHeader + 14), 13u) << "the odd stride is recorded";
    EXPECT_EQ(std::memcmp(c.data() + kRawChunkHeader + kRawRecordHeader,
                          junk, sizeof(junk)), 0);
}

// ---------------------------------------------------------------------------
// It says so when it does not
// ---------------------------------------------------------------------------

TEST(RawLog, TheByteCapStopsCaptureAndCountsWhatFollows)
{
    // A capture that stopped silently would leave a file that decodes without
    // error and is missing most of the run.
    Fixture f;
    // Room for the header plus a handful of records, no more.
    f.log.begin(1, kRawChunkHeader + 4 * (kRawRecordHeader + 3 * 24),
                512 * 1024, 0, -1);

    uint32_t accepted = 0;
    for (uint32_t i = 0; i < 50; i++) {
        Batch b(3, 3, i);
        if (f.log.write(0x10, 1, i, b.count, b.stride, b.base())) {
            accepted++;
        }
    }
    f.log.end();

    EXPECT_GT(accepted, 0u)   << "some batches should have fitted";
    EXPECT_LT(accepted, 50u)  << "the cap should have stopped it";
    EXPECT_EQ(f.log.batches(), accepted);
    EXPECT_EQ(f.log.dropped(), 50u - accepted);
    EXPECT_TRUE(f.log.capReached())
        << "reaching a cap is a decision, and it is recorded as one";
    EXPECT_EQ(f.log.failures(), 0u)
        << "a cap is not a write failure -- one is a decision and one is a fault";

    // And the cap is a real ceiling, not an approximate one.
    EXPECT_LE(f.log.bytes(), kRawChunkHeader + 4 * (kRawRecordHeader + 3 * 24));
}

TEST(RawLog, ABatchLargerThanTheBufferIsDroppedAndCounted)
{
    // Not silently truncated to fit. A batch this large is itself a finding, and
    // half of one would be a frame nobody could interpret.
    Fixture f;
    f.log.begin(1, 16 * 1024 * 1024, 512 * 1024, 0, -1);

    std::vector<uint8_t> huge(kRawBufferBytes + 1024, 0xAB);
    EXPECT_FALSE(f.log.write(0x10, 1, 0,
                             static_cast<uint16_t>(huge.size() / 24), 24,
                             reinterpret_cast<const SDK::Sensor::Data *>(huge.data())));
    f.log.end();

    EXPECT_EQ(f.log.dropped(), 1u);
    EXPECT_EQ(f.log.batches(), 0u);
}

TEST(RawLog, AWriteFailureIsCountedSeparatelyFromACap)
{
    Fixture f;
    f.fx.fileSystem.failWritesAfterBytes = kRawChunkHeader;   // header only
    f.log.begin(1, 16 * 1024 * 1024, 512 * 1024, 0, -1);

    for (uint32_t i = 0; i < 400; i++) {
        Batch b(9, 3, i);
        f.log.write(0x10, 1, i, b.count, b.stride, b.base());
    }
    f.log.end();

    EXPECT_GT(f.log.failures(), 0u);
    EXPECT_FALSE(f.log.capReached())
        << "a write failure is a fault, not a decision, and the two are "
           "distinguished in the manifest";
}

// ---------------------------------------------------------------------------
// Chunking
// ---------------------------------------------------------------------------

TEST(RawLog, ItRotatesChunksAndEachOneStandsAlone)
{
    // FwDump's discipline: a chunk interrupted by the cable loses itself and not
    // the run, and chunk 0 must decode without chunk 1 having been written.
    Fixture f;
    const uint32_t chunkBytes = 4096;
    f.log.begin(1, 1024 * 1024, chunkBytes, 500, 99);

    for (uint32_t i = 0; i < 200; i++) {
        Batch b(9, 3, i);
        f.log.write(0x10, 1, 1000 + i, b.count, b.stride, b.base());
    }
    f.log.end();

    ASSERT_GT(f.log.chunks(), 1u) << "200 batches should not fit one 4 KB chunk";
    EXPECT_EQ(f.log.dropped(), 0u);

    for (uint32_t seq = 0; seq < f.log.chunks(); seq++) {
        const std::string c = f.chunk(seq);
        ASSERT_GE(c.size(), kRawChunkHeader) << "chunk " << seq << " is missing";
        // Every chunk carries its own full header, so it is decodable alone.
        EXPECT_EQ(c.compare(0, 4, "SLRW"), 0) << "chunk " << seq;
        EXPECT_EQ(rd32(c, 12), seq)           << "chunk " << seq << " seq field";
        EXPECT_LE(c.size(), chunkBytes + kRawBufferBytes)
            << "chunk " << seq << " ran well past its rotation size";
    }
}

TEST(RawLog, ZeroCapMeansCaptureOffWhichIsAValidRunNotAFailure)
{
    Fixture f;
    f.log.begin(1, 0, 512 * 1024, 0, -1);
    EXPECT_FALSE(f.log.capturing());

    Batch b(9, 3, 1);
    EXPECT_FALSE(f.log.write(0x10, 1, 0, b.count, b.stride, b.base()));
    f.log.end();

    EXPECT_EQ(f.log.bytes(), 0u);
    EXPECT_EQ(f.log.chunks(), 0u);
    EXPECT_EQ(f.log.failures(), 0u) << "off is not a failure";
    // Dropped counts batches capture *would* have taken. With capture off there
    // was never a promise to keep, so nothing is reported as broken.
    EXPECT_EQ(f.log.dropped(), 0u);
}

// ---------------------------------------------------------------------------
// The `B` rows, and the encoding trap they exposed
// ---------------------------------------------------------------------------

TEST(RunLogBins, AMeasuredZeroIsNotTheSameAsNotEstablished)
{
    // A histogram's `origin` is legitimately 0.0, and it used to be written as
    // the (0, 127) sentinel that means "never established" -- so a reader could
    // not tell a measured zero from an absent measurement. The same conflation
    // would have made a genuine `min` of 0.0 look unmeasured.
    //
    // Found by reading a `B` row out of the round-trip fixture rather than by
    // reasoning, which is the only reason it was found at all.
    SDK::TestSupport::KernelFixture fx;
    RunLog log(fx.kernel);

    RunManifest m {};
    m.runId = 1;
    ASSERT_TRUE(log.begin(m));

    SensorLab::Stats::Histogram<8> h;
    h.reset(1.0f, 0.0f);          // origin is exactly zero
    h.add(0.0f);                  // and so is the only sample
    h.add(0.0f);
    ASSERT_TRUE(log.writeBins(1000, 0x10, BinSeries::SampleDt, h.view()));

    const std::string csv = fx.fileSystem.readFile("runs/1.csv");
    const size_t at = csv.find("B,1000,0x10,");
    ASSERT_NE(at, std::string::npos) << csv;
    const std::string row = csv.substr(at, csv.find('\n', at) - at);

    // width 1.0 = (1000000, -6); origin 0 must be (0, 0), never (0, 127).
    EXPECT_NE(row.find(",1000000,-6,0,0,"), std::string::npos)
        << "origin 0 should encode as (0, 0), a measured zero:\n" << row;
    EXPECT_EQ(row.find(",0,127,"), std::string::npos)
        << "no field of this row was unmeasured:\n" << row;
    // ...and min and max, both exactly zero, likewise.
    EXPECT_EQ(row.find("127"), std::string::npos) << row;
}

TEST(RunLogBins, TheBinsAreSparseAndCarryTheirOwnWidth)
{
    SDK::TestSupport::KernelFixture fx;
    RunLog log(fx.kernel);
    RunManifest m {};
    m.runId = 1;
    ASSERT_TRUE(log.begin(m));

    // A bimodal distribution: 21 ms mostly, 27 ms sometimes. **This is the case
    // the B rows exist for** -- a single p50 sits in one mode and says nothing
    // about the other, and five quantiles chosen in advance cannot show two
    // modes at all.
    SensorLab::Stats::Histogram<128> h;
    h.reset(1.0f, 0.0f);
    for (int i = 0; i < 100; i++) { h.add(21.0f); }
    for (int i = 0; i < 12;  i++) { h.add(27.0f); }
    ASSERT_TRUE(log.writeBins(2000, 0x10, BinSeries::SampleDt, h.view()));

    const std::string csv = fx.fileSystem.readFile("runs/1.csv");
    // Sparse: only the two non-empty bins, as `<idx>:<count>`.
    EXPECT_NE(csv.find("21:100"), std::string::npos) << csv;
    EXPECT_NE(csv.find("27:12"),  std::string::npos) << csv;
    // ...and no bin that was empty.
    EXPECT_EQ(csv.find("22:0"), std::string::npos);
    // Both modes are recoverable, which is the whole claim.
    EXPECT_EQ(csv.find("trunc:"), std::string::npos) << "nothing was truncated";
}

TEST(RunLogBins, AnEmptyHistogramWritesNothing)
{
    // Three series per type per interval; a row of zeroes would be most of the
    // file and would say nothing the absence does not.
    SDK::TestSupport::KernelFixture fx;
    RunLog log(fx.kernel);
    RunManifest m {};
    m.runId = 1;
    ASSERT_TRUE(log.begin(m));

    SensorLab::Stats::Histogram<128> h;
    h.reset(1.0f, 0.0f);
    EXPECT_TRUE(log.writeBins(3000, 0x10, BinSeries::SampleDt, h.view()));

    EXPECT_EQ(fx.fileSystem.readFile("runs/1.csv").find("B,3000"),
              std::string::npos);
}

TEST(RawLog, FlushIsIdempotentAndEndIsSafeTwice)
{
    Fixture f;
    f.log.begin(1, 1024 * 1024, 512 * 1024, 0, -1);
    Batch b(4, 3, 7);
    f.log.write(0x10, 1, 0, b.count, b.stride, b.base());

    EXPECT_TRUE(f.log.flush());
    const uint64_t after = f.log.bytes();
    EXPECT_TRUE(f.log.flush());
    EXPECT_EQ(f.log.bytes(), after) << "a second flush must not duplicate bytes";

    f.log.end();
    f.log.end();
    EXPECT_EQ(f.log.bytes(), after);
}
