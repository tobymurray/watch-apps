#include "SettingsSplice.hpp"

#include "EditableFields.hpp"
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

namespace
{

/// A value that runs to the last byte read is a truncated file, and the
/// firmware's own reader would refuse or misread it. Rewriting one commits a
/// shorter truncation, and the readback confirms it because it compares the
/// file against the buffer just written.
TEST(SettingsShape, RefusesAValueThatReachesTheEndOfTheBuffer)
{
    char buf[kCapacity] = {};
    const std::string input = R"({"height":190)";
    std::memcpy(buf, input.data(), input.size());
    size_t len = input.size();

    EXPECT_EQ(SettingsSplice::setUnsigned(buf, len, kCapacity, {nullptr, "height"}, 183u),
              SettingsSplice::Result::FieldNotFound);
    EXPECT_EQ(std::string(buf, len), input);
}

/// `false` and `falsey` share a prefix, and matching the prefix alone rewrote
/// the latter to `truey`: not valid JSON, ignored by the kernel's own bool
/// reader, and reported as saved by the commit.
TEST(SettingsShape, RefusesABooleanThatIsOnlyAPrefixOfTheToken)
{
    for (const char *file : {R"({"phone":{"notifications":falsey}})",
                             R"({"phone":{"notifications":truely}})"}) {
        EXPECT_EQ(splice(file, true).result, SettingsSplice::Result::FieldNotFound) << file;
    }
}

} // namespace

