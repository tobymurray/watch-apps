#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ActivityCount.hpp"
#include "DayLog.hpp"
#include "Engine/EpochCounter.hpp"
#include "EpochHistory.hpp"
#include "EpochRow.hpp"
#include "FakeFileSystem.hpp"
#include "LocalTime.hpp"
#include "NightLog.hpp"
#include "SleepNow.hpp"
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

/// A day file an older schema began is set aside, not appended to.
TEST(DayLog, AnOlderSchemasFileIsSetAside)
{
    SDK::Test::FakeFileSystem fs;
    fs.mkdir(DayLog::kDir);
    const std::string old = "# Slumber epoch log, schema 1\nuptime_ms,utc\n1,2\n";
    {
        auto   f = fs.file("Days/20261010.csv");
        size_t w = 0;
        ASSERT_TRUE(f->open(true, true));
        ASSERT_TRUE(f->write(old.data(), old.size(), w));
        f->close();
    }
    DayLog log(fs);
    ASSERT_TRUE(log.add(EpochRow(), "20261010"));
    ASSERT_TRUE(log.flush());

    EXPECT_EQ(readAll(fs, "Days/20261010-1.csv"), old);
    EXPECT_EQ(readAll(fs, "Days/20261010.csv").find(DayLog::kPreamble), 0u);

    ASSERT_TRUE(log.add(EpochRow(), "20261010"));
    ASSERT_TRUE(log.flush());
    EXPECT_FALSE(fs.exist("Days/20261010-2.csv"));
}

TEST(LocalTime, MinuteOfDayIsLocal)
{
    const int64_t utc = 1791599400;  // 2026-10-10T02:30:00Z
    EXPECT_EQ(LocalTime::minuteOfDay(utc, -240), 22 * 60 + 30);
    EXPECT_EQ(LocalTime::minuteOfDay(utc, 0), 2 * 60 + 30);
}

namespace
{

SleepPeriods::Epoch epochAt(uint32_t utc)
{
    SleepPeriods::Epoch e;
    e.utc        = utc;
    e.count      = utc % 5000;
    e.steps      = 513;
    e.hrTrustX10 = 27;
    e.hasHr      = true;
    e.hrAsked    = true;
    return e;
}

/// 20261009 00:00 in Toronto.
constexpr uint32_t kMidnight = 1791518400u;  // 2026-10-09T04:00:00Z

} // namespace

TEST(EpochHistory, ARecordRoundTrips)
{
    uint8_t buf[EpochHistory::kRecSize];
    const SleepPeriods::Epoch in = epochAt(1791599400);
    EpochHistory::encode(in, buf);
    const SleepPeriods::Epoch out = EpochHistory::decode(buf);
    EXPECT_EQ(out.utc, in.utc);
    EXPECT_EQ(out.count, in.count);
    EXPECT_EQ(out.steps, in.steps);
    EXPECT_EQ(out.hrTrustX10, in.hrTrustX10);
    EXPECT_TRUE(out.hasHr);
    EXPECT_TRUE(out.hrAsked);
}

/// A restart mid-morning finds the night that crossed midnight, in order.
TEST(EpochHistory, ARestartRestoresTheWindowAcrossDays)
{
    SDK::Test::FakeFileSystem fs;
    {
        auto h = std::make_unique<EpochHistory>(fs);
        char stem[9];
        for (uint32_t t = kMidnight - 6 * 3600; t < kMidnight + 9 * 3600; t += 30) {
            LocalTime::dayStem(t, -240, stem);
            ASSERT_TRUE(h->add(epochAt(t), stem));
        }
        ASSERT_TRUE(h->flush());
    }
    EXPECT_TRUE(fs.exist("Epochs/20261008.bin"));
    EXPECT_TRUE(fs.exist("Epochs/20261009.bin"));

    auto h = std::make_unique<EpochHistory>(fs);
    h->restore(kMidnight + 9 * 3600, -240);
    ASSERT_EQ(h->size(), 15u * 120u);
    EXPECT_EQ(h->data()[0].utc, kMidnight - 6 * 3600);
    for (size_t i = 1; i < h->size(); ++i) {
        ASSERT_LT(h->data()[i - 1].utc, h->data()[i].utc);
    }
}

