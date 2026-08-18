/**
 * Host tests for the settings file.
 *
 * Mostly negative paths, deliberately. This app is autostart: a settings file
 * that could stop it running would stop it running *for ever*, with no screen
 * to say why. So every way the file can be wrong has to end in usable defaults,
 * and the one thing that must never happen is a value being silently clamped
 * into a different experiment.
 */

#include <gtest/gtest.h>

#include <string>

#include "KernelTestDoubles.hpp"
#include "Settings.hpp"

namespace {

using SDK::TestSupport::KernelFixture;
using SleepLab::HrMode;
using SleepLab::Settings;
using SleepLab::SettingsStatus;

SettingsStatus load(KernelFixture &fx, const std::string &json, Settings &out)
{
    fx.fileSystem.seedFile(SleepLab::kSettingsPath, json);
    return SleepLab::loadSettings(fx.kernel, out);
}

// -- Failure paths -----------------------------------------------------------

TEST(Settings, AnAbsentFileGivesUsableDefaults)
{
    KernelFixture fx;
    Settings s;
    EXPECT_EQ(SleepLab::loadSettings(fx.kernel, s), SettingsStatus::Absent);

    // 21:00-11:00, which crosses midnight -- the normal case.
    EXPECT_EQ(s.segmenter.windowStartMin, 21 * 60);
    EXPECT_EQ(s.segmenter.windowEndMin,   11 * 60);
    EXPECT_EQ(s.hrMode, HrMode::Continuous);
    EXPECT_FALSE(s.alarmEnabled) << "an alarm must be opt-in";
    EXPECT_FALSE(s.rawRecording) << "31 MB a night must be opt-in";
}

TEST(Settings, EveryWayTheFileCanBeWrongEndsInDefaults)
{
    struct Case { const char *json; SettingsStatus expect; };
    const Case kCases[] = {
        { "",                                        SettingsStatus::NotJson },
        { "not json at all",                         SettingsStatus::NotJson },
        { R"({"values":{"bedtime":"22:00"}})",        SettingsStatus::WrongSchema },
        { R"({"schema":99,"values":{"bedtime":"22:00"}})", SettingsStatus::WrongSchema },
    };

    for (const auto &c : kCases) {
        KernelFixture fx;
        Settings s;
        EXPECT_EQ(load(fx, c.json, s), c.expect) << c.json;
        EXPECT_EQ(s.segmenter.windowStartMin, 21 * 60) << c.json;
    }
}

TEST(Settings, AnOversizedFileIsRefusedBeforeAnythingIsAllocated)
{
    // All five SDK settings serializers do `new char[file->size()]` with no
    // upper bound, which turns any oversized file dropped into an app folder
    // into a bite out of a 256 KB app budget.
    KernelFixture fx;
    Settings s;
    EXPECT_EQ(load(fx, std::string(SleepLab::kSettingsMaxBytes + 1, 'x'), s),
              SettingsStatus::TooLarge);
    EXPECT_EQ(s.hrMode, HrMode::Continuous);
}

// -- Times of day -------------------------------------------------------------

TEST(Settings, TimesAreWrittenTheWayAPersonWritesThem)
{
    KernelFixture fx;
    Settings s;
    ASSERT_EQ(load(fx, R"({"schema":1,"values":{
        "bedtime":"22:30","wake_by":"9:15","alarm_at":"06:45"}})", s),
              SettingsStatus::Ok);

    EXPECT_EQ(s.segmenter.windowStartMin, 22 * 60 + 30);
    EXPECT_EQ(s.segmenter.windowEndMin,    9 * 60 + 15) << "H:MM as well as HH:MM";
    EXPECT_EQ(s.alarmDeadlineMin,          6 * 60 + 45);
}

TEST(Settings, AMalformedTimeKeepsTheDefaultRatherThanHalfParsing)
{
    // A partially-parsed time is a wrong time, and a wrong bedtime window
    // records the wrong hours of the night.
    const char *kBad[] = {
        R"({"schema":1,"values":{"bedtime":"2230"}})",
        R"({"schema":1,"values":{"bedtime":"22:3"}})",
        R"({"schema":1,"values":{"bedtime":"22:75"}})",
        R"({"schema":1,"values":{"bedtime":"25:00"}})",
        R"({"schema":1,"values":{"bedtime":"ab:cd"}})",
        R"({"schema":1,"values":{"bedtime":"10:30pm"}})",
    };

    for (const char *json : kBad) {
        KernelFixture fx;
        Settings s;
        ASSERT_EQ(load(fx, json, s), SettingsStatus::Ok) << json;
        EXPECT_EQ(s.segmenter.windowStartMin, 21 * 60) << json;
    }
}

// -- Out of range is refused, never clamped ------------------------------------