namespace
{

using SettingsPersist::FieldDescriptor;

/// Splices `f`'s value through the read cap, the way the write path does.
struct FieldSplice {
    SettingsSplice::Result result;
    std::string text;
    size_t at;
};

FieldSplice spliceField(const std::string &input, const FieldDescriptor &f, const char *token)
{
    char buf[SettingsPersist::kBufferCapacity] = {};
    std::memcpy(buf, input.data(), input.size());
    size_t len = input.size();
    size_t at  = 0;
    const auto result = SettingsPersist::spliceWithinReadCap(f, buf, len, token,
                                                             std::strlen(token), &at);
    return {result, std::string(buf, len), at};
}

/// Asserts that the only bytes that changed are the ones at the offset the
/// splice reported, and that the rest of the file moved by the length delta and
/// nothing more. This is the whole promise of the mechanism -- a real personal
/// settings file comes back byte for byte apart from the one value asked for --
/// and it holds for every field without a hand-written expectation per field.
void expectOnlyTheReportedValueChanged(const std::string &before, const FieldSplice &out,
                                       const char *token)
{
    ASSERT_EQ(out.result, SettingsSplice::Result::Ok);
    const size_t newLen = std::strlen(token);
    ASSERT_GE(newLen + before.size(), out.text.size());
    const size_t oldLen = newLen + before.size() - out.text.size();
    ASSERT_LE(out.at + oldLen, before.size());

    const std::string expected =
        before.substr(0, out.at) + token + before.substr(out.at + oldLen);
    EXPECT_EQ(out.text, expected);
}

TEST(SettingsFields, EveryEditableFieldLeavesTheRestOfTheRealFileAlone)
{
    const struct {
        const FieldDescriptor &field;
        const char *token;
    } cases[] = {
        {SettingsPersist::fields::kUnits, "\"imperial\""},
        {SettingsPersist::fields::kNotifications, "true"},
        {SettingsPersist::fields::kActivityMinutes, "45"},
        {SettingsPersist::fields::kSteps, "12000"},
        {SettingsPersist::fields::kFloors, "12"},
        {SettingsPersist::fields::kHeight, "183"},
        {SettingsPersist::fields::kWeight, "75"},
        {SettingsPersist::fields::kMaxHeartRate, "[92,110,129,147,166,184]"},
    };

    for (const auto &c : cases) {
        const auto out = spliceField(kRealFile, c.field, c.token);
        SCOPED_TRACE(c.field.name);
        expectOnlyTheReportedValueChanged(kRealFile, out, c.token);
    }
}

/// The personal fields the kit cannot name are the reason the file is spliced
/// rather than regenerated, and the kernel's own `save()` would drop both of
/// these -- it writes no `gender` and no `dateOfBirth` key at all.
TEST(SettingsFields, TheFieldsThisAppRefusesSurviveEveryEditItMakes)
{
    for (const auto *f : SettingsPersist::kEditable) {
        const char *token = f->shape == SettingsSplice::JsonShape::NumberArray6
                                ? "[92,110,129,147,166,184]"
                                : (f->shape == SettingsSplice::JsonShape::UnitsToken
                                       ? "\"imperial\""
                                       : (f->shape == SettingsSplice::JsonShape::Boolean ? "true"
                                                                                         : "7"));
        const auto out = spliceField(kRealFile, *f, token);
        SCOPED_TRACE(f->name);
        ASSERT_EQ(out.result, SettingsSplice::Result::Ok);
        EXPECT_NE(out.text.find(R"("gender":"M")"), std::string::npos);
        EXPECT_NE(out.text.find(R"("dateOfBirth":"1990-01-01")"), std::string::npos);
        EXPECT_NE(out.text.find(R"("watchFaceId":0)"), std::string::npos);
        EXPECT_NE(out.text.find(R"("version":2)"), std::string::npos);
    }
}

TEST(SettingsNumbers, RefusesATokenTheParserWouldNotHaveWritten)
{
    for (const char *value : {"+5", ".5", "5.", "1e3", "--1", "true", "null", "\"190\"",
                              "0x10", "1 0", "190x"}) {
        const std::string file = std::string(R"({"height":)") + value + R"(,"version":2})";
        char buf[kCapacity] = {};
        std::memcpy(buf, file.data(), file.size());
        size_t len = file.size();
        EXPECT_EQ(SettingsSplice::setUnsigned(buf, len, kCapacity, {nullptr, "height"}, 183u),
                  SettingsSplice::Result::FieldNotFound)
            << value;
        EXPECT_EQ(std::string(buf, len), file) << value;
    }
}

/// A fraction is a token this understands and replaces, not one it refuses:
/// `weight` is read with `strtod`, so a wearer's phone may have written one,
/// and refusing it would make the field uneditable rather than safe.
TEST(SettingsNumbers, ReplacesAFractionalWeightWithAWholeOne)
{
    const std::string file = R"({"weight":90.5,"version":2})";
    char buf[kCapacity] = {};
    std::memcpy(buf, file.data(), file.size());
    size_t len = file.size();

    ASSERT_EQ(SettingsSplice::setUnsigned(buf, len, kCapacity, {nullptr, "weight"}, 75u),
              SettingsSplice::Result::Ok);
    EXPECT_EQ(std::string(buf, len), R"({"weight":75,"version":2})");
}

/// A negative number is a shape this can replace. The file holding one is how a
/// wearer would find their way to a screen that can fix it; refusing the token
/// would leave them with no way to.
TEST(SettingsNumbers, ReplacesANegativeNumber)
{
    const std::string file = R"({"height":-5,"version":2})";
    char buf[kCapacity] = {};
    std::memcpy(buf, file.data(), file.size());
    size_t len = file.size();

    ASSERT_EQ(SettingsSplice::setUnsigned(buf, len, kCapacity, {nullptr, "height"}, 183u),
              SettingsSplice::Result::Ok);
    EXPECT_EQ(std::string(buf, len), R"({"height":183,"version":2})");
}

TEST(SettingsNumbers, MovesTheTailCorrectlyInBothDirections)
{
    const std::string tail = R"(,"weight":90,"version":2})";
    char buf[kCapacity] = {};
    const std::string grown = R"({"height":9)" + tail;
    std::memcpy(buf, grown.data(), grown.size());
    size_t len = grown.size();

    ASSERT_EQ(SettingsSplice::setUnsigned(buf, len, kCapacity, {nullptr, "height"}, 190u),
              SettingsSplice::Result::Ok);
    EXPECT_EQ(std::string(buf, len), R"({"height":190)" + tail);

    ASSERT_EQ(SettingsSplice::setUnsigned(buf, len, kCapacity, {nullptr, "height"}, 9u),
              SettingsSplice::Result::Ok);
    EXPECT_EQ(std::string(buf, len), grown);
}

/// The same depth discipline `phone.notifications` needed. A `steps` key one
/// level deeper inside `dailyGoals` is not the one the kernel parses, and the
/// readback would confirm the wrong edit.
TEST(SettingsNumbers, IgnoresAGoalKeyNestedInsideTheGoalsObject)
{
    const std::string file = R"({"dailyGoals":{"last":{"steps":1},"steps":5000}})";
    char buf[kCapacity] = {};
    std::memcpy(buf, file.data(), file.size());
    size_t len = file.size();

    ASSERT_EQ(SettingsSplice::setUnsigned(buf, len, kCapacity, {"dailyGoals", "steps"}, 12000u),
              SettingsSplice::Result::Ok);
    EXPECT_EQ(std::string(buf, len), R"({"dailyGoals":{"last":{"steps":1},"steps":12000}})");
}

TEST(SettingsNumbers, IgnoresAGoalKeyOutsideTheGoalsObject)
{
    const std::string file = R"({"steps":1,"dailyGoals":{"steps":5000}})";
    char buf[kCapacity] = {};
    std::memcpy(buf, file.data(), file.size());
    size_t len = file.size();

    ASSERT_EQ(SettingsSplice::setUnsigned(buf, len, kCapacity, {"dailyGoals", "steps"}, 12000u),
              SettingsSplice::Result::Ok);
    EXPECT_EQ(std::string(buf, len), R"({"steps":1,"dailyGoals":{"steps":12000}})");
}

TEST(SettingsNumbers, RefusesWhenTheGoalsObjectIsAbsent)
{
    char buf[kCapacity] = {};
    const std::string file = R"({"steps":5000,"version":2})";
    std::memcpy(buf, file.data(), file.size());
    size_t len = file.size();
    EXPECT_EQ(SettingsSplice::setUnsigned(buf, len, kCapacity, {"dailyGoals", "steps"}, 12000u),
              SettingsSplice::Result::FieldNotFound);
}

} // namespace