TEST(EpochHistory, OnlyTheWindowIsKept)
{
    SDK::Test::FakeFileSystem fs;
    auto h = std::make_unique<EpochHistory>(fs);
    for (uint32_t t = kMidnight; t < kMidnight + EpochHistory::kWindowS + 3600; t += 30) {
        h->add(epochAt(t), "20261009");
    }
    EXPECT_EQ(h->size(), EpochHistory::kCap);
    EXPECT_EQ(h->data()[0].utc, kMidnight + 3600);
}

TEST(EpochHistory, AClockSetBackStartsAgain)
{
    SDK::Test::FakeFileSystem fs;
    auto h = std::make_unique<EpochHistory>(fs);
    h->add(epochAt(kMidnight), "20261009");
    h->add(epochAt(kMidnight + 30), "20261009");
    h->add(epochAt(kMidnight - 3600), "20261008");
    ASSERT_EQ(h->size(), 1u);
    EXPECT_EQ(h->data()[0].utc, kMidnight - 3600);
}

TEST(EpochHistory, AFileUnderAnotherHeaderIsReplaced)
{
    SDK::Test::FakeFileSystem fs;
    fs.mkdir(EpochHistory::kDir);
    {
        auto   f = fs.file("Epochs/20261009.bin");
        size_t w = 0;
        ASSERT_TRUE(f->open(true, true));
        ASSERT_TRUE(f->write("old\n0123456789AB", 16, w));
        f->close();
    }
    auto h = std::make_unique<EpochHistory>(fs);
    for (size_t i = 0; i < EpochHistory::kFlushRecs; ++i) {
        ASSERT_TRUE(h->add(epochAt(kMidnight + 30 * i), "20261009"));
    }
    h->restore(kMidnight + 3600, -240);
    EXPECT_EQ(h->size(), EpochHistory::kFlushRecs);
}

namespace
{

EpochRow rowClosingAt(int64_t utc)
{
    EpochRow r;
    r.utc        = utc;
    r.count      = static_cast<uint32_t>(utc % 5000);
    r.steps      = static_cast<int32_t>(utc % 3);
    r.hrN        = static_cast<int32_t>(utc % 4) - 1;
    r.hrTrustX10 = 27;
    return r;
}

/// The day files a watch upgraded from a build without `Epochs/` holds.
void writeDays(SDK::Test::FakeFileSystem &fs, uint32_t from, uint32_t to)
{
    auto log = std::make_unique<DayLog>(fs);
    char stem[9];
    for (uint32_t t = from; t < to; t += 30) {
        LocalTime::dayStem(t, -240, stem);
        ASSERT_TRUE(log->add(rowClosingAt(t), stem));
    }
    ASSERT_TRUE(log->flush());
}

} // namespace

TEST(EpochHistory, AMissingCacheIsRebuiltFromTheDayFiles)
{
    SDK::Test::FakeFileSystem fs;
    writeDays(fs, kMidnight - 6 * 3600 + 30, kMidnight + 9 * 3600 + 30);

    auto h = std::make_unique<EpochHistory>(fs);
    EXPECT_EQ(h->restore(kMidnight + 9 * 3600, -240), 2u);
    ASSERT_EQ(h->size(), 15u * 120u);
    for (size_t i = 0; i < h->size(); ++i) {
        const EpochRow            r    = rowClosingAt(kMidnight - 6 * 3600 + 30 * (i + 1));
        const SleepPeriods::Epoch want = DayRow::toEpoch(r.utc, r.count, r.steps, r.hrN, r.hrTrustX10);
        const SleepPeriods::Epoch got  = h->data()[i];
        ASSERT_EQ(got.utc, want.utc);
        ASSERT_EQ(got.count, want.count);
        ASSERT_EQ(got.steps, want.steps);
        ASSERT_EQ(got.hasHr, want.hasHr);
        ASSERT_EQ(got.hrAsked, want.hrAsked);
        ASSERT_EQ(got.hrTrustX10, want.hrTrustX10);
    }
}

