// Host tests for the Squash research-mode IMU CSV recorder.
//
// The recorder is the first thing the Squash app ships because every later
// tier (shot detection, stroke classification, rally structure) is tuned
// against recordings it produces. Two properties therefore matter most and are
// asserted here:
//
//   * the byte format is exactly what Sensor::ImuFusionSource's CSV playback
//     parser accepts, so a recording replays through the simulator and through
//     these tests unchanged;
//   * the caps are hard — a recording can never outgrow its size budget or run
//     past its duration budget, whatever the sample stream does.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ImuCsvRecorder.hpp"

namespace {

/// Sink that keeps everything in memory, and can be told to start failing.
class MemorySink : public ImuCsvRecorder::ISink {
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
        return !mFailFlush;
    }

    const std::string& data() const { return mData; }
    uint32_t writes() const { return mWrites; }
    uint32_t flushes() const { return mFlushes; }

    /// Fail the Nth write (1-based) and every write after it.
    void failFromWrite(uint32_t n) { mFailFromWrite = n; }
    void failFlush() { mFailFlush = true; }

private:
    std::string mData;
    uint32_t    mWrites        = 0;
    uint32_t    mFlushes       = 0;
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
    // A well-formed recording ends with a newline, so anything left over here
    // is a truncated final row and worth failing on.
    if (!cur.empty()) {
        out.push_back("<UNTERMINATED>" + cur);
    }
    return out;
}

ImuCsvRecorder::Sample sample(int16_t base)
{
    ImuCsvRecorder::Sample s;
    s.ax = base;
    s.ay = static_cast<int16_t>(-base);
    s.az = 4096;
    s.gx = static_cast<int16_t>(base * 2);
    s.gy = 0;
    s.gz = static_cast<int16_t>(-base / 2);
    return s;
}

constexpr char kHeader[] = "t_ms,ax,ay,az,gx,gy,gz";

} // namespace

TEST(ImuCsvRecorder, WritesHeaderThenRowsInRawUnits)
{
    MemorySink     sink;
    ImuCsvRecorder rec;

    ASSERT_TRUE(rec.begin(sink, 1000));
    EXPECT_TRUE(rec.isRecording());

    ImuCsvRecorder::Sample s;
    s.ax = 12; s.ay = -8; s.az = 4096; s.gx = 3; s.gy = -1; s.gz = 0;
    EXPECT_TRUE(rec.onSample(1000, s));
    s.ax = 15; s.ay = -9; s.az = 4090; s.gx = 120; s.gy = -64; s.gz = 17;
    EXPECT_TRUE(rec.onSample(1010, s));
    EXPECT_TRUE(rec.end());

    const auto lines = splitLines(sink.data());
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], kHeader);
    // Timestamps are relative to begin(), so a file always starts at 0.
    EXPECT_EQ(lines[1], "0,12,-8,4096,3,-1,0");
    EXPECT_EQ(lines[2], "10,15,-9,4090,120,-64,17");
    EXPECT_EQ(rec.sampleCount(), 2u);
    EXPECT_FALSE(rec.isRecording());
    EXPECT_EQ(rec.stopReason(), ImuCsvRecorder::Stop::REQUESTED);
}

// The format contract with the simulator's playback parser: 7 comma-separated
// integer fields per row, no spaces, no float notation, one header line.
TEST(ImuCsvRecorder, EveryRowIsSevenIntegerFieldsParserWillAccept)
{
    MemorySink     sink;
    ImuCsvRecorder rec;
    ASSERT_TRUE(rec.begin(sink, 0));

    // Include the extremes: a clipped stroke rails both ranges, and int16 min
    // is the widest field the formatter can be asked for.
    ImuCsvRecorder::Sample extremes;
    extremes.ax = INT16_MIN; extremes.ay = INT16_MAX; extremes.az = 0;
    extremes.gx = INT16_MIN; extremes.gy = INT16_MAX; extremes.gz = -1;
    EXPECT_TRUE(rec.onSample(0, extremes));
    for (int i = 1; i < 50; ++i) {
        EXPECT_TRUE(rec.onSample(static_cast<uint32_t>(i * 10), sample(static_cast<int16_t>(i * 37))));
    }
    ASSERT_TRUE(rec.end());

    const auto lines = splitLines(sink.data());
    ASSERT_EQ(lines.size(), 51u);
    EXPECT_EQ(lines[0], kHeader);
    EXPECT_EQ(lines[1], "0,-32768,32767,0,-32768,32767,-1");

    for (size_t i = 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        int                fields = 1;
        for (const char c : line) {
            if (c == ',') {
                ++fields;
            } else {
                ASSERT_TRUE(c == '-' || (c >= '0' && c <= '9'))
                    << "row " << i << " has a non-integer character: " << line;
            }
        }
        EXPECT_EQ(fields, 7) << "row " << i << " is not 7 fields: " << line;
    }
}

