#include "SettingsSplice.hpp"

#include "SettingsField.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace
{

constexpr size_t kCapacity = 520;

/// The real file read off a watch on 2026-09-04, byte for byte.
constexpr const char *kRealFile =
    "{\"units\":\"metric\",\"watchFaceId\":0,\"phone\":{\"notifications\":false},"
    "\"heartRateZones\":[30,60,70,80,90,100],\"dailyGoals\":{\"activityMinutes\":30,"
    "\"steps\":5000,\"floors\":5},\"height\":190,\"weight\":90,\"gender\":\"M\","
    "\"dateOfBirth\":\"1990-01-01\",\"version\":2}";

struct Spliced {
    SettingsSplice::Result result;
    std::string text;
};

Spliced splice(const std::string &input, bool enable)
{
    char buf[kCapacity] = {};
    std::memcpy(buf, input.data(), input.size());
    size_t len = input.size();
    const auto result = SettingsSplice::setNotifications(buf, len, kCapacity, enable);
    return {result, std::string(buf, len)};
}

TEST(SettingsSplice, TurnsTheRealFileOn)
{
    const auto out = splice(kRealFile, true);
    EXPECT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_NE(out.text.find("\"phone\":{\"notifications\":true}"), std::string::npos);
}

/// Everything this app does not own has to come back byte for byte, including
/// the personal fields it never parses.
TEST(SettingsSplice, ChangesNothingElseInTheRealFile)
{
    const auto out = splice(kRealFile, true);
    ASSERT_EQ(out.result, SettingsSplice::Result::Ok);

    std::string expected = kRealFile;
    const auto at = expected.find("\"notifications\":false");
    expected.replace(at, std::strlen("\"notifications\":false"), "\"notifications\":true");
    EXPECT_EQ(out.text, expected);
}

TEST(SettingsSplice, RoundTripsBackToTheOriginal)
{
    const auto on = splice(kRealFile, true);
    ASSERT_EQ(on.result, SettingsSplice::Result::Ok);
    const auto off = splice(on.text, false);
    ASSERT_EQ(off.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(off.text, kRealFile);
}

TEST(SettingsSplice, WritingTheValueItAlreadyHasIsAByteIdenticalNoOp)
{
    const auto out = splice(kRealFile, false);
    EXPECT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text, kRealFile);
}

TEST(SettingsSplice, AcceptsWhitespaceAroundTheColons)
{
    const auto out = splice("{\"phone\" : { \"notifications\" :  false } }", true);
    EXPECT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text, "{\"phone\" : { \"notifications\" :  true } }");
}

/// The bug this scoping exists to prevent: a same-named key outside `phone`,
/// which a whole-file substring search would have edited instead.
TEST(SettingsSplice, IgnoresANotificationsKeyOutsideThePhoneObject)
{
    const auto out = splice(
        "{\"watch\":{\"notifications\":true},\"phone\":{\"notifications\":false}}", true);
    ASSERT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text, "{\"watch\":{\"notifications\":true},\"phone\":{\"notifications\":true}}");
}

TEST(SettingsSplice, IgnoresALongerKeyThatEndsInTheSameWord)
{
    const auto out = splice("{\"phone\":{\"pushNotifications\":true,\"notifications\":false}}", true);
    ASSERT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text, "{\"phone\":{\"pushNotifications\":true,\"notifications\":true}}");
}

TEST(SettingsSplice, SurvivesANestedObjectInsidePhone)
{
    const auto out = splice(
        "{\"phone\":{\"quiet\":{\"from\":22,\"to\":7},\"notifications\":false},\"x\":1}", true);
    ASSERT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text,
              "{\"phone\":{\"quiet\":{\"from\":22,\"to\":7},\"notifications\":true},\"x\":1}");
}

/// A brace inside a string value must not be read as structure.
TEST(SettingsSplice, IsNotFooledByBracesInsideStrings)
{
    const auto out = splice("{\"phone\":{\"label\":\"a}b{c\",\"notifications\":false}}", true);
    ASSERT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text, "{\"phone\":{\"label\":\"a}b{c\",\"notifications\":true}}");
}

TEST(SettingsSplice, IsNotFooledByAnEscapedQuote)
{
    const auto out = splice("{\"phone\":{\"label\":\"a\\\"}\",\"notifications\":false}}", true);
    ASSERT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text, "{\"phone\":{\"label\":\"a\\\"}\",\"notifications\":true}}");
}

