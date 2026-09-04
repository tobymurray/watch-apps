/**
 ******************************************************************************
 * @file    SharedLog_test.cpp
 * @brief   The two files a ride leaves behind, written against a filesystem.
 *
 * TrainKit's own tests cover what the JSON says; these cover getting it onto
 * storage -- the mkdir, the tmp/rotate/rename commit, the refusal to overwrite
 * a newer schema, and the append-across-rides the diagnostic log relies on.
 * None of that is reachable from a Rust test, and none of it is reachable from
 * Spin's other host suite either.
 *
 * The filesystem here is the SDK's in-memory double, not FatFs. That makes
 * these tests evidence about THIS code's logic and not about the kernel's
 * open()/rename() mapping, which only hardware can settle -- see
 * Docs/RECOVERY-FIELD-TEST.md.
 ******************************************************************************
 */

#include "EventLog.hpp"
#include "KernelTestDoubles.hpp"
#include "SharedLog.hpp"
#include "trainkit.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using SDK::TestSupport::KernelFixture;
using TrainKit::EventLog;
using TrainKit::SharedLog;

extern "C" {

/// The Service's own, which the archive expects any host to provide. A panic
/// reaching here means a TrainKit bug the Rust suite should have caught, so it
/// fails the test rather than being swallowed.
void trainkit_host_panic(const uint8_t* msg, uint32_t len)
{
    FAIL() << "TrainKit panicked: "
           << std::string(reinterpret_cast<const char*>(msg), len);
}

/// libtrainkit.a is built `no_std` against a `core` that was compiled to
/// unwind, so the host linker still wants this even though `panic = "abort"`
/// means nothing ever reaches it. The watch build has its own.
void rust_eh_personality() {}

} // extern "C"

namespace {

constexpr const char* kPath    = "../SharedData/spin_sessions.json";
constexpr const char* kTmpPath = "../SharedData/spin_sessions.json.tmp";
constexpr const char* kBakPath = "../SharedData/spin_sessions.json.bak";

/// The in-memory double lets a rename overwrite its target. FatFs does not --
/// Squash proved that and left the finding in SquashEngine.cpp -- so a test
/// running against the permissive double would pass on code the watch refuses.
/// This narrows the double to the real behaviour for that one call.
class FatFsRenameRules : public SDK::TestSupport::InMemoryFileSystem {
public:
    bool rename(const char* oldPath, const char* newPath) override
    {
        if (newPath == nullptr) {
            return false;
        }
        // The rotation of the live file to .bak is best-effort, so the code
        // under test has to survive it failing. Nothing else simulates that.
        if (failBackupRotation && endsWith(newPath, ".bak")) {
            return false;
        }
        if (exist(newPath)) {
            ++refused;
            return false;
        }
        return InMemoryFileSystem::rename(oldPath, newPath);
    }

    /// Renames the real filesystem would have rejected.
    int refused = 0;
    bool failBackupRotation = false;

private:
    static bool endsWith(const char* s, const char* suffix)
    {
        const size_t n = std::strlen(s), m = std::strlen(suffix);
        return n >= m && std::strcmp(s + n - m, suffix) == 0;
    }
};

/// One plausible ride, so a test only has to say what it is varying.
trainkit_session aRide(uint32_t startUtc)
{
    trainkit_session s{};
    s.start_utc = startUtc;
    s.active_s  = 2700;
    s.elapsed_s = 2760;
    s.kcal      = 480;
    s.work_kj   = 430;
    s.hr_avg    = 142;
    s.hr_max    = 178;
    s.hr_max_setting = 190;
    s.weight_kg = 75;
    s.zone_count = 5;
    // The watch's own ladder at 190 bpm, which is also Edwards'.
    const uint8_t floors[5] = {95, 114, 133, 152, 171};
    for (size_t i = 0; i < 5; ++i) {
        s.zone_floor[i] = floors[i];
    }
    const uint16_t seconds[6] = {120, 300, 900, 1000, 300, 80};
    for (size_t i = 0; i < 6; ++i) {
        s.zone_s[i] = seconds[i];
    }
    s.recovery_count = 1;
    s.recoveries[0].at_active_s = 2640;
    s.recoveries[0].hr0         = 170;
    s.recoveries[0].hr_end      = 117;
    s.recoveries[0].window_s    = 60;
    s.recoveries[0].trusted_s   = 61;
    s.recoveries[0].hr0_pct_max = 89;
    s.recoveries[0].trigger     = TRAINKIT_TRIGGER_PAUSE;
    const uint8_t curve[7] = {170, 157, 146, 137, 130, 123, 117};
    for (size_t i = 0; i < 7; ++i) {
        s.recoveries[0].curve[i] = curve[i];
    }
    return s;
}

std::string readBack(SDK::TestSupport::KernelFixture& k, const char* path)
{
    auto it = k.fileSystem.files.find(path);
    if (it == k.fileSystem.files.end() || !it->second.exists) {
        return {};
    }
    return std::string(it->second.content.begin(), it->second.content.end());
}

void putFile(SDK::TestSupport::KernelFixture& k, const char* path, const std::string& text)
{
    auto& entry = k.fileSystem.files[path];
    entry.exists = true;
    entry.content.assign(text.begin(), text.end());
}

} // namespace

