/**
 * @file    ManagerLog_test.cpp
 * @brief   Host tests for the diagnostic log's size cap.
 *
 * The cap is the only thing standing between an APP_AUTOSTART service that
 * never exits and a file that grows on internal flash for the life of the
 * device, so it is worth pinning rather than trusting by inspection.
 */
#include <gtest/gtest.h>

#include <string>

#include "KernelTestDoubles.hpp"
#include "ManagerLog.hpp"

namespace {

using SDK::TestSupport::KernelFixture;

const char* kLogPath = "Debug/mapmanager_verify.log";

size_t logSize(KernelFixture& fixture)
{
    auto it = fixture.fileSystem.files.find(kLogPath);
    return it == fixture.fileSystem.files.end() ? 0 : it->second.content.size();
}

TEST(ManagerLog, AppendsAcrossSeparateCalls)
{
    KernelFixture fixture;
    ManagerLog log(fixture.kernel);

    log.logf("first\n");
    const size_t afterFirst = logSize(fixture);
    ASSERT_GT(afterFirst, 0u);

    log.logf("second\n");
    EXPECT_GT(logSize(fixture), afterFirst) << "each call must append, not overwrite";

    const std::string content = fixture.fileSystem.readFile(kLogPath);
    EXPECT_NE(content.find("first"), std::string::npos);
    EXPECT_NE(content.find("second"), std::string::npos);
}

TEST(ManagerLog, StaysBoundedNoMatterHowMuchIsWritten)
{
    KernelFixture fixture;
    ManagerLog log(fixture.kernel);

    // Comfortably more than the cap's worth of lines.
    for (int i = 0; i < 4000; ++i) {
        log.logf("a reasonably long diagnostic line, number %d, with padding\n", i);
    }

    EXPECT_LE(logSize(fixture), ManagerLog::kMaxBytes + 512u)
        << "the log must not grow without bound";
    EXPECT_GT(logSize(fixture), 0u) << "...but it must still be logging";
}

TEST(ManagerLog, KeepsTheMostRecentLinesAfterRestarting)
{
    KernelFixture fixture;
    ManagerLog log(fixture.kernel);

    for (int i = 0; i < 4000; ++i) {
        log.logf("line %d padded out to take up a useful amount of room\n", i);
    }
    log.logf("THE-LATEST-EVENT\n");

    const std::string content = fixture.fileSystem.readFile(kLogPath);
    EXPECT_NE(content.find("THE-LATEST-EVENT"), std::string::npos)
        << "the newest line is the one worth keeping";
    EXPECT_EQ(content.find("line 0 padded"), std::string::npos)
        << "the oldest lines should have been dropped by the restart";
    EXPECT_NE(content.find("log restarted"), std::string::npos)
        << "a restart should say so, so a truncated log is not mistaken for a short one";
}

TEST(ManagerLog, LeavesNoOpenHandle)
{
    KernelFixture fixture;
    ManagerLog log(fixture.kernel);

    for (int i = 0; i < 500; ++i) {
        log.logf("line %d\n", i);
    }

    EXPECT_EQ(fixture.fileSystem.openHandles[kLogPath], 0u)
        << "every logf() must close its handle, including across a restart";
}

} // namespace