TEST(SettingsSplice, RefusesWhenThereIsNoPhoneObject)
{
    const auto out = splice("{\"units\":\"metric\",\"notifications\":false}", true);
    EXPECT_EQ(out.result, SettingsSplice::Result::FieldNotFound);
}

TEST(SettingsSplice, RefusesWhenTheValueIsNotABoolean)
{
    for (const char *file : {"{\"phone\":{\"notifications\":1}}",
                             "{\"phone\":{\"notifications\":\"true\"}}",
                             "{\"phone\":{\"notifications\":null}}"}) {
        EXPECT_EQ(splice(file, true).result, SettingsSplice::Result::FieldNotFound) << file;
    }
}

TEST(SettingsSplice, RefusesWhenPhoneIsNotAnObject)
{
    EXPECT_EQ(splice("{\"phone\":\"none\",\"notifications\":false}", true).result,
              SettingsSplice::Result::FieldNotFound);
}

TEST(SettingsSplice, RefusesTruncatedJson)
{
    EXPECT_EQ(splice("{\"phone\":{\"notifications\":fal", true).result,
              SettingsSplice::Result::FieldNotFound);
    EXPECT_EQ(splice("{\"phone\":{\"notifications\":false", true).result,
              SettingsSplice::Result::FieldNotFound);
}

TEST(SettingsSplice, RefusesAnEmptyBuffer)
{
    char buf[kCapacity] = {};
    size_t len = 0;
    EXPECT_EQ(SettingsSplice::setNotifications(buf, len, kCapacity, true),
              SettingsSplice::Result::FieldNotFound);
}

/// The one growth case: true -> false is a byte longer, so a buffer with no
/// room must refuse rather than write past the end.
TEST(SettingsSplice, RefusesWhenGrowingWouldNotFit)
{
    const std::string input = "{\"phone\":{\"notifications\":true}}";
    char buf[kCapacity] = {};
    std::memcpy(buf, input.data(), input.size());
    size_t len = input.size();

    EXPECT_EQ(SettingsSplice::setNotifications(buf, len, input.size(), true),
              SettingsSplice::Result::Ok);
    EXPECT_EQ(SettingsSplice::setNotifications(buf, len, input.size(), false),
              SettingsSplice::Result::WouldNotFit);
    EXPECT_EQ(len, input.size());
    EXPECT_EQ(std::string(buf, len), input);
}

/// Shrinking and growing move the tail in opposite directions; both have to
/// land the trailing bytes intact.
TEST(SettingsSplice, MovesTheTailCorrectlyInBothDirections)
{
    const std::string tail = ",\"a\":1,\"b\":2,\"c\":\"trailing\"}";

    const auto shrunk = splice("{\"phone\":{\"notifications\":false}" + tail, true);
    ASSERT_EQ(shrunk.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(shrunk.text, "{\"phone\":{\"notifications\":true}" + tail);

    const auto grown = splice(shrunk.text, false);
    ASSERT_EQ(grown.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(grown.text, "{\"phone\":{\"notifications\":false}" + tail);
}

TEST(SettingsSplice, NeverWritesPastTheReportedLength)
{
    const std::string input = "{\"phone\":{\"notifications\":true}}";
    char buf[kCapacity];
    std::memset(buf, '\xAA', sizeof(buf));
    std::memcpy(buf, input.data(), input.size());
    size_t len = input.size();

    ASSERT_EQ(SettingsSplice::setNotifications(buf, len, kCapacity, false),
              SettingsSplice::Result::Ok);
    for (size_t i = len; i < sizeof(buf); ++i) {
        ASSERT_EQ(static_cast<unsigned char>(buf[i]), 0xAAu) << "wrote past len at " << i;
    }
}

} // namespace

namespace
{

/// The offset a debug build logs instead of the file's contents, so a failed
/// splice can be diagnosed without a copy of the wearer's personal data.
TEST(SettingsSplice, ReportsWhereItEdited)
{
    char buf[kCapacity] = {};
    const std::string input = kRealFile;
    std::memcpy(buf, input.data(), input.size());
    size_t len = input.size();

    size_t at = 0;
    ASSERT_EQ(SettingsSplice::setNotifications(buf, len, kCapacity, true, &at),
              SettingsSplice::Result::Ok);
    EXPECT_EQ(at, input.find("false"));
    EXPECT_EQ(std::string(buf + at, 4), "true");
}

TEST(SettingsSplice, LeavesTheOffsetAloneWhenItRefuses)
{
    char buf[kCapacity] = {};
    const std::string input = "{\"units\":\"metric\"}";
    std::memcpy(buf, input.data(), input.size());
    size_t len = input.size();

    size_t at = 0xABCD;
    EXPECT_EQ(SettingsSplice::setNotifications(buf, len, kCapacity, true, &at),
              SettingsSplice::Result::FieldNotFound);
    EXPECT_EQ(at, 0xABCDu);
}

} // namespace

namespace
{

} // namespace

namespace
{

/// The same characters appear in a string value, and only the colon after them
/// says which one was a key. Stopping at the first match made a settings file
/// that merely contained the word refuse to be read at all.
TEST(SettingsScan, LooksPastAValueThatLooksLikeTheKey)
{
    const auto out = splice(R"({"a":"phone","phone":{"notifications":false}})", true);
    ASSERT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text, R"({"a":"phone","phone":{"notifications":true}})");
}

TEST(SettingsScan, LooksPastANotificationsValueInsidePhone)
{
    const auto out = splice(R"({"phone":{"a":"notifications","notifications":false}})", true);
    ASSERT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text, R"({"phone":{"a":"notifications","notifications":true}})");
}



