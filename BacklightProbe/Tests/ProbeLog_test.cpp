/**
 * Tests for the results file.
 *
 * The wording of these lines is the deliverable: it is what somebody reads a
 * year from now to decide whether the backlight can be dimmed. So these tests
 * pin the bytes, not just the fact that something was written.
 *
 * Two things in particular are worth pinning:
 *
 *   - **The negative is stated, not implied.** Six null pointers followed by
 *     nothing is a reader's inference. "all six null: Q7 closed" is a finding.
 *   - **The simulator disclaims itself.** A results file produced on a host
 *     build must not be mistakable for one produced on the watch, because every
 *     register in it is unread and every zero in it is a default rather than a
 *     measurement.
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "KernelTestDoubles.hpp"

#include "ProbeLog.hpp"
#include "ProbePlan.hpp"

namespace {

using namespace Probe;

/// Opens a file in the in-memory filesystem and hands back both the handle and
/// the log writing into it.
struct LogFixture {
    SDK::TestSupport::InMemoryFileSystem fs;
    std::unique_ptr<SDK::Interface::IFile> file;
    std::unique_ptr<ProbeLog> log;

    LogFixture()
    {
        file = fs.file("results.txt");
        file->open(true, true);
        log = std::make_unique<ProbeLog>(*file);
    }

    std::string text() const { return fs.readFile("results.txt"); }
};

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

Step aSet(uint8_t brightness, uint32_t autoOff, Sender sender = Sender::Service)
{
    return Step{Action::SetBacklight, sender, brightness, autoOff, 250, 0, "ladder", true};
}

TEST(ProbeLog, HeaderSaysWhatTheFileCannotTellYou)
{
    LogFixture f;
    f.log->header(1234, true, 22, false);

    const std::string out = f.text();
    EXPECT_TRUE(contains(out, "uptime_ms=1234"));
    EXPECT_TRUE(contains(out, "registers=Y"));
    EXPECT_TRUE(contains(out, "sweep_blocks=22"));
    EXPECT_TRUE(contains(out, "timers=N"));

    // The limit of the instrument, stated in the artefact rather than left in a
    // design document nobody reading the file will have.
    EXPECT_TRUE(contains(out, "CANNOT"));
    EXPECT_TRUE(contains(out, "IBacklight"));
    EXPECT_TRUE(contains(out, "video"));
}

TEST(ProbeLog, HeaderWarnsLoudlyWhenThereAreNoRegisters)
{
    LogFixture f;
    f.log->header(0, /*registersAvailable=*/false, 0, false);

    const std::string out = f.text();
    EXPECT_TRUE(contains(out, "registers=N"));
    EXPECT_TRUE(contains(out, "WARNING"));
    EXPECT_TRUE(contains(out, "says anything about real hardware"))
        << "a host-build results file must not be mistakable for a real run";
}

TEST(ProbeLog, HeaderRecordsWhetherTheTimerBlocksWereIncluded)
{
    // The bases used to be an inference and the header used to warn about them.
    // They are confirmed against ST's CMSIS device header now, so the line says
    // what was covered rather than what might fault.
    LogFixture f;
    f.log->header(0, true, 35, /*timersIncluded=*/true);

    const std::string out = f.text();
    EXPECT_TRUE(contains(out, "timers=Y"));
    EXPECT_TRUE(contains(out, "sweep_blocks=35"));
    EXPECT_FALSE(contains(out, "UNCONFIRMED"))
        << "still warning about bases that are no longer in doubt";
}

TEST(ProbeLog, ARequestRecordsBothWhatWentOutAndWhatCameBack)
{
    LogFixture f;
    Backlight::Outcome outcome;
    outcome.brightness    = 75;
    outcome.autoOffMs     = 600000;
    outcome.sendTimeoutMs = 250;
    outcome.sent          = true;
    outcome.result        = SDK::MessageResult::SUCCESS;
    outcome.completed     = true;
    outcome.elapsedMs     = 3;

    f.log->backlight(7, aSet(75, 600000), outcome);

    const std::string out = f.text();
    EXPECT_TRUE(contains(out, "brightness=75"));
    EXPECT_TRUE(contains(out, "auto_off_ms=600000"));
    EXPECT_TRUE(contains(out, "send_timeout_ms=250"));
    EXPECT_TRUE(contains(out, "sent=Y"));
    EXPECT_TRUE(contains(out, "result=SUCCESS"));
    EXPECT_TRUE(contains(out, "completed=Y"));
    EXPECT_TRUE(contains(out, "elapsed_ms=3"));
    EXPECT_TRUE(contains(out, "from=SVC"));
}

TEST(ProbeLog, ARequestRecordsWhichProcessSentIt)
{
    LogFixture f;
    Backlight::Outcome outcome;
    outcome.result = SDK::MessageResult::TIMEOUT;

    f.log->backlight(3, aSet(100, 5000, Sender::Gui), outcome);
    EXPECT_TRUE(contains(f.text(), "from=GUI"))
        << "Q1's context half is unreadable if the file does not say who sent it";
}

TEST(ProbeLog, AFailedAllocationIsDistinctFromAKernelRejection)
{
    // Two different failures that must not be conflated: "the message never
    // left" and "the kernel refused it" point at completely different causes.
    LogFixture f;
    Backlight::Outcome outcome;
    outcome.allocationFailed = true;
    outcome.sent             = false;

    f.log->backlight(1, aSet(100, 0), outcome);

    const std::string out = f.text();
    EXPECT_TRUE(contains(out, "sent=N"));
    EXPECT_TRUE(contains(out, "alloc_failed=Y"));
}