/// The rebuilt cache is written, so the next restart does not read the day files.
TEST(EpochHistory, ARebuiltCacheIsKept)
{
    SDK::Test::FakeFileSystem fs;
    writeDays(fs, kMidnight - 6 * 3600 + 30, kMidnight + 9 * 3600 + 30);
    std::make_unique<EpochHistory>(fs)->restore(kMidnight + 9 * 3600, -240);
    fs.remove("Days/20261008.csv");
    fs.remove("Days/20261009.csv");

    auto h = std::make_unique<EpochHistory>(fs);
    EXPECT_EQ(h->restore(kMidnight + 9 * 3600, -240), 0u);
    EXPECT_EQ(h->size(), 15u * 120u);
}

TEST(EpochHistory, ADayFileUnderAnotherSchemaIsNotRead)
{
    SDK::Test::FakeFileSystem fs;
    fs.mkdir(DayLog::kDir);
    {
        const std::string text = std::string("# Slumber epoch log, schema 1\n") + EpochRow::kHeader +
                                 "1,1791518430,-240\n";
        auto   f = fs.file("Days/20261009.csv");
        size_t w = 0;
        ASSERT_TRUE(f->open(true, true));
        ASSERT_TRUE(f->write(text.data(), text.size(), w));
        f->close();
    }
    auto h = std::make_unique<EpochHistory>(fs);
    EXPECT_EQ(h->restore(kMidnight + 3600, -240), 0u);
    EXPECT_EQ(h->size(), 0u);
    EXPECT_FALSE(fs.exist("Epochs/20261009.bin"));
}

/// A crash can leave the last row half-written; the rows before it still count.
TEST(EpochHistory, AHalfWrittenRowIsSkipped)
{
    SDK::Test::FakeFileSystem fs;
    writeDays(fs, kMidnight + 30, kMidnight + 3600 + 30);
    {
        auto   f    = fs.file("Days/20261009.csv");
        size_t w    = 0;
        const char cut[] = "3600000,1791522030,-240,30000";
        ASSERT_TRUE(f->open(true, false));
        ASSERT_TRUE(f->seek(f->size()));
        ASSERT_TRUE(f->write(cut, sizeof(cut) - 1, w));
        f->close();
    }
    auto h = std::make_unique<EpochHistory>(fs);
    h->restore(kMidnight + 2 * 3600, -240);
    EXPECT_EQ(h->size(), 120u);
}

TEST(LocalTime, AStampIsLocal)
{
    char out[17];
    LocalTime::stamp(1791599400, -240, out);  // 2026-10-10T02:30:00Z
    EXPECT_STREQ(out, "2026-10-09 22:30");
}

namespace
{

/// 2026-10-08 23:54 to 2026-10-09 09:05 in Toronto.
SleepPeriods::Period settledNight()
{
    SleepPeriods::Period p;
    p.startUtc   = kMidnight - 6 * 60;
    p.endUtc     = kMidnight + 9 * 3600 + 5 * 60;
    p.restMin    = 551;
    p.wornPct    = 100;
    p.hrCoverPct = 83;
    p.worn       = SleepPeriods::Worn::Worn;
    p.settled    = true;
    return p;
}

} // namespace

/// One epoch every 30 s across `p`, moving at `moveAt` slots.
std::vector<SleepPeriods::Epoch> epochsOf(const SleepPeriods::Period &p, std::vector<size_t> moveAt = {})
{
    std::vector<SleepPeriods::Epoch> e;
    for (uint32_t t = p.startUtc; t < p.endUtc; t += SleepPeriods::kEpochS) {
        SleepPeriods::Epoch x;
        x.utc   = t;
        x.count = 200;
        e.push_back(x);
    }
    for (size_t k : moveAt) {
        e[k].count = 5000;
    }
    return e;
}