/// A key whose value is the wrong shape is not searched past: two `phone` keys
/// would be a file this app does not understand, and guessing which to edit is
/// worse than refusing.
TEST(SettingsScan, DoesNotSearchPastAPhoneKeyThatIsNotAnObject)
{
    EXPECT_EQ(splice(R"({"phone":"none","phone":{"notifications":false}})", true).result,
              SettingsSplice::Result::FieldNotFound);
}

/// Scoping to the `phone` object is not enough on its own: matching anywhere
/// inside it rewrote a nested key and left `phone.notifications` alone, and the
/// readback confirmed it because it compares the file against the buffer just
/// written. The change then reverted at the next reboot.
TEST(SettingsScan, IgnoresANotificationsKeyNestedInsidePhone)
{
    const auto out =
        splice(R"({"phone":{"nested":{"notifications":true},"notifications":false}})", true);
    ASSERT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text,
              R"({"phone":{"nested":{"notifications":true},"notifications":true}})");
}

TEST(SettingsScan, RefusesWhenTheOnlyNotificationsKeyIsNestedInsidePhone)
{
    EXPECT_EQ(splice(R"({"phone":{"nested":{"notifications":true}},"x":1})", true).result,
              SettingsSplice::Result::FieldNotFound);
}

/// `phone` itself has to be found at the outermost depth too, or a `phone`
/// object nested in something else is edited in preference to the real one.
TEST(SettingsScan, IgnoresAPhoneObjectNestedInsideAnotherObject)
{
    const auto out = splice(
        R"({"lastPaired":{"phone":{"notifications":true}},"phone":{"notifications":true}})", false);
    ASSERT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text,
              R"({"lastPaired":{"phone":{"notifications":true}},"phone":{"notifications":false}})");
}

TEST(SettingsScan, RefusesWhenTheOnlyPhoneObjectIsNested)
{
    EXPECT_EQ(splice(R"({"lastPaired":{"phone":{"notifications":true}},"x":1})", false).result,
              SettingsSplice::Result::FieldNotFound);
}

} // namespace

