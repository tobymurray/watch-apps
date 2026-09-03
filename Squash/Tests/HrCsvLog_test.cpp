// Host tests for the research-mode heart-rate sidecar.
//
// The sidecar exists so the first phase of a recovery metric can be measured
// at all: what the heart-rate signal's own settling time is when effort stops,
// and whether wrist optical is usable during play. Three properties carry
// that and are asserted here:
//
//   * a reading's t_ms is on the recording's clock, so a labelled effort
//     transition and the readings around it line up without correlation;
//   * hundredths of a bpm survive, because whole-bpm rounding would discard
//     exactly the sub-bpm steps that are the evidence of kernel smoothing;
//   * both per-source readings are kept beside the arbitrated one, so optical
//     against strap is answerable from one recording rather than two.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "HrCsvLog.hpp"

namespace {

/// Sink that keeps everything in memory, and can be told to start failing.
class MemorySink : public HrCsvLog::ISink {
public:
    bool write(const char* data, size_t len) override
    {
        ++mWrites;
        if (mFailFromWrite != 0 && mWrites >= mFailFromWrite) {
            return false;
        }
        mData.append(data, len);
        return true;
    }

    bool flush() override
    {
        ++mFlushes;
        mFlushedLen = mData.size();
        return !mFailFlush;
    }

    const std::string& data() const { return mData; }
    size_t flushedLen() const { return mFlushedLen; }
    int    flushes() const { return mFlushes; }
    void   failFromWrite(int n) { mFailFromWrite = n; }
    void   failFlush() { mFailFlush = true; }

private:
    std::string mData;
    size_t      mFlushedLen    = 0;
    int         mWrites        = 0;
    int         mFlushes       = 0;
    int         mFailFromWrite = 0;
    bool        mFailFlush     = false;
};

std::vector<std::string> rows(const std::string& csv)
{
    std::vector<std::string> out;
    size_t                   at = 0;
    while (at < csv.size()) {
        const size_t nl = csv.find('\n', at);
        if (nl == std::string::npos) {
            out.push_back(csv.substr(at));
            break;
        }
        out.push_back(csv.substr(at, nl - at));
        at = nl + 1;
    }
    return out;
}

HrCsvLog::Sample reading(float bpm, float optical, float external)
{
    HrCsvLog::Sample s;
    s.bpm         = bpm;
    s.opticalBpm  = optical;
    s.externalBpm = external;
    s.trust       = 2;
    s.source      = HrCsvLog::Source::EXTERNAL;
    return s;
}

} // namespace

TEST(HrCsvLog, WritesAHeaderNamingEveryColumn)
{
    MemorySink sink;
    HrCsvLog   log;

    ASSERT_TRUE(log.begin(sink, 1000));

    EXPECT_EQ(rows(sink.data())[0], "t_ms,bpm_x100,trust,source,optical_x100,external_x100");
}

