// Reads the shared sleep files the way another watch app would: through the
// SDK's own JsonStreamReader, knowing nothing of Slumber's code.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "FakeFileSystem.hpp"
#include "NightLog.hpp"
#include "SDK/JSON/JsonStreamReader.hpp"
#include "SleepNow.hpp"

using namespace Slumber;

namespace
{

/// 2026-10-08 23:54 to 2026-10-09 09:05 in Toronto, moving in its second epoch.
SleepPeriods::Period night()
{
    SleepPeriods::Period p;
    p.startUtc = 1791518040;
    p.endUtc   = 1791551100;
    p.restMin  = 551;
    p.worn     = SleepPeriods::Worn::Worn;
    p.settled  = true;
    return p;
}

std::vector<std::string> lines(const std::string &all)
{
    std::vector<std::string> out;
    size_t                   from = 0;
    for (size_t nl = all.find('\n'); nl != std::string::npos; nl = all.find('\n', from)) {
        out.push_back(all.substr(from, nl - from));
        from = nl + 1;
    }
    return out;
}

} // namespace

TEST(SharedSleep, ASessionLineReadsThroughTheSdkJsonReader)
{
    SDK::Test::FakeFileSystem fs;
    auto                      log = std::make_unique<NightLog>(fs);
    const SleepPeriods::Period p   = night();
    std::vector<SleepPeriods::Epoch> e;
    for (uint32_t t = p.startUtc; t < p.endUtc; t += SleepPeriods::kEpochS) {
        SleepPeriods::Epoch x;
        x.utc   = t;
        x.count = t == p.startUtc + SleepPeriods::kEpochS ? 5000u : 200u;
        e.push_back(x);
    }
    ASSERT_TRUE(log->due(p));
    ASSERT_TRUE(log->add(p, {}, -240, e.data(), e.size()));

    const auto ls = lines(fs.fileContents(NightLog::kPath));
    ASSERT_EQ(ls.size(), 1u);
    SDK::JsonStreamReader json(ls[0].data(), ls[0].size());
    ASSERT_TRUE(json.validate());

    std::string_view source, epochs;
    uint32_t         start = 0, end = 0, epochS = 0;
    int16_t          off   = 0;
    ASSERT_TRUE(json.get("source", source));
    ASSERT_TRUE(json.get("start", start));
    ASSERT_TRUE(json.get("end", end));
    ASSERT_TRUE(json.get("utc_off_min", off));
    ASSERT_TRUE(json.get("epoch_s", epochS));
    ASSERT_TRUE(json.get("epochs", epochs));
    EXPECT_EQ(source, "Slumber");
    EXPECT_EQ(start, p.startUtc);
    EXPECT_EQ(end, p.endUtc);
    EXPECT_EQ(off, -240);
    EXPECT_EQ(epochs.size() * epochS, end - start);
    EXPECT_EQ(epochs.substr(0, 3), "rmr");
}

TEST(SharedSleep, NowReadsThroughTheSdkJsonReader)
{
    SDK::Test::FakeFileSystem  fs;
    const SleepPeriods::Period p = night();
    SleepNow::State            s;
    s.writtenUtc     = p.endUtc + 600;
    s.heartbeatS     = 300;
    s.offsetMin      = -240;
    s.lastNight      = &p;
    s.usual.known    = true;
    s.usual.bed      = Rhythm::afterNoon(p.startUtc, -240);
    s.usual.wake     = Rhythm::afterNoon(p.endUtc, -240);
    s.usual.nights   = 5;
    s.tonightDay     = Schedule::tonight(s.writtenUtc, -240);
    s.lastHeart      = NightSummary::Heart{57, 51, p.startUtc + 3600};
    ASSERT_TRUE(SleepNow::publish(fs, s));

    const std::string all = fs.fileContents(SleepNow::kPath);
    SDK::JsonStreamReader json(all.data(), all.size());
    ASSERT_TRUE(json.validate());

    bool     asleepNull = false, settled = false, rhythmNull = false;
    uint32_t lastStart = 0;
    uint16_t bed = 0, wake = 0, restFill = 0;
    ASSERT_TRUE(json.getNull("asleep_since", asleepNull));
    ASSERT_TRUE(json.get("last_night.start", lastStart));
    ASSERT_TRUE(json.get("last_night.settled", settled));
    ASSERT_TRUE(json.get("usual.bed_min", bed));
    ASSERT_TRUE(json.get("usual.wake_min", wake));
    ASSERT_TRUE(json.get("rest_fill", restFill));
    ASSERT_TRUE(json.getNull("rhythm_fill", rhythmNull));
    EXPECT_TRUE(asleepNull);
    EXPECT_EQ(lastStart, p.startUtc);
    EXPECT_TRUE(settled);
    EXPECT_EQ(bed, 23 * 60 + 54);
    EXPECT_EQ(wake, 9 * 60 + 5);
    EXPECT_EQ(restFill, 1311);
    EXPECT_TRUE(rhythmNull);

    uint8_t  weekday = 9, heartMedian = 0;
    uint16_t tonightBed = 0;
    ASSERT_TRUE(json.get("tonight.weekday", weekday));
    ASSERT_TRUE(json.get("tonight.bed_min", tonightBed));
    ASSERT_TRUE(json.get("heart.median_bpm", heartMedian));
    EXPECT_EQ(weekday, Schedule::weekday(s.tonightDay));
    EXPECT_EQ(tonightBed, 23 * 60 + 54);
    EXPECT_EQ(heartMedian, 57);
}