bool logNight(NightLog &log, const SleepPeriods::Period &p, NightSummary::Summary s = {},
              std::vector<size_t> moveAt = {})
{
    const auto e = epochsOf(p, moveAt);
    return log.add(p, s, -240, e.data(), e.size());
}

TEST(NightLog, ASettledNightIsLoggedOnceAsOneJsonLine)
{
    SDK::Test::FakeFileSystem fs;
    auto                      log = std::make_unique<NightLog>(fs);
    NightSummary::Summary     s;
    s.restlessSpells = 15;
    s.longestRestMin = 240;
    const SleepPeriods::Period p = settledNight();
    ASSERT_TRUE(log->due(p));
    ASSERT_TRUE(logNight(*log, p, s, {1}));
    EXPECT_FALSE(log->due(p));

    const std::string all = readAll(fs, NightLog::kPath);
    const std::string head =
        "{\"schema\":1,\"source\":\"Slumber\",\"start\":1791518040,\"end\":1791551100,"
        "\"utc_off_min\":-240,\"rest_min\":551,\"worn\":\"worn\",\"start_local\":\"2026-10-08 23:54\","
        "\"end_local\":\"2026-10-09 09:05\",\"span_min\":551,\"missing_min\":0,\"longest_rest_min\":240,"
        "\"restless_spells\":15,\"worn_pct\":100,\"hr_cover_pct\":83,\"epoch_s\":30,\"epochs\":\"";
    ASSERT_EQ(all.find(head), 0u);
    const std::string strip = all.substr(head.size(), all.size() - head.size() - 3);
    EXPECT_EQ(strip.size(), 551u * 2u);
    EXPECT_EQ(strip.substr(0, 3), "rmr");
    EXPECT_EQ(all.substr(all.size() - 3), "\"}\n");
}

TEST(NightLog, ARestartDoesNotLogItAgain)
{
    SDK::Test::FakeFileSystem fs;
    const SleepPeriods::Period p = settledNight();
    {
        auto log = std::make_unique<NightLog>(fs);
        ASSERT_TRUE(log->due(p));
        ASSERT_TRUE(logNight(*log, p));
    }
    auto log = std::make_unique<NightLog>(fs);
    EXPECT_FALSE(log->due(p));
    SleepPeriods::Period next = p;
    next.startUtc += 86400;
    next.endUtc += 86400;
    EXPECT_TRUE(log->due(next));
}

TEST(NightLog, AnotherSourcesLinesAreNotItsOwn)
{
    SDK::Test::FakeFileSystem fs;
    fs.seedFile(NightLog::kPath, "{\"schema\":1,\"source\":\"Other\",\"start\":1791518040,"
                                 "\"end\":1791551100,\"utc_off_min\":-240,\"rest_min\":551,\"worn\":\"worn\"}\n");
    auto log = std::make_unique<NightLog>(fs);
    EXPECT_TRUE(log->due(settledNight()));
    EXPECT_EQ(log->recent().size(), 0u);
}

TEST(NightLog, ATornLineIsClosedOffBeforeTheNext)
{
    SDK::Test::FakeFileSystem fs;
    fs.seedFile(NightLog::kPath, "{\"schema\":1,\"source\":\"Slumber\",\"start\":17915");
    auto log = std::make_unique<NightLog>(fs);
    ASSERT_TRUE(log->due(settledNight()));
    ASSERT_TRUE(logNight(*log, settledNight()));
    const std::string all = readAll(fs, NightLog::kPath);
    const size_t      nl  = all.find('\n');
    EXPECT_EQ(all.substr(0, nl), "{\"schema\":1,\"source\":\"Slumber\",\"start\":17915");
    EXPECT_EQ(all.find("{\"schema\"", nl), nl + 1);
}