namespace
{

/// The shape of two real files read off one watch on 2026-09-06 either side of
/// a flip made from the phone, with every personal value replaced by a neutral
/// one of the same byte length -- height, weight, gender, date of birth and the
/// heart-rate zones are the wearer's, and a test fixture is a poor place to
/// keep them. The lengths are what these cases turn on, and they are exact:
/// 245 bytes and 247, with the units token starting at byte 9 in both.
constexpr const char *kRealMetric =
    "{\"units\":\"metric\",\"watchFaceId\":0,\"phone\":{\"notifications\":false},"
    "\"heartRateZones\":[99,111,122,133,144,155],\"dailyGoals\":{\"activityMinutes\":30,"
    "\"steps\":5000,\"floors\":5},\"height\":100,\"weight\":10,\"gender\":\"X\","
    "\"dateOfBirth\":\"2000-01-01\",\"version\":2}";

constexpr const char *kRealImperial =
    "{\"units\":\"imperial\",\"watchFaceId\":0,\"phone\":{\"notifications\":false},"
    "\"heartRateZones\":[99,111,122,133,144,155],\"dailyGoals\":{\"activityMinutes\":30,"
    "\"steps\":5000,\"floors\":5},\"height\":100,\"weight\":10,\"gender\":\"X\","
    "\"dateOfBirth\":\"2000-01-01\",\"version\":2}";

Spliced spliceUnits(const std::string &input, bool imperial)
{
    char buf[kCapacity] = {};
    std::memcpy(buf, input.data(), input.size());
    size_t len = input.size();
    const auto result = SettingsSplice::setUnits(buf, len, kCapacity, imperial);
    return {result, std::string(buf, len)};
}

TEST(SettingsUnits, TurnsTheMetricFileIntoTheImperialOne)
{
    const auto out = spliceUnits(kRealMetric, true);
    EXPECT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text, kRealImperial);
}

TEST(SettingsUnits, TurnsTheImperialFileIntoTheMetricOne)
{
    const auto out = spliceUnits(kRealImperial, false);
    EXPECT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text, kRealMetric);
}

/// 245 and 247 as measured, which is what makes the delta two bytes rather than
/// the one a true/false rewrite moves.
TEST(SettingsUnits, TheMeasuredFilesAreTheLengthsTheBufferIsSizedFor)
{
    EXPECT_EQ(std::strlen(kRealMetric), 245u);
    EXPECT_EQ(std::strlen(kRealImperial), 247u);
}

TEST(SettingsUnits, RoundTripsBackToTheOriginal)
{
    const auto imperial = spliceUnits(kRealMetric, true);
    ASSERT_EQ(imperial.result, SettingsSplice::Result::Ok);
    const auto back = spliceUnits(imperial.text, false);
    ASSERT_EQ(back.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(back.text, kRealMetric);
}

TEST(SettingsUnits, SettingWhatIsAlreadySetChangesNothing)
{
    const auto out = spliceUnits(kRealMetric, false);
    EXPECT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text, kRealMetric);
}

/// The firmware's parser knows two spellings and stores nothing for anything
/// else, so a third is a file this does not understand.
TEST(SettingsUnits, RefusesATokenTheFirmwareWouldNotRecognise)
{
    EXPECT_EQ(spliceUnits(R"({"units":"furlongs","version":2})", true).result,
              SettingsSplice::Result::FieldNotFound);
}

TEST(SettingsUnits, RefusesWhenTheValueIsNotAString)
{
    EXPECT_EQ(spliceUnits(R"({"units":true,"version":2})", true).result,
              SettingsSplice::Result::FieldNotFound);
    EXPECT_EQ(spliceUnits(R"({"units":0,"version":2})", true).result,
              SettingsSplice::Result::FieldNotFound);
}

TEST(SettingsUnits, RefusesWhenThereIsNoUnitsKey)
{
    EXPECT_EQ(spliceUnits(R"({"watchFaceId":0,"version":2})", true).result,
              SettingsSplice::Result::FieldNotFound);
}

TEST(SettingsUnits, RefusesTruncatedJson)
{
    EXPECT_EQ(spliceUnits(R"({"units":"metr)", true).result,
              SettingsSplice::Result::FieldNotFound);
    EXPECT_EQ(spliceUnits(R"({"units":)", true).result,
              SettingsSplice::Result::FieldNotFound);
}

TEST(SettingsUnits, RefusesAnEmptyBuffer)
{
    EXPECT_EQ(spliceUnits("", true).result, SettingsSplice::Result::FieldNotFound);
}

/// The same characters appear as a string *value*, and only the colon after
/// them says which one was the key.
TEST(SettingsUnits, LooksPastAValueThatLooksLikeTheKey)
{
    const auto out = spliceUnits(R"({"note":"units","units":"metric"})", true);
    EXPECT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text, R"({"note":"units","units":"imperial"})");
}

TEST(SettingsUnits, ToleratesWhitespaceAroundTheColon)
{
    const auto out = spliceUnits("{ \"units\" : \"metric\" ,\"version\":2}", true);
    EXPECT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text, "{ \"units\" : \"imperial\" ,\"version\":2}");
}