// -- The shared session log ---------------------------------------------------

TEST(SharedLog, AFirstRideCreatesTheFile)
{
    KernelFixture k;
    SharedLog log(k.kernel.fs, "Spin", "indoor_cycling");

    ASSERT_EQ(log.record(aRide(1756800000u)), SharedLog::Status::OK);

    const std::string text = readBack(k, kPath);
    ASSERT_FALSE(text.empty()) << "nothing landed at " << kPath;
    EXPECT_NE(text.find("\"version\":1"), std::string::npos) << text;
    EXPECT_NE(text.find("\"app\":\"Spin\""), std::string::npos) << text;
    EXPECT_NE(text.find("\"kept\":1"), std::string::npos) << text;
    EXPECT_NE(text.find("\"start_utc\":1756800000"), std::string::npos) << text;
    EXPECT_NE(text.find("\"drop_bpm\":53"), std::string::npos) << text;
}

TEST(SharedLog, TheCommitLeavesNoTemporaryFileBehind)
{
    KernelFixture k;
    SharedLog log(k.kernel.fs, "Spin", "indoor_cycling");
    ASSERT_EQ(log.record(aRide(1)), SharedLog::Status::OK);
    EXPECT_FALSE(k.kernel.fs.exist(kTmpPath));
}

TEST(SharedLog, ASecondRideAppendsAndKeepsTheOldFileAsBackup)
{
    KernelFixture k;
    SharedLog log(k.kernel.fs, "Spin", "indoor_cycling");

    ASSERT_EQ(log.record(aRide(1000)), SharedLog::Status::OK);
    ASSERT_EQ(log.record(aRide(2000)), SharedLog::Status::OK);

    const std::string text = readBack(k, kPath);
    EXPECT_NE(text.find("\"kept\":2"), std::string::npos) << text;
    EXPECT_NE(text.find("\"start_utc\":1000"), std::string::npos);
    EXPECT_NE(text.find("\"start_utc\":2000"), std::string::npos);

    // The previous version is still there, which is what makes a failed commit
    // survivable rather than fatal.
    const std::string bak = readBack(k, kBakPath);
    EXPECT_NE(bak.find("\"kept\":1"), std::string::npos) << bak;
}

TEST(SharedLog, TheSameRideTwiceIsOneEntry)
{
    KernelFixture k;
    SharedLog log(k.kernel.fs, "Spin", "indoor_cycling");
    ASSERT_EQ(log.record(aRide(4242)), SharedLog::Status::OK);
    ASSERT_EQ(log.record(aRide(4242)), SharedLog::Status::OK);
    EXPECT_NE(readBack(k, kPath).find("\"kept\":1"), std::string::npos);
}

TEST(SharedLog, WhatIsWrittenIsWhatTrainKitReadsBack)
{
    KernelFixture k;
    SharedLog log(k.kernel.fs, "Spin", "indoor_cycling");
    ASSERT_EQ(log.record(aRide(777)), SharedLog::Status::OK);

    const std::string text = readBack(k, kPath);
    std::vector<uint8_t> hist(trainkit_history_bytes());
    trainkit_history_init(hist.data(), "Spin", "indoor_cycling");
    ASSERT_EQ(trainkit_history_load(hist.data(),
                                    reinterpret_cast<const uint8_t*>(text.data()),
                                    static_cast<uint32_t>(text.size())),
              TRAINKIT_LOAD_OK);
}

TEST(SharedLog, ANewerSchemaIsLeftExactlyAsFound)
{
    KernelFixture k;
    const std::string newer = R"({"version":2,"app":"Spin","sessions":[],"future":1})";
    putFile(k, kPath, newer);

    SharedLog log(k.kernel.fs, "Spin", "indoor_cycling");
    EXPECT_EQ(log.record(aRide(1)), SharedLog::Status::REFUSED);
    EXPECT_EQ(readBack(k, kPath), newer) << "a newer writer's file was modified";
    EXPECT_FALSE(k.kernel.fs.exist(kTmpPath));
}

TEST(SharedLog, RubbishIsKeptAsEvidenceAndAFreshFileStarted)
{
    KernelFixture k;
    putFile(k, kPath, "this is not json");

    SharedLog log(k.kernel.fs, "Spin", "indoor_cycling");
    ASSERT_EQ(log.record(aRide(9)), SharedLog::Status::OK);

    EXPECT_EQ(readBack(k, kBakPath), "this is not json");
    EXPECT_NE(readBack(k, kPath).find("\"kept\":1"), std::string::npos);
}

