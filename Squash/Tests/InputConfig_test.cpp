/**
 ******************************************************************************
 * @file    InputConfig_test.cpp
 * @brief   The provisioning file that decides whether recording happens.
 ******************************************************************************
 *
 * 1.0.0 shipped the recording flag in settings.json, a file the app rewrites
 * itself and that a fresh install does not have — so the recorder could not be
 * turned on at all by anyone who installed it normally. These tests exist
 * because that failure was silent: the app started, recorded a perfectly good
 * activity, and simply wrote no IMU. Nothing observable said why.
 *
 * So the assertions here are mostly about the *negative* paths — absent,
 * malformed, wrong schema, oversized, unrecognised value — each pinned to a
 * distinguishable status rather than a bare false.
 */

#include "InputConfig.hpp"
#include "KernelTestDoubles.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using InputConfig::Status;

constexpr const char* kPath = "input.json";

std::string doc(const std::string& value, unsigned schema = 1)
{
    return "{\"schema\":" + std::to_string(schema) +
           ",\"values\":{\"record_imu\":\"" + value + "\"}}";
}

class InputConfigTest : public ::testing::Test {
protected:
    SDK::TestSupport::KernelFixture fixture;

    InputConfig::Reader reader{fixture.kernel, kPath};

    void write(const std::string& content)
    {
        fixture.fileSystem.seedFile(kPath, content);
    }
};

TEST_F(InputConfigTest, NoFileMeansOffAndSaysSo)
{
    reader.refresh();
    EXPECT_EQ(reader.status(), Status::Absent);
    EXPECT_FALSE(reader.getFlag(InputConfig::kQueryRecordImu));
}

TEST_F(InputConfigTest, ConstructionReadsNothingUntilRefresh)
{
    write(doc("on"));
    // Deliberate: the simulator constructs the app's objects before TouchGFX
    // exists, and this class logs, so reading in the constructor would take the
    // process down.
    EXPECT_EQ(reader.status(), Status::Absent);
    EXPECT_TRUE(reader.refresh());
    EXPECT_EQ(reader.status(), Status::Ok);
}

TEST_F(InputConfigTest, OnEnablesRecording)
{
    write(doc("on"));
    reader.refresh();
    ASSERT_EQ(reader.status(), Status::Ok);
    EXPECT_TRUE(reader.getFlag(InputConfig::kQueryRecordImu));
}

TEST_F(InputConfigTest, TheAffirmativeVocabularyIsAccepted)
{
    for (const char* word : {"on", "yes", "true", "1", "enabled"}) {
        write(doc(word));
        reader.refresh();
        EXPECT_TRUE(reader.getFlag(InputConfig::kQueryRecordImu)) << "word: " << word;
    }
}

TEST_F(InputConfigTest, CaseDoesNotMatter)
{
    for (const char* word : {"ON", "On", "YES", "True", "ENABLED"}) {
        write(doc(word));
        reader.refresh();
        EXPECT_TRUE(reader.getFlag(InputConfig::kQueryRecordImu)) << "word: " << word;
    }
}

TEST_F(InputConfigTest, TheNegativeVocabularyIsRejected)
{
    for (const char* word : {"off", "no", "false", "0", "disabled", "OFF"}) {
        write(doc(word));
        reader.refresh();
        EXPECT_FALSE(reader.getFlag(InputConfig::kQueryRecordImu)) << "word: " << word;
    }
}

TEST_F(InputConfigTest, AnUnrecognisedWordIsOffRatherThanOn)
{
    // Somebody typed something meaning to enable it. Off is the conservative
    // direction for a flag whose only effect is to start consuming flash.
    write(doc("banana"));
    reader.refresh();
    EXPECT_EQ(reader.status(), Status::Ok);
    EXPECT_FALSE(reader.getFlag(InputConfig::kQueryRecordImu));
}

TEST_F(InputConfigTest, AMissingKeyIsOffWithTheFileStillValid)
{
    write("{\"schema\":1,\"values\":{\"something_else\":\"on\"}}");
    reader.refresh();
    EXPECT_EQ(reader.status(), Status::Ok);
    EXPECT_FALSE(reader.has(InputConfig::kQueryRecordImu));
    EXPECT_FALSE(reader.getFlag(InputConfig::kQueryRecordImu));
}

TEST_F(InputConfigTest, AnUnknownSchemaIsRefusedRatherThanGuessedAt)
{
    write(doc("on", /*schema=*/2));
    reader.refresh();
    EXPECT_EQ(reader.status(), Status::WrongSchema);
    EXPECT_FALSE(reader.getFlag(InputConfig::kQueryRecordImu));
}

TEST_F(InputConfigTest, AMissingSchemaIsRefused)
{
    write("{\"values\":{\"record_imu\":\"on\"}}");
    reader.refresh();
    EXPECT_EQ(reader.status(), Status::WrongSchema);
    EXPECT_FALSE(reader.getFlag(InputConfig::kQueryRecordImu));
}

TEST_F(InputConfigTest, GarbageIsNotJson)
{
    write("this is not json at all");
    reader.refresh();
    EXPECT_EQ(reader.status(), Status::NotJson);
    EXPECT_FALSE(reader.getFlag(InputConfig::kQueryRecordImu));
}

TEST_F(InputConfigTest, AnEmptyFileIsNotJson)
{
    // A copy interrupted midway looks exactly like this.
    write("");
    reader.refresh();
    EXPECT_EQ(reader.status(), Status::NotJson);
}

TEST_F(InputConfigTest, AnOversizedFileIsRefusedBeforeAllocating)
{
    write(std::string(InputConfig::kMaxFileBytes + 1, 'x'));
    reader.refresh();
    EXPECT_EQ(reader.status(), Status::TooLarge);
    EXPECT_FALSE(reader.getFlag(InputConfig::kQueryRecordImu));
}

TEST_F(InputConfigTest, AValueTooLongForTheFlagBufferIsOffNotTruncated)
{
    // "onnnnnnnnnn" must not read as "on".
    write(doc("onnnnnnnnnnnnnnnnnnn"));
    reader.refresh();
    ASSERT_EQ(reader.status(), Status::Ok);
    EXPECT_FALSE(reader.getFlag(InputConfig::kQueryRecordImu));
}

TEST_F(InputConfigTest, RefreshReReadsOnlyWhenTheFileChanges)
{
    write(doc("off"));
    EXPECT_TRUE(reader.refresh());
    EXPECT_FALSE(reader.getFlag(InputConfig::kQueryRecordImu));

    // Nothing outside touched it: one stat, no re-read.
    EXPECT_FALSE(reader.refresh());

    // Flipping the flag over USB must take effect without a restart. The size
    // differs here, which is what the double can express — its objectInfo
    // reports utc as 0 for every file.
    write(doc("enabled"));
    EXPECT_TRUE(reader.refresh());
    EXPECT_TRUE(reader.getFlag(InputConfig::kQueryRecordImu));
}

TEST_F(InputConfigTest, GetStringRejectsRatherThanTruncates)
{
    write(doc("abcdefgh"));
    reader.refresh();
    ASSERT_EQ(reader.status(), Status::Ok);

    char small[4]{};
    EXPECT_FALSE(reader.getString(InputConfig::kQueryRecordImu, small, sizeof(small)));
    EXPECT_STREQ(small, "") << "must be emptied, not left holding a partial value";

    char big[16]{};
    EXPECT_TRUE(reader.getString(InputConfig::kQueryRecordImu, big, sizeof(big)));
    EXPECT_STREQ(big, "abcdefgh");
}

} // namespace