TEST(SettingsUnits, RefusesWhenTheTwoByteGrowthWouldNotFit)
{
    const std::string input = R"({"units":"metric"})";
    char buf[64] = {};
    std::memcpy(buf, input.data(), input.size());
    size_t len = input.size();

    EXPECT_EQ(SettingsSplice::setUnits(buf, len, input.size() + 1, true),
              SettingsSplice::Result::WouldNotFit);
    EXPECT_EQ(len, input.size());
    EXPECT_EQ(std::string(buf, len), input);

    EXPECT_EQ(SettingsSplice::setUnits(buf, len, input.size() + 2, true),
              SettingsSplice::Result::Ok);
    EXPECT_EQ(std::string(buf, len), R"({"units":"imperial"})");
}

TEST(SettingsUnits, MovesTheTailCorrectlyInBothDirections)
{
    const auto grown = spliceUnits(R"({"units":"metric","tail":[1,2,3]})", true);
    ASSERT_EQ(grown.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(grown.text, R"({"units":"imperial","tail":[1,2,3]})");

    const auto shrunk = spliceUnits(grown.text, false);
    ASSERT_EQ(shrunk.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(shrunk.text, R"({"units":"metric","tail":[1,2,3]})");
}

TEST(SettingsUnits, ReportsWhereItEdited)
{
    char buf[kCapacity] = {};
    const std::string input = kRealMetric;
    std::memcpy(buf, input.data(), input.size());
    size_t len = input.size();

    size_t at = 0;
    ASSERT_EQ(SettingsSplice::setUnits(buf, len, kCapacity, true, &at),
              SettingsSplice::Result::Ok);
    EXPECT_EQ(at, 9u);
    EXPECT_EQ(buf[at], '"');
}

TEST(SettingsUnits, LeavesTheOffsetAloneWhenItRefuses)
{
    char buf[kCapacity] = {};
    const std::string input = R"({"units":"furlongs"})";
    std::memcpy(buf, input.data(), input.size());
    size_t len = input.size();

    size_t at = 0xABCD;
    EXPECT_EQ(SettingsSplice::setUnits(buf, len, kCapacity, true, &at),
              SettingsSplice::Result::FieldNotFound);
    EXPECT_EQ(at, 0xABCDu);
}

/// `Field` is five interchangeable `const char *` in a row, so the compiler
/// cannot tell a transposition from a correct one. `isWellFormed` is what does,
/// and each app asserts it at compile time -- these cases are what say the
/// assertion is worth having.
namespace field_cases
{

constexpr SettingsPersist::Field kGood = {
    .splice     = &SettingsSplice::setUnits,
    .name       = "units",
    .probeAPath = "2:/ut-probe-a.tmp",
    .probeBPath = "2:/ut-probe-b.tmp",
    .probeText  = "UnitToggle primitive self-test",
};

constexpr SettingsPersist::Field with(SettingsPersist::Field f, int slot, const char *value)
{
    switch (slot) {
        case 0: f.probeAPath = value; break;
        case 1: f.probeBPath = value; break;
        default: f.probeText = value; break;
    }
    return f;
}

/// How many interchangeable `const char *` paths `isWellFormed` checks.
constexpr int kPathSlots = 2;

} // namespace field_cases

TEST(SettingsField, AcceptsAWellFormedDescriptor)
{
    EXPECT_TRUE(SettingsPersist::isWellFormed(field_cases::kGood));
}

/// The transposition that matters: two members holding the same path means the
/// commit moves the file aside under the name it is about to write.
TEST(SettingsField, RejectsTwoPathsThatAreTheSame)
{
    for (int slot = 0; slot < field_cases::kPathSlots; ++slot) {
        for (int other = 0; other < field_cases::kPathSlots; ++other) {
            if (slot == other) {
                continue;
            }
            const char *const paths[] = {field_cases::kGood.probeAPath,
                                         field_cases::kGood.probeBPath};
            EXPECT_FALSE(SettingsPersist::isWellFormed(
                field_cases::with(field_cases::kGood, slot, paths[other])))
                << "slot " << slot << " set to slot " << other;
        }
    }
}

/// A probe path colliding with the settings file, or with either shared commit
/// scratch name, would have the self-test delete what a commit is holding.
TEST(SettingsField, RejectsAProbePathThatIsAReservedName)
{
    for (const char *reserved : {SettingsPersist::kSettingsPath,
                                 SettingsPersist::kScratchTmpPath,
                                 SettingsPersist::kScratchPrevPath}) {
        for (int slot = 0; slot < field_cases::kPathSlots; ++slot) {
            EXPECT_FALSE(SettingsPersist::isWellFormed(
                field_cases::with(field_cases::kGood, slot, reserved)))
                << reserved << " in slot " << slot;
        }
    }
}

/// The commit's scratch names are shared, so recovery reaches a stranded file
/// whichever app left it -- and the legacy names stay swept until no watch can
/// still be carrying one.
TEST(SettingsField, TheSharedScratchNamesAreDistinctFromEachOtherAndTheRealFile)
{
    EXPECT_STRNE(SettingsPersist::kScratchTmpPath, SettingsPersist::kScratchPrevPath);
    EXPECT_STRNE(SettingsPersist::kScratchTmpPath, SettingsPersist::kSettingsPath);
    EXPECT_STRNE(SettingsPersist::kScratchPrevPath, SettingsPersist::kSettingsPath);
    for (size_t i = 0; i < SettingsPersist::kLegacyPrevPathCount; ++i) {
        EXPECT_STRNE(SettingsPersist::kLegacyPrevPaths[i], SettingsPersist::kSettingsPath);
        EXPECT_STRNE(SettingsPersist::kLegacyPrevPaths[i], SettingsPersist::kScratchPrevPath);
    }
}

TEST(SettingsField, RejectsAnEmptyOrMissingPath)
{
    for (int slot = 0; slot < field_cases::kPathSlots; ++slot) {
        EXPECT_FALSE(SettingsPersist::isWellFormed(field_cases::with(field_cases::kGood, slot, "")));
        EXPECT_FALSE(
            SettingsPersist::isWellFormed(field_cases::with(field_cases::kGood, slot, nullptr)));
    }
}

/// A path longer than the File object's own buffer is truncated silently by
/// setPath, and surfaces only as a primitive check failing for no stated
/// reason.
TEST(SettingsField, RejectsAScratchPathTooLongForTheFileObject)
{
    const std::string atLimit("2:/" + std::string(SettingsPersist::kMaxScratchPathBytes - 3, 'x'));
    const std::string overLimit(atLimit + "x");
    ASSERT_EQ(atLimit.size(), SettingsPersist::kMaxScratchPathBytes);

    for (int slot = 0; slot < field_cases::kPathSlots; ++slot) {
        EXPECT_TRUE(SettingsPersist::isWellFormed(
            field_cases::with(field_cases::kGood, slot, atLimit.c_str())))
            << "slot " << slot;
        EXPECT_FALSE(SettingsPersist::isWellFormed(
            field_cases::with(field_cases::kGood, slot, overLimit.c_str())))
            << "slot " << slot;
    }
}

/// The probe text is read back into a fixed buffer, so one that does not fit is
/// a stack overrun inside the function that gates every raw-address write.
TEST(SettingsField, RejectsAProbeTextThatWouldNotFitTheReadBuffer)
{
    const std::string atLimit(SettingsPersist::kMaxProbeTextBytes, 'x');
    const std::string overLimit(SettingsPersist::kMaxProbeTextBytes + 1, 'x');

    EXPECT_TRUE(
        SettingsPersist::isWellFormed(field_cases::with(field_cases::kGood, 9, atLimit.c_str())));
    EXPECT_FALSE(
        SettingsPersist::isWellFormed(field_cases::with(field_cases::kGood, 9, overLimit.c_str())));
}

/// The capacity the write path actually passes, against the cap the read path
/// actually enforces. Every splice case above chooses its own capacity, which
/// is why none of them could see that the write path was handing the splice the
/// size of the buffer -- producing a file that commits and is then refused by
/// every later read. This drives `spliceWithinReadCap`, which is the line that
/// was wrong.
TEST(SettingsLimits, ASpliceIsCappedAtWhatTheReaderWillAccept)
{
    EXPECT_LE(SettingsPersist::kMaxSettingsFileSize, SettingsPersist::kBufferCapacity);

    constexpr SettingsPersist::Field kUnits = {
        .splice     = &SettingsSplice::setUnits,
        .name       = "units",
        .probeAPath = "2:/a.tmp",
        .probeBPath = "2:/b.tmp",
        .probeText  = "probe",
    };

    const std::string head = R"({"units":"metric","pad":")";
    const std::string tail = R"(","version":2})";
    for (size_t len : {SettingsPersist::kMaxSettingsFileSize - 1,
                       SettingsPersist::kMaxSettingsFileSize}) {
        std::string in = head + std::string(len - head.size() - tail.size(), 'x') + tail;
        ASSERT_EQ(in.size(), len);

        char buf[SettingsPersist::kBufferCapacity] = {};
        std::memcpy(buf, in.data(), in.size());
        size_t n = in.size();

        EXPECT_EQ(SettingsPersist::spliceWithinReadCap(kUnits, buf, n, true, nullptr),
                  SettingsSplice::Result::WouldNotFit)
            << "len " << len;
        EXPECT_EQ(n, len) << "len " << len;
        EXPECT_EQ(std::string(buf, n), in) << "len " << len;
    }

    // ...and one under the cap still goes through, so the guard is not simply
    // refusing everything.
    std::string ok = head + std::string(400 - head.size() - tail.size(), 'x') + tail;
    char buf[SettingsPersist::kBufferCapacity] = {};
    std::memcpy(buf, ok.data(), ok.size());
    size_t n = ok.size();
    EXPECT_EQ(SettingsPersist::spliceWithinReadCap(kUnits, buf, n, true, nullptr),
              SettingsSplice::Result::Ok);
    EXPECT_EQ(n, ok.size() + 2);
}