TEST(SharedLog, AnOrdinaryCommitNeverRenamesOntoAnExistingPath)
{
    FatFsRenameRules fs;
    SharedLog log(fs, "Spin", "indoor_cycling");
    ASSERT_EQ(log.record(aRide(1000)), SharedLog::Status::OK);
    ASSERT_EQ(log.record(aRide(2000)), SharedLog::Status::OK);
    EXPECT_EQ(fs.refused, 0) << "a rename was issued onto a path that existed";
}

TEST(SharedLog, LosingTheBackupDoesNotLoseTheRide)
{
    // Rotating the live file to .bak is best-effort. When it fails the live
    // file is still there -- and because FatFs refuses a rename onto an
    // existing path, the commit that follows fails too, turning a lost backup
    // into a lost ride. Every assertion below passes without the remove() in
    // SharedLog::commit(); only this one fails.
    FatFsRenameRules fs;
    SharedLog log(fs, "Spin", "indoor_cycling");
    ASSERT_EQ(log.record(aRide(1000)), SharedLog::Status::OK);

    fs.failBackupRotation = true;
    ASSERT_EQ(log.record(aRide(2000)), SharedLog::Status::OK)
        << "a failed .bak rotation blocked the commit";
    EXPECT_EQ(fs.refused, 0) << "a rename was issued onto a path that existed";

    auto it = fs.files.find(kPath);
    ASSERT_NE(it, fs.files.end());
    const std::string text(it->second.content.begin(), it->second.content.end());
    EXPECT_NE(text.find("\"start_utc\":2000"), std::string::npos)
        << "the newest ride did not survive: " << text;
}

TEST(SharedLog, TheFilenameIsThisAppsAndLowercase)
{
    KernelFixture k;
    SharedLog other(k.kernel.fs, "Squash", "squash");
    ASSERT_EQ(other.record(aRide(1)), SharedLog::Status::OK);

    EXPECT_TRUE(k.kernel.fs.exist("../SharedData/squash_sessions.json"));
    EXPECT_FALSE(k.kernel.fs.exist(kPath)) << "one app wrote another app's file";
}

// -- The diagnostic log -------------------------------------------------------

TEST(EventLog, ARideCreatesTheFileAndWritesToIt)
{
    KernelFixture k;
    EventLog log(k.kernel.fs, "recovery.log");
    log.open();
    log.line("%u start max_hr=%u", 1756800000u, 190u);
    log.close();

    const std::string text = readBack(k, "recovery.log");
    EXPECT_EQ(text, "1756800000 start max_hr=190\n");
}

TEST(EventLog, ASecondRideAppendsRatherThanOverwriting)
{
    KernelFixture k;
    EventLog log(k.kernel.fs, "recovery.log");

    log.open();
    log.line("ride one");
    log.close();

    log.open();
    log.line("ride two");
    log.close();

    EXPECT_EQ(readBack(k, "recovery.log"), "ride one\nride two\n");
}

TEST(EventLog, ItStopsAtItsCapRatherThanFillingTheCard)
{
    KernelFixture k;
    EventLog log(k.kernel.fs, "recovery.log");
    log.open();

    // Each line is 64 bytes, so this asks for four times the cap.
    const std::string filler(62, 'x');
    for (size_t i = 0; i < 4 * EventLog::kMaxBytes / 64; ++i) {
        log.line("%s", filler.c_str());
    }
    log.close();

    const std::string text = readBack(k, "recovery.log");
    EXPECT_LE(text.size(), EventLog::kMaxBytes + EventLog::kMaxLine);
    EXPECT_NE(text.find("log full"), std::string::npos);
}

TEST(EventLog, AFullLogStaysFullAcrossRides)
{
    KernelFixture k;
    putFile(k, "recovery.log", std::string(EventLog::kMaxBytes, 'x'));

    EventLog log(k.kernel.fs, "recovery.log");
    log.open();
    log.line("this must not be written");
    log.close();

    EXPECT_EQ(readBack(k, "recovery.log").find("must not"), std::string::npos);
}

TEST(EventLog, ALineLongerThanTheBufferIsTruncatedNotSplit)
{
    KernelFixture k;
    EventLog log(k.kernel.fs, "recovery.log");
    log.open();
    log.line("%s", std::string(EventLog::kMaxLine * 2, 'y').c_str());
    log.close();

    const std::string text = readBack(k, "recovery.log");
    EXPECT_LE(text.size(), EventLog::kMaxLine);
    EXPECT_EQ(text.back(), '\n') << "a truncated line still has to end";
}

TEST(EventLog, LoggingWithoutOpeningIsSilentRatherThanFatal)
{
    KernelFixture k;
    EventLog log(k.kernel.fs, "recovery.log");
    log.line("nobody opened this");   // must not crash
    log.sync();
    log.close();
    EXPECT_FALSE(k.kernel.fs.exist("recovery.log"));
}

TEST(EventLog, ResetLeavesNothingBehind)
{
    KernelFixture k;
    EventLog log(k.kernel.fs, "recovery.log");
    log.open();
    log.line("something");
    log.reset();
    EXPECT_FALSE(k.kernel.fs.exist("recovery.log"));
}
