/**
 ******************************************************************************
 * @file    Service_test.cpp
 * @brief   The joins: a file on the watch's storage becoming a message the
 *          GUI can draw.
 ******************************************************************************
 *
 * Every other test in this suite is about a part. This one runs the real
 * `Service` against a scripted kernel and looks at what came out, because the
 * failure this app is most exposed to is not a wrong barcode -- the encoder
 * tests cover that -- it is a *correct* id that never reaches the screen.
 * Nothing in a unit test sees that, and on the watch it looks identical to
 * having no file at all.
 *
 * See ServiceHarness.hpp for why the harness exists and what SleepLab's dead
 * glance has to do with it.
 *
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ServiceHarness.hpp"

namespace {

using BarcodeTest::document;
using BarcodeTest::Harness;

// ---------------------------------------------------------------------------
// The id gets out
// ---------------------------------------------------------------------------

TEST(Service, PublishesTheIdWhenTheGuiStarts)
{
    Harness h;
    h.seed(document("A1234567"));
    h.comm.queueGuiRun();
    h.comm.queueStop();
    h.run();

    ASSERT_FALSE(h.comm.ranDry);
    ASSERT_EQ(h.published().size(), 1u) << "the GUI starting must be answered exactly once";
    EXPECT_EQ(h.publishedProblem(0), Barcode::Problem::None);
    EXPECT_EQ(h.publishedId(0), "A1234567");
}

TEST(Service, ReadsTheFileBeforeTheGuiEverAsks)
{
    // run() refreshes and adopts up front, so the first publish already
    // carries the id rather than a placeholder the GUI would have to re-ask
    // for.
    Harness h;
    h.seed(document("Z99"));
    h.comm.queueGuiRun();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 1u);
    EXPECT_EQ(h.publishedId(0), "Z99");
}

TEST(Service, AnswersEveryRequestEvenWhenNothingChanged)
{
    // The GUI asks on every resume and needs an answer each time -- a service
    // that only replied on change would leave a resumed screen blank.
    Harness h;
    h.seed(document("A1234567"));
    h.comm.queueGuiRun();
    h.comm.queueRequest();
    h.comm.queueRequest();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 3u);
    for (size_t i = 0; i < 3; i++) {
        EXPECT_EQ(h.publishedProblem(i), Barcode::Problem::None) << "publish " << i;
        EXPECT_EQ(h.publishedId(i), "A1234567") << "publish " << i;
    }
}

TEST(Service, PicksUpANewIdOnRequestWithoutRestarting)
{
    // Overwriting input.json and relaunching is the documented flow, but the
    // GUI resuming is supposed to be enough on its own. The file is swapped
    // *between* the two messages, which is the situation BARCODE_REQUEST
    // exists for. Different length, so the fake filesystem can see the change
    // at all, since the fake filesystem notices a change by size.
    Harness h;
    h.seed(document("A1"));
    h.comm.queueGuiRun();
    h.comm.queueAction([&h] { h.seed(document("B22")); });
    h.comm.queueRequest();
    h.comm.queueStop();
    h.run();

    ASSERT_FALSE(h.comm.ranDry);
    ASSERT_EQ(h.published().size(), 2u);
    EXPECT_EQ(h.publishedId(0), "A1") << "what was there when the GUI started";
    EXPECT_EQ(h.publishedId(1), "B22") << "and what replaced it, without a restart";
}

TEST(Service, AFileArrivingWhileRunningIsPickedUpOnRequest)
{
    // The first launch on a fresh install: the app is open saying there is no
    // id, the user writes the file, and the screen has to catch up.
    Harness h;
    h.comm.queueGuiRun();
    h.comm.queueAction([&h] { h.seed(document("A1234567")); });
    h.comm.queueRequest();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 2u);
    EXPECT_EQ(h.publishedProblem(0), Barcode::Problem::NoConfig);
    EXPECT_EQ(h.publishedProblem(1), Barcode::Problem::None);
    EXPECT_EQ(h.publishedId(1), "A1234567");
}

TEST(Service, AFileGoingBadWhileRunningStopsShowingTheOldBarcode)
{
    // The harmful direction. If the file is replaced with something unusable,
    // the app must stop drawing the id it used to have -- a stale barcode
    // still scans, as the wrong person.
    Harness h;
    h.seed(document("A1234567"));
    h.comm.queueGuiRun();
    h.comm.queueAction([&h] { h.seed("{ \"schema\": 1, \"values\": { } }"); });
    h.comm.queueRequest();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 2u);
    EXPECT_EQ(h.publishedId(0), "A1234567");
    EXPECT_EQ(h.publishedProblem(1), Barcode::Problem::NoValue);
    EXPECT_EQ(h.publishedId(1), "") << "the old id must not survive the file going bad";
}

TEST(Service, TheFileBeingDeletedWhileRunningIsNoticed)
{
    Harness h;
    h.seed(document("A1234567"));
    h.comm.queueGuiRun();
    h.comm.queueAction([&h] { h.removeFile(); });
    h.comm.queueRequest();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 2u);
    EXPECT_EQ(h.publishedProblem(1), Barcode::Problem::NoConfig);
    EXPECT_EQ(h.publishedId(1), "");
}

// ---------------------------------------------------------------------------
// Every reason there is no id reaches the GUI as itself
//
// The screen names the file and the key that fixes it, so the service must not
// flatten these into one "no id" -- that is the difference between a legible
// failure and a blank white box.
// ---------------------------------------------------------------------------

TEST(Service, NoFilePublishesNoFile)
{
    Harness h;
    h.comm.queueGuiRun();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 1u);
    EXPECT_EQ(h.publishedProblem(0), Barcode::Problem::NoConfig);
    EXPECT_EQ(h.publishedId(0), "") << "no id may ever ride along with a problem";
}

TEST(Service, OversizedPublishesTooLarge)
{
    Harness h;
    h.seed(std::string(SDK::AppConfig::skMaxFileBytes + 1, 'x'));
    h.comm.queueGuiRun();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 1u);
    EXPECT_EQ(h.publishedProblem(0), Barcode::Problem::NoConfig);
}

TEST(Service, MalformedPublishesNotJson)
{
    Harness h;
    h.seed("{ \"schema\": 1, ");
    h.comm.queueGuiRun();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 1u);
    EXPECT_EQ(h.publishedProblem(0), Barcode::Problem::NoConfig);
}

TEST(Service, AnUnknownSchemaPublishesWrongSchema)
{
    Harness h;
    h.seed("{ \"schema\": 2, \"values\": { \"id\": \"A1\" } }");
    h.comm.queueGuiRun();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 1u);
    EXPECT_EQ(h.publishedProblem(0), Barcode::Problem::NoConfig);
}

TEST(Service, AMissingKeyPublishesNoValueNotBadValue)
{
    // The two are told apart by has(): an incomplete file and a wrong one need
    // different things said about them.
    Harness h;
    h.seed("{ \"schema\": 1, \"values\": { } }");
    h.comm.queueGuiRun();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 1u);
    EXPECT_EQ(h.publishedProblem(0), Barcode::Problem::NoValue);
}

TEST(Service, APresentButUnusableValuePublishesBadValue)
{
    Harness h;
    h.seed(document("01234567890123456")); // 17 characters
    h.comm.queueGuiRun();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 1u);
    EXPECT_EQ(h.publishedProblem(0), Barcode::Problem::BadValue);
    EXPECT_EQ(h.publishedId(0), "") << "a refused id must not be published anyway";
}

TEST(Service, ANonStringIdIsRefusedRatherThanCoerced)
{
    // This is the app's worst-harm case and it used to be real. The old
    // InputConfig::getString() never checked the JSON *type* of a value, so
    // `"id": null` was accepted and its raw text became the id -- four letters
    // that scan. `"id": 1234567` gave the digits the user meant.
    //
    // SDK::AppConfig refuses both: the value is not a string, so no code is
    // adopted and the state says NoValue. Pinned here because the harm was
    // specific -- a plausible barcode that scans as something nobody entered --
    // and because it is now the SDK's promise rather than this app's.
    //
    // The cost is that an unquoted number is refused too, where it once worked.
    // A wearer who writes 1234567 without quotes gets "no codes yet" rather
    // than a wrong barcode, which is the right way round.
    for (const char *value : {"null", "1234567", "true", "[]", "{}"}) {
        Harness h;
        h.seed(std::string("{\n  \"schema\": 1,\n  \"values\": {\n    \"id1\": ")
               + value + "\n  }\n}\n");
        h.comm.queueGuiRun();
        h.comm.queueStop();
        h.run();

        ASSERT_EQ(h.published().size(), 1u) << value;
        EXPECT_EQ(h.publishedCount(0), 0) << value << " was adopted as a code";
        EXPECT_EQ(h.publishedId(0), "") << value << " reached the GUI as an id";
        EXPECT_EQ(h.publishedProblem(0), Barcode::Problem::NoValue) << value;
    }
}

TEST(Service, PublishesEverySlotInOrder)
{
    // The six-code capability from 84b9f03 had no test at all: the GUI cycles
    // codes[0..count) in order, so the order the service compacts them into is
    // the order a wearer sees.
    Harness h;
    h.seed(BarcodeTest::documentWithCodes({"A1234567", "B7654321", "2005812"},
                             {"Toby", "Sam", "Kids Card"}));
    h.comm.queueGuiRun();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 1u);
    EXPECT_EQ(h.publishedProblem(0), Barcode::Problem::None);
    EXPECT_EQ(h.publishedCount(0), 3);
    EXPECT_EQ(h.publishedIds(0), (std::vector<std::string>{"A1234567", "B7654321", "2005812"}));
}

TEST(Service, CompactsPastEmptyAndUnusableSlots)
{
    // Barcode.hpp promises codes[0..count) are all drawable, so the GUI can
    // cycle without landing on a gap. Slot 2 is empty and slot 4 is too long,
    // so what comes out is slots 1, 3 and 5 with no holes -- and the state
    // carries no problem, because the usable ones are usable.
    Harness h;
    h.seed(BarcodeTest::documentWithCodes({"A1111111", "", "B2222222", "01234567890123456", "C3333333"}));
    h.comm.queueGuiRun();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 1u);
    EXPECT_EQ(h.publishedCount(0), 3);
    EXPECT_EQ(h.publishedIds(0),
              (std::vector<std::string>{"A1111111", "B2222222", "C3333333"}));
}

TEST(Service, APresentButEmptyIdPublishesNotSet)
{
    // Present and empty is NotSet, not BadValue: an empty slot is skipped
    // rather than refused, and a slot that carried a value at all -- even an
    // empty one -- means somebody has seen the form. Service.cpp puts it as
    // "the form was filled in and left empty, which is what accepting a
    // required field's pre-filled default looks like from here".
    //
    // This test expected BadValue until the SDK::AppConfig migration, when the
    // distinction between "no key" and "empty key" became one the config API
    // can actually report. NoValue is the no-key case; see above.
    Harness h;
    h.seed(document(""));
    h.comm.queueGuiRun();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 1u);
    EXPECT_EQ(h.publishedProblem(0), Barcode::Problem::NotSet);
    EXPECT_EQ(h.publishedId(0), "");
}

TEST(Service, AnIdTheEncoderRefusesPublishesBadValue)
{
    // adopt() probes Code128::encode() before accepting, so the reader's rules
    // and the encoder's cannot drift apart into a blank white box. A tab is the
    // case that separates them: it is a character, so nothing about length or
    // presence rejects it, and it is below the printable range the encoder
    // accepts. app-manifest.json's pattern would stop it coming from the phone;
    // a hand-written file is not checked against that pattern.
    Harness h;
    h.seed(document("\\tABC"));
    h.comm.queueGuiRun();
    h.comm.queueStop();
    h.run();

    ASSERT_EQ(h.published().size(), 1u);
    EXPECT_EQ(h.publishedProblem(0), Barcode::Problem::BadValue);
    EXPECT_EQ(h.publishedId(0), "") << "a refused id must not be published anyway";
}

/// A problem state never carries an id, at any length, for any reason. This is
/// the harmful case from the README stated as a property over the whole set:
/// a stale barcode next to a message saying there is no id would still scan.
TEST(Service, NoPublishedStateEverPairsAProblemWithAnId)
{
    const char *kDocuments[] = {
        "",
        "not json",
        "{ \"schema\": 2, \"values\": { \"id\": \"A1\" } }",
        "{ \"schema\": 1, \"values\": { } }",
        "{ \"schema\": 1, \"values\": { \"id\": \"\" } }",
        "{ \"schema\": 1, \"values\": { \"id\": \"01234567890123456\" } }",
        "{ \"schema\": 1, \"values\": { \"id\": \"A\\\\B\" } }",
    };

    for (const char *doc : kDocuments) {
        Harness h;
        h.seed(doc);
        h.comm.queueGuiRun();
        h.comm.queueStop();
        h.run();

        ASSERT_EQ(h.published().size(), 1u) << doc;
        if (h.publishedProblem(0) != Barcode::Problem::None) {
            EXPECT_EQ(h.publishedId(0), "") << doc;
        }
    }
}

// ---------------------------------------------------------------------------
// The loop
// ---------------------------------------------------------------------------

TEST(Service, StopEndsTheLoopWithoutPublishing)
{
    Harness h;
    h.seed(document("A1234567"));
    h.comm.queueStop();
    h.run();

    EXPECT_FALSE(h.comm.ranDry);
    EXPECT_TRUE(h.published().empty()) << "nothing asked, nothing sent";
}

TEST(Service, TheGuiGoingAwayEndsTheLoop)
{
    // Nothing here changes on its own, so the service exits with the GUI
    // rather than staying resident.
    Harness h;
    h.seed(document("A1234567"));
    h.comm.queueGuiRun();
    h.comm.queueGuiStop();
    h.run();

    EXPECT_FALSE(h.comm.ranDry) << "the service should have returned on GUI_STOP";
    EXPECT_EQ(h.published().size(), 1u);
}

TEST(Service, AnUnknownMessageIsIgnoredAndNotAnswered)
{
    Harness h;
    h.seed(document("A1234567"));
    h.comm.queueUnknown();
    h.comm.queueStop();
    h.run();

    EXPECT_FALSE(h.comm.ranDry);
    EXPECT_TRUE(h.published().empty())
        << "a message the service has no case for must not provoke a publish";
}

} // namespace
