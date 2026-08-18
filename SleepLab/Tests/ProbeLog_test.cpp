/**
 * Host tests for the Tier 0 probe's on-disk record.
 *
 * The format is a contract between two pieces of code in two languages:
 * `Probe::Log` writes it on the watch, `Tools/probe_report.py` reads it on a
 * host. Testing the writer against this file's own idea of the format would
 * only pin the writer to itself, so `probe-log-export` (below, and built
 * alongside these tests) writes a real file with the real writer for the real
 * script to parse -- the same two-level check FwDump uses against
 * `reassemble_dump.py`.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "KernelTestDoubles.hpp"
#include "ProbeLog.hpp"

namespace {

using SDK::TestSupport::KernelFixture;

/// Split a written log into lines, dropping the trailing empty one.
std::vector<std::string> lines(const std::string &blob)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start < blob.size()) {
        const size_t nl = blob.find('\n', start);
        if (nl == std::string::npos) {
            out.push_back(blob.substr(start));
            break;
        }
        out.push_back(blob.substr(start, nl - start));
        start = nl + 1;
    }
    return out;
}

/// Comma-separated fields of one line.
std::vector<std::string> fields(const std::string &line)
{
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t c = line.find(',', start);
        if (c == std::string::npos) {
            out.push_back(line.substr(start));
            return out;
        }
        out.push_back(line.substr(start, c - start));
        start = c + 1;
    }
}

TEST(ProbeLog, HeaderIsWrittenOnceForANewFile)
{
    KernelFixture fx;
    Probe::Log log(fx.kernel);

    ASSERT_TRUE(log.begin(1000, 1755000000, "continuous"));

    auto ls = lines(fx.fileSystem.readFile(Probe::kLogPath));
    ASSERT_EQ(ls.size(), 2u);
    EXPECT_EQ(ls[0].rfind("H,schema,1,cols,", 0), 0u);
    EXPECT_EQ(ls[1].rfind("R,1000,1755000000,", 0), 0u);
}

TEST(ProbeLog, SecondLaunchAppendsAnRRowAndNoSecondHeader)
{
    KernelFixture fx;
    {
        Probe::Log log(fx.kernel);
        ASSERT_TRUE(log.begin(1000, 1755000000, "continuous"));
    }
    {
        // A separate Log, as a relaunch really is a separate process.
        Probe::Log log(fx.kernel);
        ASSERT_TRUE(log.begin(400000, 1755000400, "off"));
    }

    auto ls = lines(fx.fileSystem.readFile(Probe::kLogPath));
    ASSERT_EQ(ls.size(), 3u);
    EXPECT_EQ(ls[0].rfind("H,", 0), 0u);
    EXPECT_EQ(ls[1].rfind("R,1000,", 0), 0u);
    EXPECT_EQ(ls[2].rfind("R,400000,", 0), 0u);
    // The mode this launch ran under travels with the launch, so a night's
    // rows can never be attributed to the wrong experiment.
    EXPECT_NE(ls[2].find("hr=off"), std::string::npos);
}

TEST(ProbeLog, RowHasExactlyAsManyFieldsAsTheHeaderNames)
{
    KernelFixture fx;
    Probe::Log log(fx.kernel);
    ASSERT_TRUE(log.begin(0, 1, "continuous"));

    Probe::MinuteRow r;
    r.uptimeMs = 60000;
    ASSERT_TRUE(log.write(r));

    auto ls = lines(fx.fileSystem.readFile(Probe::kLogPath));
    ASSERT_EQ(ls.size(), 3u);

    // The header is "H,schema,1,cols," followed by the column names, so its
    // field count is four more than a row's. Asserting the relationship rather
    // than two magic numbers is what makes this catch a column added to one
    // and not the other.
    const auto header = fields(ls[0]);
    const auto row    = fields(ls[2]);
    ASSERT_GT(header.size(), 4u);
    EXPECT_EQ(header.size() - 4u, row.size());
    EXPECT_EQ(header[4], "kind");
    EXPECT_EQ(row[0], "M");
}

TEST(ProbeLog, ADefaultRowIsAllSentinelsRatherThanAllZeroes)
{
    // The distinction between "never subscribed" and "delivered nothing" is
    // the one the whole log turns on, so a row nobody filled in must not read
    // as a row where every sensor reported zero.
    KernelFixture fx;
    Probe::Log log(fx.kernel);
    ASSERT_TRUE(log.begin(0, 1, "off"));

    Probe::MinuteRow r;
    r.uptimeMs = 60000;
    r.spanMs   = 60000;
    ASSERT_TRUE(log.write(r));

    const auto row = fields(lines(fx.fileSystem.readFile(Probe::kLogPath))[2]);
    // acc_n is field 5 (kind, uptime, wall, local_min, span, acc_n).
    EXPECT_EQ(row[5], "-1");
    // wall_utc defaults to 0 and local_min to -1 -- an unset clock is
    // reported, not silently stamped as the epoch.
    EXPECT_EQ(row[3], "-1");
}

TEST(ProbeLog, NegativeAndLargeValuesRoundTripAsWritten)
{
    KernelFixture fx;
    Probe::Log log(fx.kernel);
    ASSERT_TRUE(log.begin(0, 1, "duty"));

    Probe::MinuteRow r;
    r.uptimeMs      = 4294967295u;   // one tick before the uptime wrap
    r.wallUtc       = 1755000000;
    r.localMin      = 1439;          // 23:59
    r.spanMs        = 61234;
    r.accN          = 1500;
    r.battAvgMaX10  = -1234;         // discharging, if the sign means that
    r.stepTotal     = 9876543210LL;  // monotonic since boot, easily past 2^32
    ASSERT_TRUE(log.write(r));

    const auto row = fields(lines(fx.fileSystem.readFile(Probe::kLogPath))[2]);
    EXPECT_EQ(row[1], "4294967295");
    EXPECT_EQ(row[3], "1439");
    EXPECT_EQ(row[5], "1500");
    // step_total is field 34.
    EXPECT_EQ(row[34], "9876543210");
    // batt_avg_ma_x10 is field 41.
    EXPECT_EQ(row[41], "-1234");
}

TEST(ProbeLog, EveryRowIsFlushedAndNoHandleIsLeftOpen)
{
    // The writer is interrupted by a USB connection that kills the process
    // without warning, so a row still sitting in a cache is a row that never
    // happened -- and a leaked handle is a FatFs lock slot gone for good.
    KernelFixture fx;
    Probe::Log log(fx.kernel);
    ASSERT_TRUE(log.begin(0, 1, "continuous"));

    Probe::MinuteRow r;
    for (int i = 0; i < 10; i++) {
        r.uptimeMs = static_cast<uint32_t>(60000 * (i + 1));
        ASSERT_TRUE(log.write(r));
    }

    EXPECT_EQ(fx.fileSystem.openHandles[Probe::kLogPath], 0u);
    // begin() writes twice (header + R row), then ten rows.
    EXPECT_EQ(fx.fileSystem.flushCounts[Probe::kLogPath], 12u);
    EXPECT_EQ(log.failures(), 0u);
}

} // namespace