namespace
{

/// The firmware's own default ladder, read out of the settings constructor at
/// `0x080abbb4`: a word store of `0x9885725F` at `structBase+0x10` followed by a
/// halfword store of `0xBEAB`, which is the six bytes 95, 114, 133, 152, 171,
/// 190. Those are 50/60/70/80/90/100% of 190 exactly.
///
/// This is the whole argument for one control instead of six: the rule is the
/// kernel's, and reproducing it means writing what the watch itself would.
/// Falsified by a firmware whose default ladder is a different set of
/// percentages.
TEST(SettingsZoneLadder, MatchesTheFirmwareDefault)
{
    char token[SettingsSplice::kMaxTokenBytes] = {};
    const size_t n = SettingsSplice::format::zoneLadder(token, sizeof(token), 190u);
    EXPECT_EQ(std::string(token, n), "[95,114,133,152,171,190]");
}

/// The ladder measured on this watch, from `Spin`'s own recovery log: a real
/// maximum of 184, reported through `RequestSystemSettings` as
/// `[92,110,129,147,166,184]`.
TEST(SettingsZoneLadder, MatchesTheLadderMeasuredOnAWatch)
{
    char token[SettingsSplice::kMaxTokenBytes] = {};
    const size_t n = SettingsSplice::format::zoneLadder(token, sizeof(token), 184u);
    EXPECT_EQ(std::string(token, n), "[92,110,129,147,166,184]");
}

/// Zone *N* runs from its own floor to the next, so two equal floors are a zone
/// no heart rate can be in. This is where `kMaxHeartRate`'s lower bound comes
/// from, rather than from taste.
TEST(SettingsZoneLadder, EveryLadderInTheWritableRangeStrictlyIncreases)
{
    for (uint32_t maxHr = SettingsPersist::fields::kMaxHeartRate.writeMin;
         maxHr <= SettingsPersist::fields::kMaxHeartRate.writeMax; ++maxHr) {
        char token[SettingsSplice::kMaxTokenBytes] = {};
        const size_t n =
            SettingsSplice::format::zoneLadder(token, sizeof(token), static_cast<uint8_t>(maxHr));
        ASSERT_GT(n, 0u) << maxHr;

        std::string text(token, n);
        ASSERT_EQ(text.front(), '[');
        ASSERT_EQ(text.back(), ']');
        int previous = -1;
        size_t at = 1;
        int values = 0;
        while (at < text.size()) {
            const size_t stop = text.find_first_of(",]", at);
            const int value = std::stoi(text.substr(at, stop - at));
            EXPECT_GT(value, previous) << "maxHr " << maxHr << " ladder " << text;
            previous = value;
            ++values;
            at = stop + 1;
        }
        EXPECT_EQ(values, 6) << text;
        EXPECT_EQ(previous, static_cast<int>(maxHr)) << "the last value is the maximum";
    }
}

/// A ladder is at most `[255,255,255,255,255,255]`, and the token buffer every
/// caller declares is sized from that constant.
TEST(SettingsZoneLadder, EveryLadderFitsTheTokenBuffer)
{
    for (uint32_t maxHr = 0; maxHr <= 255u; ++maxHr) {
        char token[SettingsSplice::kMaxTokenBytes] = {};
        EXPECT_GT(SettingsSplice::format::zoneLadder(token, sizeof(token),
                                                     static_cast<uint8_t>(maxHr)),
                  0u)
            << maxHr;
    }
}

/// Left as it was, not half written: a caller that ignored the length would
/// otherwise splice part of a ladder.
TEST(SettingsZoneLadder, LeavesABufferItWouldOverrunUntouched)
{
    char token[8] = {};
    EXPECT_EQ(SettingsSplice::format::zoneLadder(token, sizeof(token), 184u), 0u);
    for (char c : token) {
        EXPECT_EQ(c, '\0');
    }
}

/// The pulled file's `[30,60,70,80,90,100]` is 20 bytes and the real ladder
/// `[92,110,129,147,166,184]` is 24, so this is the largest length delta any
/// field here moves -- four bytes against the two `units` moves and the one a
/// boolean does.
TEST(SettingsZoneLadder, ReplacesTheWholeArrayInTheRealFile)
{
    char buf[SettingsPersist::kBufferCapacity] = {};
    const std::string input = kRealFile;
    std::memcpy(buf, input.data(), input.size());
    size_t len = input.size();

    size_t at = 0;
    ASSERT_EQ(SettingsSplice::setZoneLadder(buf, len, SettingsPersist::kMaxSettingsFileSize, 184u,
                                            &at),
              SettingsSplice::Result::Ok);
    EXPECT_EQ(at, input.find("[30,"));
    EXPECT_EQ(len, input.size() + 4);
    EXPECT_NE(std::string(buf, len).find("\"heartRateZones\":[92,110,129,147,166,184],"),
              std::string::npos);
}

TEST(SettingsZoneLadder, RoundTripsBackToTheOriginal)
{
    char buf[SettingsPersist::kBufferCapacity] = {};
    const std::string input = kRealFile;
    std::memcpy(buf, input.data(), input.size());
    size_t len = input.size();

    ASSERT_EQ(SettingsSplice::setZoneLadder(buf, len, kCapacity, 184u),
              SettingsSplice::Result::Ok);
    ASSERT_EQ(SettingsSplice::setZoneLadder(buf, len, kCapacity, 100u),
              SettingsSplice::Result::Ok);
    // Not the file it started as: `[30,60,...]` is not a ladder the watch's own
    // rule can produce, which is exactly why the editor has to refuse to open a
    // field holding one rather than flatten it.
    EXPECT_EQ(std::string(buf, len),
              std::string(kRealFile).replace(std::string(kRealFile).find("[30,"),
                                             std::strlen("[30,60,70,80,90,100]"),
                                             "[50,60,70,80,90,100]"));
}

TEST(SettingsZoneLadder, RefusesAnArrayThatIsNotSixValues)
{
    for (const char *array : {"[30,60,70,80,90]", "[30,60,70,80,90,100,110]", "[]", "[30]",
                              R"(["a","b","c","d","e","f"])", "30"}) {
        const std::string file =
            std::string(R"({"heartRateZones":)") + array + R"(,"version":2})";
        char buf[kCapacity] = {};
        std::memcpy(buf, file.data(), file.size());
        size_t len = file.size();
        EXPECT_EQ(SettingsSplice::setZoneLadder(buf, len, kCapacity, 184u),
                  SettingsSplice::Result::FieldNotFound)
            << array;
        EXPECT_EQ(std::string(buf, len), file) << array;
    }
}

TEST(SettingsZoneLadder, ToleratesWhitespaceInsideTheArray)
{
    const std::string file = R"({"heartRateZones":[ 30, 60 ,70,80,90,100 ],"version":2})";
    char buf[kCapacity] = {};
    std::memcpy(buf, file.data(), file.size());
    size_t len = file.size();

    ASSERT_EQ(SettingsSplice::setZoneLadder(buf, len, kCapacity, 184u),
              SettingsSplice::Result::Ok);
    EXPECT_EQ(std::string(buf, len),
              R"({"heartRateZones":[92,110,129,147,166,184],"version":2})");
}

/// The five-byte growth is the one this cap has to catch, and it is caught at
/// the reader's limit rather than the buffer's.
TEST(SettingsZoneLadder, GrowthIsCappedAtWhatTheReaderWillAccept)
{
    const std::string head = R"({"heartRateZones":[30,60,70,80,90,100],"pad":")";
    const std::string tail = R"(","version":2})";
    const size_t len = SettingsPersist::kMaxSettingsFileSize;
    std::string in = head + std::string(len - head.size() - tail.size(), 'x') + tail;
    ASSERT_EQ(in.size(), len);

    char buf[SettingsPersist::kBufferCapacity] = {};
    std::memcpy(buf, in.data(), in.size());
    size_t n = in.size();

    EXPECT_EQ(SettingsPersist::spliceWithinReadCap(SettingsPersist::fields::kMaxHeartRate, buf, n,
                                                   "[92,110,129,147,166,184]", 24, nullptr),
              SettingsSplice::Result::WouldNotFit);
    EXPECT_EQ(std::string(buf, n), in);
}

} // namespace

