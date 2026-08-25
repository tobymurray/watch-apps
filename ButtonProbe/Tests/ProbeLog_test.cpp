/**
 * Tests for the probe's log file, over the SDK's in-memory filesystem.
 *
 * This file is the app's entire output. If it silently writes nothing, the
 * probe is not a failed experiment -- it is an experiment that cannot be told
 * apart from a failed one, which is worse. That is the SleepLab failure exactly:
 * every part built, every part passed, and the wiring between them sent nothing.
 */

#include <string>

#include <gtest/gtest.h>

#include "KernelTestDoubles.hpp"
#include "ProbeLog.hpp"

namespace
{

constexpr char kPath[] = "probe.txt";

TEST(ProbeLog, WritesALineTerminatedByANewline)
{
    SDK::TestSupport::KernelFixture fixture;
    Probe::Log log(fixture.kernel, kPath);

    log.line("run-gui: SUCCESS");

    EXPECT_EQ(fixture.fileSystem.readFile(kPath), "run-gui: SUCCESS\n");
}

TEST(ProbeLog, AppendsRatherThanOverwriting)
{
    SDK::TestSupport::KernelFixture fixture;
    Probe::Log log(fixture.kernel, kPath);

    log.line("first tick at t=%u ms", 1234u);
    log.line("run-gui: refused, result=%u", 2u);

    EXPECT_EQ(fixture.fileSystem.readFile(kPath),
              "first tick at t=1234 ms\nrun-gui: refused, result=2\n");
}

TEST(ProbeLog, LeavesNoOpenHandles)
{
    // Every open() on this filesystem takes a lock slot that only close()
    // returns. A probe that leaked one per line would stop logging after a few
    // hundred, which on a watch is indistinguishable from the app dying -- one
    // of the outcomes being measured.
    SDK::TestSupport::KernelFixture fixture;
    Probe::Log log(fixture.kernel, kPath);

    for (int i = 0; i < 50; ++i) {
        log.line("line %d", i);
    }

    EXPECT_EQ(fixture.fileSystem.openHandles[kPath], 0u);
}

TEST(ProbeLog, StartsOverRatherThanGrowingWithoutBound)
{
    SDK::TestSupport::KernelFixture fixture;
    fixture.fileSystem.seedFile(kPath, std::string(Probe::Log::kMaxBytes, 'x'));

    Probe::Log log(fixture.kernel, kPath);
    log.line("after the roll");

    // The old contents are gone rather than appended to: a probe left installed
    // gets scrolled past for weeks.
    EXPECT_EQ(fixture.fileSystem.readFile(kPath), "after the roll\n");
}

TEST(ProbeLog, KeepsAppendingJustUnderTheCap)
{
    SDK::TestSupport::KernelFixture fixture;
    const std::string seeded(Probe::Log::kMaxBytes - 1, 'x');
    fixture.fileSystem.seedFile(kPath, seeded);

    Probe::Log log(fixture.kernel, kPath);
    log.line("kept");

    EXPECT_EQ(fixture.fileSystem.readFile(kPath), seeded + "kept\n");
}

TEST(ProbeLog, TruncatesAnOverlongLineAndStillTerminatesIt)
{
    SDK::TestSupport::KernelFixture fixture;
    Probe::Log log(fixture.kernel, kPath);

    // vsnprintf reports what it *would* have written, so using its return value
    // as a length unclamped would read past the end of the stack buffer.
    log.line("%s", std::string(500, 'z').c_str());

    const std::string written = fixture.fileSystem.readFile(kPath);
    EXPECT_FALSE(written.empty());
    EXPECT_EQ(written.back(), '\n');
    EXPECT_LT(written.size(), 500u);
    EXPECT_EQ(written.find_first_not_of('z'), written.size() - 1);
}

TEST(ProbeLog, SurvivesAFilesystemThatWillNotWrite)
{
    // The thing that records failures has nowhere to report its own, so it must
    // at least not take the app down with it.
    SDK::TestSupport::KernelFixture fixture;
    fixture.fileSystem.failWritesAfterBytes = 0;

    Probe::Log log(fixture.kernel, kPath);
    log.line("this goes nowhere");

    EXPECT_EQ(fixture.fileSystem.openHandles[kPath], 0u);
}

} // namespace