TEST(ImuCsvRecorder, SizeCapIsNeverExceeded)
{
    MemorySink     sink;
    ImuCsvRecorder rec;

    ImuCsvRecorder::Limits limits;
    limits.maxBytes      = 512;
    limits.maxDurationMs = 60u * 60u * 1000u;   // not the binding cap here

    ASSERT_TRUE(rec.begin(sink, 0, limits));
    uint32_t t = 0;
    while (rec.onSample(t, sample(static_cast<int16_t>(t % 3000)))) {
        t += 10;
        ASSERT_LT(t, 100000u) << "recorder never hit its size cap";
    }

    EXPECT_EQ(rec.stopReason(), ImuCsvRecorder::Stop::SIZE_LIMIT);
    EXPECT_FALSE(rec.isRecording());
    EXPECT_LE(sink.data().size(), limits.maxBytes);
    EXPECT_GT(rec.sampleCount(), 0u);
    // Cap enforcement must not corrupt the format: the last row is complete.
    const auto lines = splitLines(sink.data());
    for (const auto& l : lines) {
        EXPECT_EQ(l.rfind("<UNTERMINATED>", 0), std::string::npos) << l;
    }
}

TEST(ImuCsvRecorder, DurationCapStopsAtTheBoundary)
{
    MemorySink     sink;
    ImuCsvRecorder rec;

    ImuCsvRecorder::Limits limits;
    limits.maxBytes      = 10u * 1024u * 1024u;  // not the binding cap here
    limits.maxDurationMs = 1000;

    ASSERT_TRUE(rec.begin(sink, 5000, limits));
    // 100 Hz for exactly the cap: t=5000..5990 are inside, 6000 is the boundary.
    for (uint32_t i = 0; i < 100; ++i) {
        EXPECT_TRUE(rec.onSample(5000 + i * 10, sample(1))) << "sample " << i;
    }
    EXPECT_FALSE(rec.onSample(6000, sample(1)));

    EXPECT_EQ(rec.stopReason(), ImuCsvRecorder::Stop::DURATION_LIMIT);
    EXPECT_EQ(rec.sampleCount(), 100u);
}

// The kernel tick is a 32-bit millisecond counter that wraps about every 49.7
// days. A recording that straddles the wrap must keep counting forward.
TEST(ImuCsvRecorder, TimestampsSurviveTheTickWrap)
{
    MemorySink     sink;
    ImuCsvRecorder rec;

    const uint32_t start = 0xFFFFFF00u;   // 256 ms before the wrap
    ASSERT_TRUE(rec.begin(sink, start));
    EXPECT_TRUE(rec.onSample(start, sample(1)));            // rel 0
    EXPECT_TRUE(rec.onSample(start + 100u, sample(2)));     // rel 100, pre-wrap
    EXPECT_TRUE(rec.onSample(start + 300u, sample(3)));     // rel 300, past the wrap
    ASSERT_TRUE(rec.end());

    const auto lines = splitLines(sink.data());
    ASSERT_EQ(lines.size(), 4u);
    EXPECT_EQ(lines[1].substr(0, 2), "0,");
    EXPECT_EQ(lines[2].substr(0, 4), "100,");
    EXPECT_EQ(lines[3].substr(0, 4), "300,");
}

TEST(ImuCsvRecorder, SinkFailureStopsTheRecordingAndIsReported)
{
    MemorySink     sink;
    ImuCsvRecorder rec;

    sink.failFromWrite(2);   // header write succeeds, the first drain fails

    ASSERT_TRUE(rec.begin(sink, 0));
    bool stopped = false;
    for (uint32_t i = 0; i < 2000 && !stopped; ++i) {
        stopped = !rec.onSample(i * 10, sample(static_cast<int16_t>(i)));
    }

    ASSERT_TRUE(stopped) << "a failing sink must stop the recording";
    EXPECT_EQ(rec.stopReason(), ImuCsvRecorder::Stop::SINK_ERROR);
    EXPECT_FALSE(rec.isRecording());
    // end() reports the failure rather than papering over it.
    EXPECT_FALSE(rec.end());
}

