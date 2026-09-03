/**
 ******************************************************************************
 * @file    SquashLog_test.cpp
 * @brief   The two files that explain a session, through SDK::Kernel.
 ******************************************************************************
 *
 * The logger exists because LOG_INFO needs a UART capture and a dev tool
 * attached, and nobody has one on court. That only helps if the file is
 * actually appended to rather than overwritten, which is the trap
 * `open(write, override=false)` sets: it positions at offset 0 on both the
 * SDK's fake and FatFs, so a writer that does not seek keeps its newest line
 * and nothing else. That, the rotation at the cap, and the CSV's column order
 * are what these assert.
 */

#include "KernelTestDoubles.hpp"
#include "SquashLog.hpp"

#include <gtest/gtest.h>

#include <string>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

std::string read(const SDK::TestSupport::InMemoryFileSystem& fs, const char* path)
{
    const auto it = fs.files.find(path);
    if (it == fs.files.end() || !it->second.exists) {
        return {};
    }
    return std::string(it->second.content.begin(), it->second.content.end());
}

std::vector<std::string> lines(const std::string& text)
{
    std::vector<std::string> out;
    size_t                   at = 0;
    while (at < text.size()) {
        const size_t nl = text.find('\n', at);
        if (nl == std::string::npos) {
            out.push_back(text.substr(at));
            break;
        }
        out.push_back(text.substr(at, nl - at));
        at = nl + 1;
    }
    return out;
}

/// The header this build writes, for the staleness test to compare against.
const char* kSessionsHeaderForTest()
{
    static std::string header;
    if (header.empty()) {
        SDK::TestSupport::KernelFixture fixture;
        SquashLog                       log(fixture.kernel);
        SquashLog::Session              s{};
        log.session(s);
        const std::string text = read(fixture.fileSystem, kSquashSessionsPath);
        header = lines(text)[0];
    }
    return header.c_str();
}

SquashLog::Session sessionRow()
{
    SquashLog::Session s{};
    s.record.startedUtc        = 1785751200;
    s.record.activeS           = 3600;
    s.record.hrCoveredS        = 3500;
    s.record.hrMean            = 142.5f;
    s.record.hrMax             = 181.25f;
    s.record.hrSource          = 2;
    s.record.segmented         = 0;
    s.record.hrExternalReadings = 118;
    s.profileSessions          = 3;
    s.calibration              = 0;
    s.imuSamples               = 180000;
    s.imuBytes                 = 7900000;
    s.markers                  = 41;
    s.hrRows                   = 1800;
    s.imuStop                  = 1;
    s.recordingIntact          = 1;
    s.profileSaved             = 1;
    return s;
}

} // namespace

TEST(SquashLog, EveryLineIsKeptRatherThanOverwritten)
{
    SDK::TestSupport::KernelFixture fixture;
    SquashLog                       log(fixture.kernel);

    log.line("launch", "one");
    log.line("profile", "two");
    log.line("imu", "three");

    const auto l = lines(read(fixture.fileSystem, kSquashLogPath));
    ASSERT_GE(l.size(), 3u) << "open(write, override=false) starts at offset 0; without a seek "
                               "this file keeps only its newest line";
    EXPECT_NE(l[0].find("launch one"), std::string::npos);
    EXPECT_NE(l[1].find("profile two"), std::string::npos);
    EXPECT_NE(l[2].find("imu three"), std::string::npos);
}

TEST(SquashLog, ALineCarriesBothClocksAndItsTag)
{
    SDK::TestSupport::KernelFixture fixture;
    SquashLog                       log(fixture.kernel);

    log.line("abi", "ok=%u", 1u);

    const std::string text = read(fixture.fileSystem, kSquashLogPath);
    unsigned long     uptime = 0;
    long long         utc    = 0;
    char              tag[32]{};
    char              detail[64]{};
    ASSERT_EQ(std::sscanf(text.c_str(), "%lu %lld %31s %63[^\n]", &uptime, &utc, tag, detail), 4);
    EXPECT_STREQ(tag, "abi");
    EXPECT_STREQ(detail, "ok=1");
}

TEST(SquashLog, TheSessionCsvGetsItsHeaderOnceAndARowPerSession)
{
    SDK::TestSupport::KernelFixture fixture;
    SquashLog                       log(fixture.kernel);

    log.session(sessionRow());
    log.session(sessionRow());

    const auto l = lines(read(fixture.fileSystem, kSquashSessionsPath));
    ASSERT_EQ(l.size(), 3u);
    EXPECT_EQ(l[0].compare(0, 4, "utc,"), 0);
    EXPECT_EQ(l[1], l[2]);
}

TEST(SquashLog, ASessionRowCarriesEveryColumnItsHeaderNames)
{
    SDK::TestSupport::KernelFixture fixture;
    SquashLog                       log(fixture.kernel);

    log.session(sessionRow());

    const auto l = lines(read(fixture.fileSystem, kSquashSessionsPath));
    ASSERT_EQ(l.size(), 2u);

    const auto commas = [](const std::string& s) {
        return std::count(s.begin(), s.end(), ',');
    };
    EXPECT_EQ(commas(l[0]), commas(l[1])) << "header and row must have the same column count";
    EXPECT_EQ(l[1],
              "1785751200,3600,14250,18125,3500,2,0,0,0,0,0,0,0,0,0,0,3,0,1,180000,7900000,41,1800,1,1,118");
}

