#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string>

#include "ActivityCount.hpp"
#include "DayLog.hpp"
#include "Engine/EpochCounter.hpp"
#include "EpochRow.hpp"
#include "FakeFileSystem.hpp"
#include "LocalTime.hpp"
#include "Tilt.hpp"

using namespace Slumber;

namespace
{

constexpr float kPi = 3.14159265f;

/// 30 s of wrist movement with jittered timestamps, as the watch delivers.
template <typename F>
void feed(F &&add, uint32_t t0, float amp, float hz)
{
    uint32_t t = t0;
    for (int i = 0; i < 750; ++i) {
        t += 38 + (i * 7) % 5;
        const float s = static_cast<float>(t) * 0.001f;
        add(t, amp * std::sin(2 * kPi * hz * s), 0.2f + amp * std::cos(2 * kPi * hz * s),
            0.98f);
    }
}

size_t columns(const std::string &line)
{
    size_t n = 1;
    for (char c : line) {
        n += c == ',' ? 1 : 0;
    }
    return n;
}

std::string readAll(SDK::Test::FakeFileSystem &fs, const char *path)
{
    auto        f = fs.file(path);
    std::string out;
    if (f && f->open(false, false)) {
        out.resize(f->size());
        size_t got = 0;
        f->read(&out[0], out.size(), got);
        out.resize(got);
        f->close();
    }
    return out;
}

} // namespace

/// SleepPeriods' thresholds were measured in SleepLab's counts.
TEST(ActivityCount, MatchesSleepLabSampleForSample)
{
    for (float amp : {0.0f, 0.003f, 0.05f, 0.4f}) {
        ActivityCount        ours;
        Engine::EpochCounter theirs;
        for (int epoch = 0; epoch < 3; ++epoch) {
            feed([&](uint32_t t, float x, float y, float z) {
                ours.add(t, x, y, z);
                theirs.add(t, x, y, z);
            }, 1000 + epoch * 30000, amp, 1.0f);
            const ActivityCount::Out o = ours.close();
            uint32_t count = 0, peak = 0;
            uint16_t samples = 0;
            theirs.closeEpoch(count, peak, samples);
            EXPECT_EQ(o.count, count) << amp;
            EXPECT_EQ(o.peak, peak) << amp;
            EXPECT_EQ(o.samples, samples) << amp;
        }
    }
}

TEST(Tilt, AStillWristTurnsNowhere)
{
    Tilt t;
    feed([&](uint32_t ts, float, float, float) { t.add(ts, 0.0f, 0.2f, 0.98f); }, 0, 0, 1);
    const Tilt::Out o = t.close();
    EXPECT_TRUE(o.any);
    EXPECT_EQ(o.turnSumDegX10, 0);
    EXPECT_EQ(o.meanX1000[2], 980);
    EXPECT_GE(o.blocks, 5);
}

/// Face up to face sideways is a quarter turn, whichever axis carried gravity;
/// a roll inside one block splits across two steps, so only the sum is whole.
TEST(Tilt, ARollOverIsNinetyDegrees)
{
    Tilt     t;
    uint32_t ts = 0;
    for (; ts < 15000; ts += 40) {
        t.add(ts, 0.0f, 0.0f, 1.0f);
    }
    for (; ts < 30000; ts += 40) {
        t.add(ts, 1.0f, 0.0f, 0.0f);
    }
    const Tilt::Out o = t.close();
    EXPECT_GE(o.turnMaxDegX10, 450);
    EXPECT_NEAR(o.turnSumDegX10, 900, 5);
}

TEST(Tilt, ADataGapIsNotATurn)
{
    Tilt t;
    for (uint32_t ts = 0; ts < 10000; ts += 40) {
        t.add(ts, 0.0f, 0.0f, 1.0f);
    }
    for (uint32_t ts = 20000; ts < 30000; ts += 40) {
        t.add(ts, 1.0f, 0.0f, 0.0f);
    }
    EXPECT_EQ(t.close().turnSumDegX10, 0);
}

TEST(Tilt, NoSamplesSaysSo)
{
    Tilt t;
    EXPECT_FALSE(t.close().any);
}