// end() answers "is there a usable recording?", so a cap stop (complete file,
// just short) must not be reported like a sink error (unusable file).
TEST(ImuCsvRecorder, CapStopStillReportsAUsableRecording)
{
    MemorySink     sink;
    ImuCsvRecorder rec;

    ImuCsvRecorder::Limits limits;
    limits.maxBytes      = 256;
    limits.maxDurationMs = 60u * 1000u;

    ASSERT_TRUE(rec.begin(sink, 0, limits));
    uint32_t t = 0;
    while (rec.onSample(t, sample(7))) {
        t += 10;
        ASSERT_LT(t, 100000u);
    }
    ASSERT_EQ(rec.stopReason(), ImuCsvRecorder::Stop::SIZE_LIMIT);
    EXPECT_TRUE(rec.end()) << "a capped recording is complete and usable";
}

TEST(ImuCsvRecorder, SamplesAreIgnoredWhenNotRecording)
{
    MemorySink     sink;
    ImuCsvRecorder rec;

    // Never begun.
    EXPECT_FALSE(rec.onSample(0, sample(1)));
    EXPECT_TRUE(rec.end());          // safe to call unconditionally
    EXPECT_TRUE(sink.data().empty());

    ASSERT_TRUE(rec.begin(sink, 0));
    EXPECT_TRUE(rec.onSample(0, sample(1)));
    ASSERT_TRUE(rec.end());
    const size_t sizeAtEnd = sink.data().size();

    // After end(), late samples must not reopen or extend the file.
    EXPECT_FALSE(rec.onSample(10, sample(2)));
    EXPECT_EQ(sink.data().size(), sizeAtEnd);
    EXPECT_EQ(rec.sampleCount(), 1u);
}

TEST(ImuCsvRecorder, BatchesAreBufferedRatherThanWrittenPerSample)
{
    MemorySink     sink;
    ImuCsvRecorder rec;

    ASSERT_TRUE(rec.begin(sink, 0));
    // One 10-sample sensor batch (~440 B) must fit the 1 KB staging buffer, so
    // it costs no write at all until the buffer fills or the run ends.
    for (uint32_t i = 0; i < 10; ++i) {
        ASSERT_TRUE(rec.onSample(i * 10, sample(static_cast<int16_t>(i))));
    }
    EXPECT_EQ(sink.writes(), 0u) << "a single batch should not reach the sink yet";

    ASSERT_TRUE(rec.end());
    EXPECT_EQ(sink.writes(), 1u);
    EXPECT_EQ(sink.flushes(), 1u);
    EXPECT_EQ(rec.bytesWritten(), sink.data().size());
}

// Sizing claim in the header: ~44 B/row typical, 53 B worst case, so 100 Hz is
// ~4.3 KiB/s. This pins the numbers the default caps were derived from, so a
// format change that breaks the budget fails here instead of on the watch.
TEST(ImuCsvRecorder, RowWidthMatchesTheDocumentedSizeBudget)
{
    MemorySink     sink;
    ImuCsvRecorder rec;
    ASSERT_TRUE(rec.begin(sink, 0));

    constexpr uint32_t kSamples = 1000;         // 10 s at 100 Hz
    for (uint32_t i = 0; i < kSamples; ++i) {
        ASSERT_TRUE(rec.onSample(i * 10, sample(static_cast<int16_t>(-2048 + (i % 4096)))));
    }
    ASSERT_TRUE(rec.end());

    const double bytesPerRow =
        static_cast<double>(sink.data().size() - (sizeof(kHeader))) / kSamples;
    EXPECT_GT(bytesPerRow, 20.0);
    EXPECT_LT(bytesPerRow, static_cast<double>(ImuCsvRecorder::skMaxRowBytes));

    // 30 min at 100 Hz must fit the default size cap -- the property the cap
    // was chosen for.
    const double thirtyMinBytes = bytesPerRow * 100.0 * 60.0 * 30.0;
    EXPECT_LT(thirtyMinBytes, static_cast<double>(ImuCsvRecorder::skDefaultMaxBytes))
        << "30 min at 100 Hz (" << thirtyMinBytes / 1e6
        << " MB) no longer fits skDefaultMaxBytes";
}