TEST(NightLog, MainNightsAreReadBackForTheUsual)
{
    SDK::Test::FakeFileSystem fs;
    {
        auto log = std::make_unique<NightLog>(fs);
        for (int d = 0; d < 4; ++d) {
            SleepPeriods::Period p = settledNight();
            p.startUtc += d * 86400;
            p.endUtc += d * 86400;
            ASSERT_TRUE(log->due(p));
            ASSERT_TRUE(logNight(*log, p));
        }
    }
    auto                log = std::make_unique<NightLog>(fs);
    const Rhythm::Usual u   = log->recent().usualBefore(UINT32_MAX);
    ASSERT_TRUE(u.known);
    EXPECT_EQ(u.nights, 4u);
    EXPECT_EQ(Rhythm::minuteOfDay(u.bed), 23 * 60 + 54);
    EXPECT_EQ(Rhythm::minuteOfDay(u.wake), 9 * 60 + 5);
}

TEST(NightLog, MainNightsAreReadBackForTheWeek)
{
    SDK::Test::FakeFileSystem fs;
    {
        auto log = std::make_unique<NightLog>(fs);
        for (int d = 0; d < 4; ++d) {
            SleepPeriods::Period p = settledNight();
            p.startUtc += d * 86400;
            p.endUtc += d * 86400;
            ASSERT_TRUE(logNight(*log, p));
        }
    }
    auto log = std::make_unique<NightLog>(fs);
    EXPECT_EQ(log->schedule().size(), 4u);
    const Schedule::Expected e = log->schedule().expect(Rhythm::noonDay(settledNight().startUtc, -240) + 4);
    ASSERT_TRUE(e.known);
    EXPECT_EQ(Rhythm::minuteOfDay(e.bed), 23 * 60 + 54);
}

TEST(NightLog, AnUnsettledNightWaits)
{
    SDK::Test::FakeFileSystem fs;
    auto                      log = std::make_unique<NightLog>(fs);
    SleepPeriods::Period      p   = settledNight();
    p.settled = false;
    EXPECT_FALSE(log->due(p));
    EXPECT_FALSE(fs.exist(NightLog::kPath));
}

TEST(SleepNow, BeforeAnyNightEveryAnswerIsNull)
{
    SleepNow::State s;
    s.writtenUtc = 1791600000;
    s.heartbeatS = 300;
    s.offsetMin  = -240;
    char out[SleepNow::kCap];
    ASSERT_GT(SleepNow::format(s, out, sizeof(out)), 0u);
    EXPECT_STREQ(out, "{\"schema\":1,\"source\":\"Slumber\",\"written\":1791600000,\"heartbeat_s\":300,"
                      "\"utc_off_min\":-240,\"asleep_since\":null,\"last_night\":null,\"usual\":null,"
                      "\"tonight\":null,\"week\":{\"later_bed\":[],\"earlier_bed\":[],\"later_wake\":[],"
                      "\"earlier_wake\":[],\"social_jetlag_min\":null},"
                      "\"rest_goal_min\":420,\"rest_fill\":null,\"rhythm_fill\":null,\"rhythm_offset_min\":null}");
}

TEST(SleepNow, ANightIsScoredAgainstTheNightsBeforeIt)
{
    const SleepPeriods::Period p = settledNight();
    SleepNow::State            s;
    s.writtenUtc     = p.endUtc + 3600;
    s.heartbeatS     = 300;
    s.offsetMin      = -240;
    s.lastNight      = &p;
    s.usualBefore.known  = true;
    s.usualBefore.bed    = Rhythm::afterNoon(p.startUtc, -240) - 50;
    s.usualBefore.nights = 3;
    s.usual              = s.usualBefore;
    char out[SleepNow::kCap];
    ASSERT_GT(SleepNow::format(s, out, sizeof(out)), 0u);
    const std::string j = out;
    EXPECT_NE(j.find("\"last_night\":{\"start\":1791518040,\"end\":1791551100,\"rest_min\":551,"
                     "\"worn\":\"worn\",\"settled\":true}"), std::string::npos);
    EXPECT_NE(j.find("\"usual\":{\"bed_min\":1384,\"wake_min\":720,\"nights\":3}"), std::string::npos);
    EXPECT_NE(j.find("\"rest_fill\":1311,"), std::string::npos);
    EXPECT_NE(j.find("\"rhythm_fill\":778,\"rhythm_offset_min\":50}"), std::string::npos);
}