TEST(EpochRow, EveryRowHasAColumnForEveryHeading)
{
    EpochRow r;
    char     buf[256];
    ASSERT_GT(r.format(buf, sizeof(buf)), 0u);
    EXPECT_EQ(columns(buf), columns(EpochRow::kHeader));
    EXPECT_EQ(buf[std::strlen(buf) - 1], '\n');
}

TEST(EpochRow, TheWidestRowFitsTheLogsRowBuffer)
{
    EpochRow r;
    r.uptimeMs = r.spanMs = r.lateMs = r.slotsLost = r.count = r.peak = r.writeFails = 4294967295u;
    r.countAxis[0] = r.countAxis[1] = r.countAxis[2] = 4294967295u;
    r.utc = 2147483647;
    r.utcOffMin = r.accMaxGapMs = r.steps = r.hrMeanX10 = r.hrN = r.battMah = -2147483647;
    r.gravX1000[0] = r.gravX1000[1] = r.gravX1000[2] = -32768;
    char buf[DayLog::kRowCap];
    EXPECT_GT(r.format(buf, sizeof(buf)), 0u);
}

TEST(EpochRow, AbsentIsMinusOneNotZero)
{
    char buf[256];
    EpochRow().format(buf, sizeof(buf));
    const std::string row(buf);
    EXPECT_NE(row.find(",-1,-1,-1,"), std::string::npos) << row;
}

TEST(LocalTime, CivilDaysRoundTrip)
{
    for (int32_t d : {-1, 0, 59, 60, 11016, 20735, 20736, 47482}) {
        int      y = 0;
        unsigned m = 0, day = 0;
        LocalTime::civilFromDays(d, y, m, day);
        EXPECT_EQ(LocalTime::daysFromCivil(y, m, day), d);
    }
}

/// 02:30 UTC on 10 Oct is still 9 Oct in Toronto.
TEST(LocalTime, TheStemIsTheLocalDay)
{
    const int64_t utc = 1791599400;  // 2026-10-10T02:30:00Z
    char stem[9];
    LocalTime::dayStem(utc, -240, stem);
    EXPECT_STREQ(stem, "20261009");
    LocalTime::dayStem(utc, 0, stem);
    EXPECT_STREQ(stem, "20261010");
    EXPECT_EQ(LocalTime::offsetMinutes(utc, 2026, 10, 9, 22, 30, 0), -240);
}

TEST(DayLog, BuffersThenAppendsUnderOneHeader)
{
    SDK::Test::FakeFileSystem fs;
    DayLog                    log(fs);
    EpochRow                  r;
    for (size_t i = 0; i < DayLog::kFlushRows - 1; ++i) {
        ASSERT_TRUE(log.add(r, "20261009"));
    }
    EXPECT_FALSE(fs.exist("Days/20261009.csv"));
    ASSERT_TRUE(log.add(r, "20261009"));
    ASSERT_TRUE(log.add(r, "20261009"));
    ASSERT_TRUE(log.flush());

    const std::string all = readAll(fs, "Days/20261009.csv");
    EXPECT_EQ(all.find(DayLog::kPreamble), 0u);
    EXPECT_EQ(all.find(EpochRow::kHeader), std::strlen(DayLog::kPreamble));
    EXPECT_EQ(all.find(EpochRow::kHeader, 1 + std::strlen(DayLog::kPreamble)), std::string::npos);
    size_t lines = 0;
    for (char c : all) {
        lines += c == '\n' ? 1 : 0;
    }
    EXPECT_EQ(lines, 2u + DayLog::kFlushRows + 1);
    EXPECT_EQ(log.rowsInFile(), DayLog::kFlushRows + 1);
}

TEST(DayLog, ANewDayFlushesTheOldOneFirst)
{
    SDK::Test::FakeFileSystem fs;
    DayLog                    log(fs);
    EpochRow                  r;
    log.add(r, "20261009");
    log.add(r, "20261010");
    EXPECT_TRUE(fs.exist("Days/20261009.csv"));
    EXPECT_EQ(log.rowsInFile(), 1u);
    log.flush();
    EXPECT_TRUE(fs.exist("Days/20261010.csv"));
}