TEST(Settings, AnOutOfRangeValueIsRefusedRatherThanClamped)
{
    // THE test in this file. Clamping turns a typo into a silently different
    // experiment: a night recorded at some value nobody chose, with a log that
    // looks perfectly healthy.
    KernelFixture fx;
    Settings s;
    ASSERT_EQ(load(fx, R"({"schema":1,"values":{
        "min_night_min":99999,"alarm_window_min":0,
        "hr_duty_on_sec":1,"raw_max_mb":9999}})", s),
              SettingsStatus::Ok);

    const Settings def;
    EXPECT_EQ(s.segmenter.minSessionMin, def.segmenter.minSessionMin);
    EXPECT_EQ(s.alarmWindowMin,          def.alarmWindowMin);
    EXPECT_EQ(s.hrDutyOnSec,             def.hrDutyOnSec);
    EXPECT_EQ(s.rawMaxMb,                def.rawMaxMb);
}

TEST(Settings, FlagsAcceptTheVocabularyAPersonTypes)
{
    struct Case { const char *value; bool expect; };
    const Case kCases[] = {
        { "on", true }, { "ON", true }, { "yes", true }, { "True", true },
        { "1", true },  { "enabled", true },
        { "off", false }, { "no", false }, { "FALSE", false }, { "0", false },
    };

    for (const auto &c : kCases) {
        KernelFixture fx;
        Settings s;
        const std::string json =
            std::string(R"({"schema":1,"values":{"raw_recording":")") +
            c.value + R"("}})";
        ASSERT_EQ(load(fx, json, s), SettingsStatus::Ok) << c.value;
        EXPECT_EQ(s.rawRecording, c.expect) << c.value;
    }
}

TEST(Settings, AFlagThatIsNeitherKeepsTheDefault)
{
    // These files are typed by hand. "maybe" is off, because off is the safe
    // direction for a flag whose only effect is to start filling flash.
    KernelFixture fx;
    Settings s;
    ASSERT_EQ(load(fx, R"({"schema":1,"values":{"raw_recording":"maybe"}})", s),
              SettingsStatus::Ok);
    EXPECT_FALSE(s.rawRecording);
}

// -- Coherence -----------------------------------------------------------------

TEST(Settings, ADutyCycleThatIsNotOneFallsBackToContinuous)
{
    // on >= period is not a duty cycle, and a night whose mode column claimed
    // something the sensor was not doing would be worse than one that ran the
    // expensive mode.
    KernelFixture fx;
    Settings s;
    ASSERT_EQ(load(fx, R"({"schema":1,"values":{
        "hr":"duty","hr_duty_on_sec":300,"hr_duty_per_sec":120}})", s),
              SettingsStatus::Ok);
    EXPECT_EQ(s.hrMode, HrMode::Continuous);
}

TEST(Settings, AZeroWidthBedtimeWindowIsRestoredToTheDefault)
{
    // A window that never opens looks exactly like a user who never went to
    // bed, which is a bug nobody would report.
    KernelFixture fx;
    Settings s;
    ASSERT_EQ(load(fx, R"({"schema":1,"values":{
        "bedtime":"22:00","wake_by":"22:00"}})", s), SettingsStatus::Ok);
    EXPECT_EQ(s.segmenter.windowStartMin, 21 * 60);
    EXPECT_EQ(s.segmenter.windowEndMin,   11 * 60);
}

TEST(Settings, AnAlarmOutsideTheBedtimeWindowIsDisabledRatherThanLeftSilent)
{
    // The session is already closed by then, so the alarm could never fire --
    // and an alarm that fails silently is worse than no alarm.
    KernelFixture fx;
    Settings s;
    ASSERT_EQ(load(fx, R"({"schema":1,"values":{
        "bedtime":"22:00","wake_by":"07:00","alarm":"on","alarm_at":"14:00"}})",
        s), SettingsStatus::Ok);
    EXPECT_FALSE(s.alarmEnabled);
}

TEST(Settings, AnAlarmInsideTheWindowSurvives)
{
    KernelFixture fx;
    Settings s;
    ASSERT_EQ(load(fx, R"({"schema":1,"values":{
        "bedtime":"22:00","wake_by":"09:00","alarm":"on","alarm_at":"06:30"}})",
        s), SettingsStatus::Ok);
    EXPECT_TRUE(s.alarmEnabled);
    EXPECT_EQ(s.alarmDeadlineMin, 6 * 60 + 30);
}

TEST(Settings, UnknownKeysAreIgnoredRatherThanRejected)
{
    // The app never writes this file back, so a key it does not recognise is
    // at worst an orphan -- and rejecting the file over one would cost the
    // wearer every setting in it.
    KernelFixture fx;
    Settings s;
    ASSERT_EQ(load(fx, R"({"schema":1,"values":{
        "bedtime":"23:00","something_else":"whatever"}})", s),
        SettingsStatus::Ok);
    EXPECT_EQ(s.segmenter.windowStartMin, 23 * 60);
}

} // namespace
