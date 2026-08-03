/**
 ******************************************************************************
 * @file    ImuFileSink_test.cpp
 * @brief   ImuCsvRecorder -> ImuFileSink -> ImuFusionSource round trip.
 ******************************************************************************
 *
 * ImuCsvRecorder's own tests cover formatting and the caps against a memory
 * buffer. What they cannot show is that the bytes reach storage through the
 * SDK file system, and that what lands there is readable by the playback
 * parser the whole development loop depends on. That round trip is the point
 * of the recorder, so it is tested rather than assumed.
 */

#include "ImuCsvRecorder.hpp"
#include "ImuFileSink.hpp"
#include "KernelTestDoubles.hpp"

#include "SDK/Simulator/Components/Sensors/IMU/ImuFusionSource.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {

/// 2026-08-03 10:00:00 UTC. Only the file name derives from this.
constexpr std::time_t kUtc = 1785751200;

/// Path of the single .csv the sink created, or empty.
std::string findCsvPath(const SDK::TestSupport::InMemoryFileSystem& fs)
{
    for (const auto& kv : fs.files) {
        const std::string& path = kv.first;
        if (kv.second.exists && path.size() > 4
            && path.compare(path.size() - 4, 4, ".csv") == 0) {
            return path;
        }
    }
    return {};
}

/// A recognisable ramp, so a mis-ordered or dropped field is obvious.
ImuCsvRecorder::Sample makeSample(int16_t i)
{
    ImuCsvRecorder::Sample s{};
    s.ax = static_cast<int16_t>(100 + i);
    s.ay = static_cast<int16_t>(-200 - i);
    s.az = static_cast<int16_t>(4096 + i);
    s.gx = static_cast<int16_t>(300 + i);
    s.gy = static_cast<int16_t>(-400 - i);
    s.gz = static_cast<int16_t>(500 + i);
    return s;
}

class ImuFileSinkTest : public ::testing::Test {
protected:
    SDK::TestSupport::KernelFixture fixture;
};

TEST_F(ImuFileSinkTest, CreateOpensAFileUnderTheGivenDirectory)
{
    ImuFileSink sink(fixture.kernel, "Imu");

    ASSERT_TRUE(sink.create(kUtc));
    EXPECT_TRUE(sink.isOpen());
    ASSERT_NE(sink.path(), nullptr);

    const std::string path = sink.path();
    EXPECT_EQ(path.rfind("Imu/", 0), 0u) << "path was " << path;
    EXPECT_NE(path.find(".csv"), std::string::npos) << "path was " << path;

    EXPECT_TRUE(sink.close());
    EXPECT_FALSE(sink.isOpen());
}

TEST_F(ImuFileSinkTest, WriteFailsWhenNoFileIsOpen)
{
    ImuFileSink sink(fixture.kernel, "Imu");

    // The recorder calls the sink only while running, but a sink that silently
    // accepted writes with no file would lose a whole session.
    EXPECT_FALSE(sink.write("x", 1));
    EXPECT_FALSE(sink.flush());
}

TEST_F(ImuFileSinkTest, RecordedSamplesReplayThroughImuFusionSource)
{
    ImuFileSink    sink(fixture.kernel, "Imu");
    ImuCsvRecorder recorder;

    ASSERT_TRUE(sink.create(kUtc));

    constexpr uint32_t kStartMs = 5000;
    constexpr int16_t  kCount   = 25;

    ASSERT_TRUE(recorder.begin(sink, kStartMs));

    for (int16_t i = 0; i < kCount; ++i) {
        // 10 ms apart: the 100 Hz cadence the recorder is fed on device.
        ASSERT_TRUE(recorder.onSample(kStartMs + static_cast<uint32_t>(i) * 10u,
                                      makeSample(i)))
            << "sample " << i << " rejected";
    }

    EXPECT_TRUE(recorder.end());
    EXPECT_TRUE(sink.close());
    EXPECT_EQ(recorder.sampleCount(), static_cast<uint32_t>(kCount));

    const std::string path = findCsvPath(fixture.fileSystem);
    ASSERT_FALSE(path.empty()) << "sink wrote no .csv";

    const std::string csv = fixture.fileSystem.readFile(path);
    ASSERT_FALSE(csv.empty()) << "recording is empty";

    // The contract that matters: the playback parser accepts it as-is.
    std::istringstream in(csv);
    std::vector<Sensor::ImuFusionSource::Row> rows;
    std::string error;
    ASSERT_TRUE(Sensor::ImuFusionSource::loadCsv(in, rows, error)) << error;

    ASSERT_EQ(rows.size(), static_cast<size_t>(kCount));

    // First row is the origin, and values survive the trip in raw LSB.
    EXPECT_EQ(rows[0].offsetUs, 0u);
    EXPECT_EQ(rows[0].sample.ax, makeSample(0).ax);
    EXPECT_EQ(rows[0].sample.az, makeSample(0).az);
    EXPECT_EQ(rows[0].sample.gz, makeSample(0).gz);

    const auto& last = rows[rows.size() - 1];
    EXPECT_EQ(last.offsetUs, static_cast<uint64_t>(kCount - 1) * 10000u);
    EXPECT_EQ(last.sample.ay, makeSample(kCount - 1).ay);
    EXPECT_EQ(last.sample.gy, makeSample(kCount - 1).gy);
}

TEST_F(ImuFileSinkTest, CreateTwiceStartsAFreshFileWithoutLeakingTheFirst)
{
    ImuFileSink    sink(fixture.kernel, "Imu");
    ImuCsvRecorder recorder;

    ASSERT_TRUE(sink.create(kUtc));
    ASSERT_TRUE(recorder.begin(sink, 0));
    ASSERT_TRUE(recorder.onSample(10, makeSample(1)));
    ASSERT_TRUE(recorder.end());

    // A caller that skipped close() must not strand the previous handle.
    ASSERT_TRUE(sink.create(kUtc));
    EXPECT_TRUE(sink.isOpen());
    EXPECT_TRUE(sink.close());
}

} // namespace