TEST(SquashLog, AnUncalibratedSessionRecordsZerosThatSayNothingRanNotThatNothingHappened)
{
    SDK::TestSupport::KernelFixture fixture;
    SquashLog                       log(fixture.kernel);

    log.session(sessionRow());

    const auto l = lines(read(fixture.fileSystem, kSquashSessionsPath));
    // The `segmented` and `calibration` columns are what tell a reader that the
    // zeroed rally figures beside them were never computed.
    EXPECT_NE(l[0].find("segmented"), std::string::npos);
    EXPECT_NE(l[0].find("calibration"), std::string::npos);
}

TEST(SquashLog, PastItsCapTheLogRestartsAndSaysSo)
{
    SDK::TestSupport::KernelFixture fixture;
    SquashLog                       log(fixture.kernel);

    // Each line is under 100 bytes, so this comfortably passes the cap.
    for (int i = 0; i < 1200; ++i) {
        log.line("fill", "%d 0123456789012345678901234567890123456789012345678901234567890", i);
    }

    const std::string text = read(fixture.fileSystem, kSquashLogPath);
    EXPECT_LT(text.size(), kSquashLogMaxBytes + 4096u) << "an unbounded log fills the volume the "
                                                          "recording then cannot be written to";
    const auto l = lines(text);
    ASSERT_FALSE(l.empty());
    EXPECT_NE(l[0].find("rotated"), std::string::npos)
        << "the note is the first line of the new file, so a reader is not left wondering "
           "where the older lines went; got: "
        << l[0];
}

TEST(SquashLog, PastItsCapTheSessionCsvRestartsWithItsHeaderIntact)
{
    SDK::TestSupport::KernelFixture fixture;
    SquashLog                       log(fixture.kernel);

    // A row is ~87 bytes, so this passes the 64 KiB cap with room to spare.
    for (int i = 0; i < 900; ++i) {
        log.session(sessionRow());
    }

    const std::string text = read(fixture.fileSystem, kSquashSessionsPath);
    EXPECT_LT(text.size(), kSquashSessionsMaxBytes + 4096u);
    const auto l = lines(text);
    ASSERT_GE(l.size(), 2u);
    EXPECT_EQ(l[0].compare(0, 4, "utc,"), 0)
        << "a rotated CSV must start with its header, not a note: a reader takes "
           "line one as the column names. Got: "
        << l[0];
}

TEST(SquashLog, AStaleHeaderFromAnEarlierBuildRestartsTheFile)
{
    SDK::TestSupport::KernelFixture fixture;

    // What an earlier build left behind: a header naming fewer columns, and a
    // row written against it.
    fixture.fileSystem.seedFile(
        kSquashSessionsPath,
        "utc,active_s,intact,saved\n1785751200,3600,1,1\n");

    SquashLog log(fixture.kernel);
    log.session(sessionRow());

    const auto l = lines(read(fixture.fileSystem, kSquashSessionsPath));
    ASSERT_GE(l.size(), 2u);
    EXPECT_EQ(l[0], std::string(kSessionsHeaderForTest()))
        << "the header must be the FIRST line: a reader takes line one as the "
           "column names, so a note there reports every field missing";

    const auto commas = [](const std::string& s) {
        return std::count(s.begin(), s.end(), ',');
    };
    EXPECT_EQ(commas(l[0]), commas(l[1]))
        << "header and row must agree, which is the whole point";

    // The restart is still recorded -- in the log, where a note is readable.
    EXPECT_NE(read(fixture.fileSystem, kSquashLogPath).find("sessions_csv"),
              std::string::npos);
}

TEST(SquashLog, AMatchingHeaderIsAppendedToRatherThanRestarted)
{
    SDK::TestSupport::KernelFixture fixture;
    SquashLog                       log(fixture.kernel);

    log.session(sessionRow());
    log.session(sessionRow());

    const auto l = lines(read(fixture.fileSystem, kSquashSessionsPath));
    ASSERT_EQ(l.size(), 3u) << "one header and two rows";
    EXPECT_EQ(l[0], std::string(kSessionsHeaderForTest()));
}

TEST(SquashLog, TheLogIsWrittenUnderTheSharedDebugDirectory)
{
    SDK::TestSupport::KernelFixture fixture;
    SquashLog                       log(fixture.kernel);

    log.line("launch", "x");
    log.session(sessionRow());

    EXPECT_EQ(std::string(kSquashLogPath).compare(0, 6, "Debug/"), 0);
    EXPECT_EQ(std::string(kSquashSessionsPath).compare(0, 6, "Debug/"), 0);
    EXPECT_FALSE(read(fixture.fileSystem, kSquashLogPath).empty());
    EXPECT_FALSE(read(fixture.fileSystem, kSquashSessionsPath).empty());
}