TEST(SleepNow, TheWeekIsShared)
{
    SleepNow::State s;
    s.tonightDay         = 20740;  // a Wednesday
    s.tonight.known      = true;
    s.tonight.bed        = 12 * 60 + 7;   // 00:07
    s.tonight.wake       = 20 * 60 + 15;  // 08:15
    s.tonight.own        = 6;
    s.tonight.nights     = 40;
    s.week.laterBed      = (1u << Schedule::kFriday) | (1u << Schedule::kSaturday);
    s.week.earlierWake   = 1u << Schedule::kSunday;
    s.week.jetlagKnown   = true;
    s.week.socialJetlagMin = 75;
    char out[SleepNow::kCap];
    ASSERT_GT(SleepNow::format(s, out, sizeof(out)), 0u);
    const std::string j = out;
    EXPECT_NE(j.find("\"tonight\":{\"weekday\":3,\"bed_min\":7,\"wake_min\":495,\"own_nights\":6,"
                     "\"nights\":40},"), std::string::npos)
        << j;
    EXPECT_NE(j.find("\"week\":{\"later_bed\":[5,6],\"earlier_bed\":[],\"later_wake\":[],"
                     "\"earlier_wake\":[0],\"social_jetlag_min\":75},"), std::string::npos)
        << j;
}

TEST(SleepNow, TheLongestStateFits)
{
    SleepPeriods::Period p = settledNight();
    p.startUtc             = 4000000000u;
    p.endUtc               = 4000086400u;
    p.restMin              = 1440;
    SleepNow::State s;
    s.writtenUtc     = 4000090000u;
    s.heartbeatS     = 4000000000u;
    s.offsetMin      = -720;
    s.asleepSinceUtc = 4000000000u;
    s.lastNight      = &p;
    s.usual          = Rhythm::Usual{true, 1439, 1439, 255};
    s.usualBefore    = Rhythm::Usual{true, 0, 0, 255};
    s.tonight        = Schedule::Expected{true, 1439, 1439, 255, 255};
    s.week           = Schedule::Week{0x7F, 0, 0x7F, 0, true, -1439};  // a day is later or earlier, not both
    char out[SleepNow::kCap];
    const size_t len = SleepNow::format(s, out, sizeof(out));
    ASSERT_GT(len, 0u);
    EXPECT_LT(len, SleepNow::kCap * 3 / 4) << len;
}

TEST(SleepNow, ShortOfRoomItWritesNothing)
{
    SleepNow::State s;
    char            out[40];
    EXPECT_EQ(SleepNow::format(s, out, sizeof(out)), 0u);
}

TEST(SleepNow, PublishingKeepsTheLastCopyAsBackup)
{
    SDK::Test::FakeFileSystem fs;
    SleepNow::State           s;
    s.writtenUtc = 1;
    ASSERT_TRUE(SleepNow::publish(fs, s));
    s.writtenUtc = 2;
    ASSERT_TRUE(SleepNow::publish(fs, s));
    EXPECT_NE(readAll(fs, SleepNow::kPath).find("\"written\":2,"), std::string::npos);
    EXPECT_NE(readAll(fs, SleepNow::kBakPath).find("\"written\":1,"), std::string::npos);
    EXPECT_FALSE(fs.exist(SleepNow::kTmpPath));
}
