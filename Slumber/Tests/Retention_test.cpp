#include <gtest/gtest.h>

#include <string>

#include "FakeFileSystem.hpp"
#include "LocalTime.hpp"
#include "Retention.hpp"

using namespace Slumber;

namespace
{

constexpr int32_t kEdt = -240;

/// Noon on 2026-10-11 in Toronto.
const int64_t kNow = static_cast<int64_t>(LocalTime::daysFromCivil(2026, 10, 11)) * 86400 + 16 * 3600;

std::string stem(unsigned back)
{
    char s[9];
    LocalTime::dayStem(kNow - static_cast<int64_t>(back) * 86400, kEdt, s);
    return s;
}

std::string day(unsigned back) { return "Days/" + stem(back) + ".csv"; }
std::string cache(unsigned back) { return "Epochs/" + stem(back) + ".bin"; }

/// A day file and its cache for each of the `n` days up to today.
void seedDays(SDK::Test::FakeFileSystem &fs, unsigned n)
{
    for (unsigned back = 0; back < n; ++back) {
        fs.seedFile(day(back), "rows");
        fs.seedFile(cache(back), "epochs");
    }
}

} // namespace

TEST(Retention, KeepsTheLastThirtyDaysAndRemovesTheRest)
{
    SDK::Test::FakeFileSystem fs;
    seedDays(fs, 45);

    EXPECT_EQ(Retention::prune(fs, kNow, kEdt), 2u * 15u);

    for (unsigned back = 0; back < 30; ++back) {
        EXPECT_TRUE(fs.hasFile(day(back))) << day(back);
        EXPECT_TRUE(fs.hasFile(cache(back))) << cache(back);
    }
    for (unsigned back = 30; back < 45; ++back) {
        EXPECT_FALSE(fs.hasFile(day(back))) << day(back);
        EXPECT_FALSE(fs.hasFile(cache(back))) << cache(back);
    }
}

TEST(Retention, ADayIsKeptUntilItIsThirtyDaysOld)
{
    EXPECT_EQ(stem(29), "20260912");
    EXPECT_EQ(stem(30), "20260911");
}

TEST(Retention, PruningTwiceRemovesNothingMore)
{
    SDK::Test::FakeFileSystem fs;
    seedDays(fs, 40);
    Retention::prune(fs, kNow, kEdt);
    EXPECT_EQ(Retention::prune(fs, kNow, kEdt), 0u);
}

TEST(Retention, SetAsideCopiesGoWithTheirDay)
{
    SDK::Test::FakeFileSystem fs;
    const std::string s = stem(31);
    fs.seedFile("Days/" + s + ".csv", "rows");
    fs.seedFile("Days/" + s + "-1.csv", "schema 1");
    fs.seedFile("Days/" + s + "-2.csv", "schema 1");

    EXPECT_EQ(Retention::prune(fs, kNow, kEdt), 3u);
    EXPECT_FALSE(fs.hasFile("Days/" + s + "-1.csv"));
    EXPECT_FALSE(fs.hasFile("Days/" + s + "-2.csv"));
}

TEST(Retention, ACacheWithoutItsDayFileStillGoes)
{
    SDK::Test::FakeFileSystem fs;
    fs.seedFile(cache(35), "epochs");
    EXPECT_EQ(Retention::prune(fs, kNow, kEdt), 1u);
}

TEST(Retention, OtherFilesAreLeftAlone)
{
    SDK::Test::FakeFileSystem fs;
    fs.seedFile("Nights.csv", "nights");
    fs.seedFile("Marks.csv", "marks");
    fs.seedFile("Days-schema1/" + stem(40) + ".csv", "old");
    Retention::prune(fs, kNow, kEdt);
    EXPECT_TRUE(fs.hasFile("Nights.csv"));
    EXPECT_TRUE(fs.hasFile("Marks.csv"));
    EXPECT_TRUE(fs.hasFile("Days-schema1/" + stem(40) + ".csv"));
}

TEST(Retention, ADayPastTheLookbackIsNeverReached)
{
    SDK::Test::FakeFileSystem fs;
    const unsigned past = Retention::kKeepDays + Retention::kLookbackDays;
    fs.seedFile(day(past), "rows");
    EXPECT_EQ(Retention::prune(fs, kNow, kEdt), 0u);
    EXPECT_TRUE(fs.hasFile(day(past)));
}

TEST(Retention, NeverRemovesTheDaysTheEpochCacheRestores)
{
    SDK::Test::FakeFileSystem fs;
    seedDays(fs, 10);
    Retention::prune(fs, kNow, kEdt, 0);
    for (unsigned back = 0; back < Retention::kMinKeepDays; ++back) {
        EXPECT_TRUE(fs.hasFile(day(back))) << day(back);
        EXPECT_TRUE(fs.hasFile(cache(back))) << cache(back);
    }
    EXPECT_FALSE(fs.hasFile(day(Retention::kMinKeepDays)));
}

TEST(Retention, TheBoundaryHoldsAcrossTheEndOfDaylightSaving)
{
    // EDT ends on 2026-11-01; on 2026-11-15 Toronto is on EST.
    SDK::Test::FakeFileSystem fs;
    const int64_t nov15 = static_cast<int64_t>(LocalTime::daysFromCivil(2026, 11, 15)) * 86400 + 17 * 3600;
    fs.seedFile("Days/20261016.csv", "rows");
    fs.seedFile("Days/20261017.csv", "rows");
    fs.seedFile("Days/20261101.csv", "rows");

    EXPECT_EQ(Retention::prune(fs, nov15, -300), 1u);
    EXPECT_FALSE(fs.hasFile("Days/20261016.csv"));
    EXPECT_TRUE(fs.hasFile("Days/20261017.csv"));
    EXPECT_TRUE(fs.hasFile("Days/20261101.csv"));
}