/// The same arithmetic for the one-byte field, at its own boundary.
TEST(SettingsLimits, TheOneByteGrowthIsCappedTheSameWay)
{
    const std::string head = R"({"phone":{"notifications":true},"pad":")";
    const std::string tail = R"(","version":2})";
    const size_t len = SettingsPersist::kMaxSettingsFileSize;
    std::string in = head + std::string(len - head.size() - tail.size(), 'x') + tail;
    ASSERT_EQ(in.size(), len);

    char buf[SettingsPersist::kBufferCapacity] = {};
    std::memcpy(buf, in.data(), in.size());
    size_t n = in.size();

    constexpr SettingsPersist::Field kNotify = {
        .splice     = &SettingsSplice::setNotifications,
        .name       = "notifications",
        .probeAPath = "2:/a.tmp",
        .probeBPath = "2:/b.tmp",
        .probeText  = "probe",
    };
    EXPECT_EQ(SettingsPersist::spliceWithinReadCap(kNotify, buf, n, false, nullptr),
              SettingsSplice::Result::WouldNotFit);
    EXPECT_EQ(std::string(buf, n), in);
}

/// A `units` key one level down is not the one the kernel parses. Rewriting it
/// would still pass the readback, because that compares the file against the
/// buffer just written -- so the wrong edit would be reported as saved.
TEST(SettingsUnits, IgnoresAUnitsKeyNestedInsideAnotherObject)
{
    const auto out = spliceUnits(
        R"({"phone":{"notifications":false,"units":"metric"},"units":"metric"})", true);
    ASSERT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text,
              R"({"phone":{"notifications":false,"units":"metric"},"units":"imperial"})");
}

