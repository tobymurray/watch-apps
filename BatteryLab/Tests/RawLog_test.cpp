#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "FakeFileSystem.hpp"
#include "RawLine.hpp"
#include "RawLog.hpp"

using namespace BatteryLab;

namespace
{

std::string readAll(SDK::Test::FakeFileSystem &fs, const char *path)
{
    auto        f = fs.file(path);
    std::string out;
    if (!f || !f->open(false, false)) {
        return out;
    }
    char   buf[256];
    size_t got = 0;
    while (f->read(buf, sizeof(buf), got) && got > 0) {
        out.append(buf, got);
    }
    f->close();
    return out;
}

Sample widest()
{
    Sample s;
    s.uptimeMs = s.sensorTsMs = s.utc = UINT32_MAX;
    s.kind = Kind::Metrics;
    s.utcOffMin = s.fields = s.pctX1000 = s.mv = s.maX1000 = s.avgMaX1000 = s.mahX1000 =
        s.designMahX1000 = s.connected = s.charging = INT32_MIN + 1;
    return s;
}

} // namespace

TEST(RawLine, TheWidestRowFitsItsCap)
{
    char         buf[kLineCap];
    const size_t n = format(widest(), buf, sizeof(buf));
    ASSERT_GT(n, 0u);
    EXPECT_EQ(n + 1, sizeof(buf));
    EXPECT_EQ(buf[n - 1], '\n');
}

TEST(RawLine, OneByteShortWritesNothing)
{
    char buf[kLineCap - 1];
    EXPECT_EQ(format(widest(), buf, sizeof(buf)), 0u);
}

TEST(RawLine, AMetricsRowHasEveryColumnTheHeaderNames)
{
    Sample s;
    s.uptimeMs       = 39048;
    s.sensorTsMs     = 39001;
    s.utc            = 1791642771;
    s.utcOffMin      = -240;
    s.kind           = Kind::Metrics;
    s.fields         = 5;
    s.mv             = 4379;
    s.maX1000        = -1400;
    s.avgMaX1000     = -1512;
    s.mahX1000       = 204000;
    s.designMahX1000 = 148000;
    char buf[kLineCap];
    ASSERT_GT(format(s, buf, sizeof(buf)), 0u);
    EXPECT_STREQ(buf, "39048,39001,1791642771,-240,E,5,,4379,-1400,-1512,204000,148000,,\n");

    const char *columns = std::strchr(kHeader, '\n') + 1;
    auto commas = [](const char *p) { return std::count(p, p + std::strlen(p), ','); };
    EXPECT_EQ(commas(buf), commas(columns));
}

TEST(RawLine, ScalingTruncatesAndRefusesWhatAnIntCannotHold)
{
    EXPECT_EQ(scaled(4.3789f, 1000.0f), 4378);
    EXPECT_EQ(scaled(-1.4f, 1000.0f), -1400);
    EXPECT_EQ(scaled(3.0e6f, 1000.0f), kNone);
    EXPECT_EQ(scaled(std::nanf(""), 1000.0f), kNone);
}

TEST(RawLog, WritesTheHeaderOnceAndAppendsAcrossFlushes)
{
    SDK::Test::FakeFileSystem fs;
    RawLog                    log(fs);
    Sample                    s;
    s.kind = Kind::Boot;
    ASSERT_TRUE(log.add(s, "20261015"));
    ASSERT_TRUE(log.flush());
    ASSERT_TRUE(log.add(s, "20261015"));
    ASSERT_TRUE(log.flush());

    const std::string got = readAll(fs, "Raw/20261015.csv");
    EXPECT_EQ(got.find(kHeader), 0u);
    EXPECT_EQ(got.find(kHeader, 1), std::string::npos);
    EXPECT_EQ(std::count(got.begin(), got.end(), '\n'), 2 + 2);
    EXPECT_EQ(log.lines(), 2u);
}

TEST(RawLog, ANewDayFlushesTheOldOneToItsOwnFile)
{
    SDK::Test::FakeFileSystem fs;
    RawLog                    log(fs);
    Sample                    s;
    ASSERT_TRUE(log.add(s, "20261015"));
    ASSERT_TRUE(log.add(s, "20261016"));
    ASSERT_TRUE(log.flush());
    const std::string first = readAll(fs, "Raw/20261015.csv");
    EXPECT_EQ(std::count(first.begin(), first.end(), '\n'), 2 + 1);
    EXPECT_NE(readAll(fs, "Raw/20261016.csv").find(",B,"), std::string::npos);
}

TEST(RawLog, AFullBufferFlushesWithoutBeingAsked)
{
    SDK::Test::FakeFileSystem fs;
    RawLog                    log(fs);
    const Sample              s = widest();
    for (size_t i = 0; i < RawLog::kFlushLines; ++i) {
        ASSERT_TRUE(log.add(s, "20261015"));
    }
    const std::string got = readAll(fs, "Raw/20261015.csv");
    EXPECT_EQ(static_cast<size_t>(std::count(got.begin(), got.end(), '\n')), 2 + RawLog::kFlushLines);
}