namespace
{

TEST(SettingsFormat, UnsignedDecimalWritesTheWholeNumberOrNone)
{
    char out[10] = {};
    EXPECT_EQ(SettingsSplice::format::unsignedDecimal(out, sizeof(out), 0u), 1u);
    EXPECT_EQ(out[0], '0');

    EXPECT_EQ(SettingsSplice::format::unsignedDecimal(out, sizeof(out), 4294967295u), 10u);
    EXPECT_EQ(std::string(out, 10), "4294967295");

    char tight[3] = {'!', '!', '!'};
    EXPECT_EQ(SettingsSplice::format::unsignedDecimal(tight, sizeof(tight), 1000u), 0u);
    EXPECT_EQ(std::string(tight, 3), "!!!");
}

} // namespace

namespace
{

/// A `FieldDescriptor` is a name, two more `const char *`, two enums, a
/// pointer-to-member and a range: the compiler cannot tell a transposition from
/// a correct row, and `isWellFormed` is what does. Each declaration site
/// `static_assert`s it; these cases say the assertion is worth having.
constexpr FieldDescriptor kGoodDescriptor = SettingsPersist::fields::kSteps;

TEST(SettingsDescriptor, AcceptsEveryRowTheKitDeclares)
{
    for (const auto *f : SettingsPersist::kEditable) {
        EXPECT_TRUE(SettingsPersist::isWellFormed(*f)) << f->name;
    }
    EXPECT_EQ(SettingsPersist::kEditableCount, 8u);
}

TEST(SettingsDescriptor, RejectsAShapeItsLiveWidthCannotHold)
{
    FieldDescriptor f = kGoodDescriptor;
    f.width = SettingsPersist::LiveWidth::U8;
    EXPECT_FALSE(SettingsPersist::isWellFormed(f));

    f = SettingsPersist::fields::kMaxHeartRate;
    f.width = SettingsPersist::LiveWidth::U32;
    EXPECT_FALSE(SettingsPersist::isWellFormed(f));

    f = SettingsPersist::fields::kUnits;
    f.width = SettingsPersist::LiveWidth::U8x6;
    EXPECT_FALSE(SettingsPersist::isWellFormed(f));
}

/// A one-byte live field takes the low byte of whatever the file's number
/// parsed to, and the truncation is silent: 300 in the file lands as 44 in the
/// struct.
TEST(SettingsDescriptor, RejectsARangeAOneByteFieldWouldTruncate)
{
    FieldDescriptor f = SettingsPersist::fields::kMaxHeartRate;
    f.writeMax = 300u;
    EXPECT_FALSE(SettingsPersist::isWellFormed(f));
}

TEST(SettingsDescriptor, RejectsARangeNoWearerCouldMove)
{
    FieldDescriptor f = kGoodDescriptor;
    f.writeMin = 100u;
    f.writeMax = 99u;
    EXPECT_FALSE(SettingsPersist::isWellFormed(f));
}

TEST(SettingsDescriptor, RejectsAMissingNameKeyOrOffset)
{
    FieldDescriptor f = kGoodDescriptor;
    f.name = nullptr;
    EXPECT_FALSE(SettingsPersist::isWellFormed(f));

    f = kGoodDescriptor;
    f.key.leaf = nullptr;
    EXPECT_FALSE(SettingsPersist::isWellFormed(f));

    f = kGoodDescriptor;
    f.key.leaf = "";
    EXPECT_FALSE(SettingsPersist::isWellFormed(f));

    f = kGoodDescriptor;
    f.key.outer = "";
    EXPECT_FALSE(SettingsPersist::isWellFormed(f));

    f = kGoodDescriptor;
    f.offset = nullptr;
    EXPECT_FALSE(SettingsPersist::isWellFormed(f));
}

/// Every row's offset has to name a different member, or two fields write the
/// same bytes of the live struct. The pointer-to-member is what makes this
/// checkable at all -- a plain offset would be a per-firmware number frozen at
/// compile time.
TEST(SettingsDescriptor, NoTwoRowsShareALiveOffsetMember)
{
    for (size_t i = 0; i < SettingsPersist::kEditableCount; ++i) {
        for (size_t j = i + 1; j < SettingsPersist::kEditableCount; ++j) {
            EXPECT_NE(SettingsPersist::kEditable[i]->offset,
                      SettingsPersist::kEditable[j]->offset)
                << SettingsPersist::kEditable[i]->name << " and "
                << SettingsPersist::kEditable[j]->name;
        }
    }
}

/// The offered range has to sit inside whatever hard bound exists for the
/// field. Where none does, there is nothing to assert and the range is an
/// assertion recorded in the design document instead.
TEST(SettingsDescriptor, EveryOfferedRangeSitsInsideTheHardBoundForItsField)
{
    EXPECT_LE(SettingsPersist::fields::kActivityMinutes.writeMax,
              SettingsPersist::fields::kMinutesInADay);
    EXPECT_GE(SettingsPersist::fields::kMaxHeartRate.writeMin,
              SettingsPersist::fields::kSmallestSpreadableMaximum);
    EXPECT_LE(SettingsPersist::fields::kMaxHeartRate.writeMax,
              SettingsPersist::fields::kWidestStorableMaximum);
}

/// Only `phone.notifications` is written with no supported message to
/// corroborate it, and its range is the 0/1 the byte check already enforced --
/// so widening the set of editable fields does not loosen anything.
TEST(SettingsDescriptor, TheOnlyUnwitnessedWritableFieldIsStillABoolean)
{
    for (const auto *f : SettingsPersist::kEditable) {
        if (f->witness != SettingsPersist::Witness::None) {
            continue;
        }
        EXPECT_EQ(f->shape, SettingsSplice::JsonShape::Boolean) << f->name;
        EXPECT_EQ(f->writeMin, 0u) << f->name;
        EXPECT_EQ(f->writeMax, 1u) << f->name;
    }
}

} // namespace