TEST(SettingsUnits, RefusesWhenTheOnlyUnitsKeyIsNested)
{
    EXPECT_EQ(spliceUnits(R"({"phone":{"units":"metric"},"version":2})", true).result,
              SettingsSplice::Result::FieldNotFound);
}

/// An array at the top level must not be mistaken for an object that could
/// hold the key one level down.
TEST(SettingsUnits, LooksPastAStringInsideATopLevelArray)
{
    const auto out = spliceUnits(R"({"tags":["units","metric"],"units":"metric"})", true);
    ASSERT_EQ(out.result, SettingsSplice::Result::Ok);
    EXPECT_EQ(out.text, R"({"tags":["units","metric"],"units":"imperial"})");
}

/// The two fields are spliced by different functions over the same buffer, and
/// neither may disturb the other's bytes.
TEST(SettingsUnits, DoesNotDisturbTheNotificationsFlag)
{
    const auto units = spliceUnits(kRealMetric, true);
    ASSERT_EQ(units.result, SettingsSplice::Result::Ok);
    EXPECT_NE(units.text.find("\"notifications\":false"), std::string::npos);

    const auto notifications = splice(kRealMetric, true);
    ASSERT_EQ(notifications.result, SettingsSplice::Result::Ok);
    EXPECT_NE(notifications.text.find("\"units\":\"metric\""), std::string::npos);
}

} // namespace