TEST(ProbeLog, PendingIsRecordedAsSuchRatherThanAsSuccess)
{
    // The interesting negative: a non-zero send timeout that still comes back
    // PENDING means nothing signalled completion, i.e. no handler ran.
    LogFixture f;
    Backlight::Outcome outcome;
    outcome.sent          = true;
    outcome.sendTimeoutMs = 250;
    outcome.result        = SDK::MessageResult::PENDING;
    outcome.completed     = false;

    f.log->backlight(0, aSet(100, 0), outcome);

    const std::string out = f.text();
    EXPECT_TRUE(contains(out, "result=PENDING"));
    EXPECT_TRUE(contains(out, "completed=N"));
}

TEST(ProbeLog, ASweepRecordsTheFileItShouldHaveWritten)
{
    LogFixture f;
    const Step step{Action::Sweep, Sender::Service, 0, 0, 0, 0, "lit_b100", true};

    f.log->sweep(4, step, true);
    EXPECT_TRUE(contains(f.text(), "file=sweep_lit_b100.txt"));
    EXPECT_TRUE(contains(f.text(), "written=Y"));
}

TEST(ProbeLog, AFailedSweepSaysSo)
{
    LogFixture f;
    const Step step{Action::Sweep, Sender::Service, 0, 0, 0, 0, "dark", true};

    f.log->sweep(1, step, false);
    EXPECT_TRUE(contains(f.text(), "written=N"))
        << "a missing sweep file must be attributable to the run, not to the copy off the watch";
}

TEST(ProbeLog, SixNullsAreStatedAsAConclusion)
{
    LogFixture f;
    IidProbe::Result result;
    result.meaningful   = true;
    result.nonNullCount = 0;
    for (size_t i = 0; i < IidProbe::kCount; ++i) {
        result.answers[i].iid = IidProbe::kFirst + static_cast<uint32_t>(i) * IidProbe::kStep;
    }

    f.log->iids(9, result);

    const std::string out = f.text();
    EXPECT_TRUE(contains(out, "00050000"));
    EXPECT_TRUE(contains(out, "000A0000"));
    EXPECT_TRUE(contains(out, "Q7 closed"))
        << "the load-bearing negative has to be a sentence, not six lines to infer it from";
}

TEST(ProbeLog, ANonNullPointerCarriesTheWarningNotToCallIt)
{
    LogFixture f;
    IidProbe::Result result;
    result.meaningful     = true;
    result.nonNullCount   = 1;
    result.answers[2].iid = 0x00070000;
    result.answers[2].nonNull = true;
    result.answers[2].value   = 0x0806C000;

    f.log->iids(9, result);

    const std::string out = f.text();
    EXPECT_TRUE(contains(out, "0806C000"));
    EXPECT_TRUE(contains(out, "NOT called through"));
    EXPECT_TRUE(contains(out, "Phase C"));
}

TEST(ProbeLog, ASimulatorIidWalkDisclaimsItself)
{
    LogFixture f;
    IidProbe::Result result;
    result.meaningful   = false;
    result.nonNullCount = 0;

    f.log->iids(9, result);

    const std::string out = f.text();
    EXPECT_TRUE(contains(out, "meaningful=N"));
    EXPECT_TRUE(contains(out, "says nothing about the device"));
    EXPECT_FALSE(contains(out, "Q7 closed"))
        << "the simulator must never appear to have closed a question about the watch";
}

TEST(ProbeLog, TheFooterSaysHowMuchOfTheAnswerIsNotInTheFile)
{
    LogFixture f;
    f.log->footer(99000, 47, 8);

    const std::string out = f.text();
    EXPECT_TRUE(contains(out, "uptime_ms=99000"));
    EXPECT_TRUE(contains(out, "steps=47"));
    EXPECT_TRUE(contains(out, "intact=Y"));
    EXPECT_TRUE(contains(out, "on the video, not here"));
}

TEST(ProbeLog, TruncationIsFlaggedRatherThanSilent)
{
    // Driven through a real record rather than a test-only hook: a label longer
    // than the line buffer is the way this can actually happen, and it is what
    // an over-enthusiastic edit to the plan would produce.
    LogFixture f;
    const std::string huge(400, 'x');
    const Step step{Action::Note, Sender::Service, 0, 0, 0, 0, huge.c_str(), true};

    f.log->note(0, step);

    EXPECT_FALSE(f.log->intact());
    f.log->footer(0, 1, 0);
    EXPECT_TRUE(contains(f.text(), "intact=N"))
        << "a run whose record lost a line has to be repeatable, which means knowing it lost one";
}

TEST(ProbeLog, AShortWriteIsFlagged)
{
    LogFixture f;
    // The storage stops accepting bytes partway through the run.
    f.fs.failWritesAfterBytes = 10;

    f.log->header(0, true, 22, false);
    EXPECT_FALSE(f.log->intact());
}

TEST(ProbeLog, EveryRecordIsFlushed)
{
    // Not buffered to the end: the app can be stopped without warning, and an
    // unflushed tail is the part of the experiment nobody has.
    LogFixture f;
    Backlight::Outcome outcome;
    f.log->backlight(0, aSet(100, 0), outcome);
    f.log->backlight(1, aSet(50, 0), outcome);

    EXPECT_GE(f.fs.flushCounts["results.txt"], 2u);
}

} // namespace