TEST(HrCsvLog, TimestampsAreRelativeToTheTickTheLogWasBegunWith)
{
    MemorySink sink;
    HrCsvLog   log;

    ASSERT_TRUE(log.begin(sink, 500'000));
    ASSERT_TRUE(log.onSample(500'000, reading(142.5f, 141.0f, 142.5f)));
    ASSERT_TRUE(log.onSample(501'000, reading(142.5f, 141.0f, 142.5f)));

    const auto r = rows(sink.data());
    EXPECT_EQ(r[1].substr(0, r[1].find(',')), "0");
    EXPECT_EQ(r[2].substr(0, r[2].find(',')), "1000");
}

TEST(HrCsvLog, TimestampsStayCorrectAcrossTheTickWrap)
{
    MemorySink sink;
    HrCsvLog   log;

    ASSERT_TRUE(log.begin(sink, 0xFFFFFF00u));
    ASSERT_TRUE(log.onSample(0x00000100u, reading(140.0f, 140.0f, 0.0f)));

    const auto r = rows(sink.data());
    EXPECT_EQ(r[1].substr(0, r[1].find(',')), "512");
}

TEST(HrCsvLog, SubBpmStepsSurviveTheRoundTrip)
{
    MemorySink sink;
    HrCsvLog   log;

    ASSERT_TRUE(log.begin(sink, 0));
    // The two steps CLAUDE.md records from real rides: 0.50 and 0.18 bpm.
    ASSERT_TRUE(log.onSample(0, reading(142.00f, 0.0f, 142.00f)));
    ASSERT_TRUE(log.onSample(1000, reading(142.50f, 0.0f, 142.50f)));
    ASSERT_TRUE(log.onSample(2000, reading(142.68f, 0.0f, 142.68f)));

    const auto r = rows(sink.data());
    EXPECT_EQ(r[1], "0,14200,2,2,0,14200");
    EXPECT_EQ(r[2], "1000,14250,2,2,0,14250");
    EXPECT_EQ(r[3], "2000,14268,2,2,0,14268");
}

TEST(HrCsvLog, KeepsBothSourcesBesideTheArbitratedReading)
{
    MemorySink sink;
    HrCsvLog   log;

    ASSERT_TRUE(log.begin(sink, 0));
    HrCsvLog::Sample s = reading(150.0f, 131.25f, 150.0f);
    s.source           = HrCsvLog::Source::EXTERNAL;
    ASSERT_TRUE(log.onSample(0, s));

    EXPECT_EQ(rows(sink.data())[1], "0,15000,2,2,13125,15000");
}

TEST(HrCsvLog, AnUntrustedReadingIsRecordedRatherThanDropped)
{
    MemorySink sink;
    HrCsvLog   log;

    ASSERT_TRUE(log.begin(sink, 0));
    HrCsvLog::Sample s = reading(0.0f, 0.0f, 0.0f);
    s.trust            = 0;
    s.source           = HrCsvLog::Source::UNKNOWN;
    ASSERT_TRUE(log.onSample(0, s));

    // The file records the gap honestly; deciding what an untrusted reading is
    // worth belongs to whatever reads it.
    EXPECT_EQ(rows(sink.data())[1], "0,0,0,0,0,0");
}

TEST(HrCsvLog, EveryReadingReachesStorageWhenItIsMade)
{
    MemorySink sink;
    HrCsvLog   log;

    ASSERT_TRUE(log.begin(sink, 0));
    const int afterHeader = sink.flushes();
    ASSERT_TRUE(log.onSample(0, reading(140.0f, 0.0f, 140.0f)));

    EXPECT_EQ(sink.flushes(), afterHeader + 1);
    EXPECT_EQ(sink.flushedLen(), sink.data().size());
}

TEST(HrCsvLog, StopsAtTheSampleCeilingAndSaysWhy)
{
    MemorySink sink;
    HrCsvLog   log;

    ASSERT_TRUE(log.begin(sink, 0));
    for (uint32_t i = 0; i < HrCsvLog::skMaxSamples; ++i) {
        ASSERT_TRUE(log.onSample(i * 1000, reading(140.0f, 0.0f, 140.0f))) << "at " << i;
    }

    EXPECT_FALSE(log.onSample(HrCsvLog::skMaxSamples * 1000, reading(140.0f, 0.0f, 140.0f)));
    EXPECT_EQ(log.stopReason(), HrCsvLog::Stop::LIMIT);
    EXPECT_EQ(log.sampleCount(), HrCsvLog::skMaxSamples);
    EXPECT_TRUE(log.end()) << "a run that hit its ceiling is complete, just short";
}

TEST(HrCsvLog, ASinkFailureIsReportedOnThisCallAndEveryLaterOne)
{
    MemorySink sink;
    HrCsvLog   log;

    ASSERT_TRUE(log.begin(sink, 0));
    sink.failFromWrite(2);

    EXPECT_FALSE(log.onSample(0, reading(140.0f, 0.0f, 140.0f)));
    EXPECT_EQ(log.stopReason(), HrCsvLog::Stop::SINK_ERROR);
    EXPECT_FALSE(log.end());
    EXPECT_FALSE(log.end());
}

TEST(HrCsvLog, ASessionThatNeverSawAHeartRateStillLeavesAWellFormedFile)
{
    MemorySink sink;
    HrCsvLog   log;

    ASSERT_TRUE(log.begin(sink, 0));
    EXPECT_TRUE(log.end());
    EXPECT_EQ(rows(sink.data()).size(), 1u) << "the header alone, terminated";
    EXPECT_EQ(sink.flushedLen(), sink.data().size());
}

TEST(HrCsvLog, EndIsSafeWhenNothingWasEverStarted)
{
    HrCsvLog log;
    EXPECT_TRUE(log.end());
    EXPECT_EQ(log.stopReason(), HrCsvLog::Stop::NONE);
}

TEST(HrCsvLog, NoRowCanExceedTheBudgetItsBufferIsSizedFor)
{
    MemorySink sink;
    HrCsvLog   log;

    ASSERT_TRUE(log.begin(sink, 0));
    HrCsvLog::Sample s = reading(1e9f, -1e9f, 1e9f);
    s.trust            = 255;
    s.source           = static_cast<HrCsvLog::Source>(255);
    ASSERT_TRUE(log.onSample(0xFFFFFFFFu, s));

    // The header is written straight through and is not bounded by the row
    // buffer, so only the data rows are checked against it.
    const auto r = rows(sink.data());
    for (size_t i = 1; i < r.size(); ++i) {
        EXPECT_LE(r[i].size() + 1, HrCsvLog::skMaxRowBytes) << r[i];
    }
}
