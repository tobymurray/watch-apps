// Host tests for the research-mode marker sidecar.
//
// The sidecar exists so a recording can be labelled after the fact: which
// press bookended the set, which rep to throw away. Two properties carry that
// and are asserted here:
//
//   * a marker's t_ms is on the recording's clock, so a marker row and a
//     sample row with the same t_ms are the same instant — the reason both are
//     begun from one sensor tick rather than each from its own "now";
//   * a marker reaches storage when it is made, not when the session ends,
//     because a session cannot be re-marked afterwards.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ImuMarkerLog.hpp"

namespace {

/// Sink that keeps everything in memory, and can be told to start failing.
class MemorySink : public ImuMarkerLog::ISink {
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
    uint32_t writes() const { return mWrites; }
    uint32_t flushes() const { return mFlushes; }
    /// Bytes that had been written by the time of the last flush.
    size_t flushedLen() const { return mFlushedLen; }

    void failFromWrite(uint32_t n) { mFailFromWrite = n; }
    void failFlush() { mFailFlush = true; }

private:
    std::string mData;
    uint32_t    mWrites        = 0;
    uint32_t    mFlushes       = 0;
    size_t      mFlushedLen    = 0;
    uint32_t    mFailFromWrite = 0;
    bool        mFailFlush     = false;
};

std::vector<std::string> splitLines(const std::string& s)
{
    std::vector<std::string> out;
    std::string              cur;
    for (const char c : s) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

TEST(ImuMarkerLog, EmitsHeaderOnBegin)
{
    MemorySink   sink;
    ImuMarkerLog log;

    ASSERT_TRUE(log.begin(sink, 1000));
    EXPECT_EQ(sink.data(), "t_ms,seq,kind\n");
    EXPECT_TRUE(log.isRecording());
    EXPECT_EQ(log.markerCount(), 0u);
}

// A session that is armed but never marked must still leave a readable file,
// not a zero-byte one that cannot be told apart from a storage failure.
TEST(ImuMarkerLog, HeaderOnlyFileIsFlushedAndWellFormed)
{
    MemorySink   sink;
    ImuMarkerLog log;

    ASSERT_TRUE(log.begin(sink, 0));
    EXPECT_GE(sink.flushes(), 1u);
    EXPECT_EQ(sink.flushedLen(), sink.data().size());

    EXPECT_TRUE(log.end());
    EXPECT_EQ(splitLines(sink.data()).size(), 1u);
}

// The property the whole design rests on: markers are relative to the tick the
// log was begun with, which is the tick the sample recorder was begun with.
TEST(ImuMarkerLog, TimestampsAreRelativeToBeginTick)
{
    MemorySink   sink;
    ImuMarkerLog log;

    ASSERT_TRUE(log.begin(sink, 500000));
    EXPECT_TRUE(log.mark(500000));           // t = 0
    EXPECT_TRUE(log.mark(503210));           // t = 3210
    EXPECT_TRUE(log.mark(547780));           // t = 47780

    const auto lines = splitLines(sink.data());
    ASSERT_EQ(lines.size(), 4u);
    EXPECT_EQ(lines[0], "t_ms,seq,kind");
    EXPECT_EQ(lines[1], "0,1,0");
    EXPECT_EQ(lines[2], "3210,2,0");
    EXPECT_EQ(lines[3], "47780,3,0");
}

// Same unsigned arithmetic as the sample recorder, so a session that spans the
// 32-bit tick wrap stays aligned with its samples instead of jumping.
TEST(ImuMarkerLog, TimestampsSurviveTickWrap)
{
    MemorySink   sink;
    ImuMarkerLog log;

    const uint32_t start = 0xFFFFF000u;
    ASSERT_TRUE(log.begin(sink, start));
    EXPECT_TRUE(log.mark(start + 0x2000u));  // wraps past 2^32

    const auto lines = splitLines(sink.data());
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[1], "8192,1,0");
}

TEST(ImuMarkerLog, SequenceIsOneBasedAndGapFree)
{
    MemorySink   sink;
    ImuMarkerLog log;

    ASSERT_TRUE(log.begin(sink, 0));
    for (uint32_t i = 0; i < 5; ++i) {
        ASSERT_TRUE(log.mark(i * 1000u));
    }

    const auto lines = splitLines(sink.data());
    ASSERT_EQ(lines.size(), 6u);
    for (size_t i = 1; i < lines.size(); ++i) {
        EXPECT_NE(lines[i].find("," + std::to_string(i) + ",0"), std::string::npos)
            << "line " << i << " = " << lines[i];
    }
    EXPECT_EQ(log.markerCount(), 5u);
}

// The reason markers are not buffered: a press that is not on storage before
// the next one is a press that a battery pull loses.
TEST(ImuMarkerLog, EveryMarkerIsFlushedImmediately)
{
    MemorySink   sink;
    ImuMarkerLog log;

    ASSERT_TRUE(log.begin(sink, 0));
    const uint32_t afterBegin = sink.flushes();

    ASSERT_TRUE(log.mark(1000));
    EXPECT_EQ(sink.flushes(), afterBegin + 1u);
    EXPECT_EQ(sink.flushedLen(), sink.data().size());

    ASSERT_TRUE(log.mark(2000));
    EXPECT_EQ(sink.flushes(), afterBegin + 2u);
    EXPECT_EQ(sink.flushedLen(), sink.data().size());
}

TEST(ImuMarkerLog, MarkBeforeBeginIsRejected)
{
    ImuMarkerLog log;
    EXPECT_FALSE(log.mark(1000));
    EXPECT_FALSE(log.isRecording());
    EXPECT_EQ(log.markerCount(), 0u);
}

TEST(ImuMarkerLog, MarkAfterEndIsRejected)
{
    MemorySink   sink;
    ImuMarkerLog log;

    ASSERT_TRUE(log.begin(sink, 0));
    ASSERT_TRUE(log.mark(1000));
    ASSERT_TRUE(log.end());

    EXPECT_FALSE(log.mark(2000));
    EXPECT_EQ(log.markerCount(), 1u);
    EXPECT_EQ(log.stopReason(), ImuMarkerLog::Stop::REQUESTED);
}

// A held or bouncing key must not be able to fill the sidecar with junk.
TEST(ImuMarkerLog, StopsAtMarkerLimit)
{
    MemorySink   sink;
    ImuMarkerLog log;

    ASSERT_TRUE(log.begin(sink, 0));
    for (uint16_t i = 0; i < ImuMarkerLog::skMaxMarkers; ++i) {
        ASSERT_TRUE(log.mark(i)) << "at marker " << i;
    }

    EXPECT_FALSE(log.mark(99999));
    EXPECT_FALSE(log.isRecording());
    EXPECT_EQ(log.stopReason(), ImuMarkerLog::Stop::LIMIT);
    EXPECT_EQ(log.markerCount(), ImuMarkerLog::skMaxMarkers);

    // The file is complete, just short — same contract as a capped recording.
    EXPECT_TRUE(log.end());
}

TEST(ImuMarkerLog, WriteFailureIsReportedAndSticky)
{
    MemorySink   sink;
    ImuMarkerLog log;

    ASSERT_TRUE(log.begin(sink, 0));
    sink.failFromWrite(sink.writes() + 1u);

    EXPECT_FALSE(log.mark(1000));
    EXPECT_FALSE(log.isRecording());
    EXPECT_EQ(log.stopReason(), ImuMarkerLog::Stop::SINK_ERROR);

    // end() keeps reporting the torn file rather than claiming success just
    // because the failure happened earlier.
    EXPECT_FALSE(log.end());
    EXPECT_FALSE(log.end());
}

TEST(ImuMarkerLog, FlushFailureOnMarkIsAWriteFailure)
{
    MemorySink   sink;
    ImuMarkerLog log;

    ASSERT_TRUE(log.begin(sink, 0));
    sink.failFlush();

    EXPECT_FALSE(log.mark(1000));
    EXPECT_EQ(log.stopReason(), ImuMarkerLog::Stop::SINK_ERROR);
    EXPECT_FALSE(log.end());
}

TEST(ImuMarkerLog, HeaderWriteFailureLeavesLogStopped)
{
    MemorySink   sink;
    ImuMarkerLog log;

    sink.failFromWrite(1);
    EXPECT_FALSE(log.begin(sink, 0));
    EXPECT_FALSE(log.isRecording());
    EXPECT_EQ(log.stopReason(), ImuMarkerLog::Stop::SINK_ERROR);
}

TEST(ImuMarkerLog, EndWithoutBeginIsHarmless)
{
    ImuMarkerLog log;
    EXPECT_TRUE(log.end());
    EXPECT_EQ(log.stopReason(), ImuMarkerLog::Stop::NONE);
}

} // namespace
