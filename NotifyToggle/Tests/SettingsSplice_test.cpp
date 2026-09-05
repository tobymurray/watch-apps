#include "SettingsSplice.hpp"

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

} // namespace
