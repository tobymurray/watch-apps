#include "Gui.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include "Commands.hpp"
#include "EffectBridge.hpp"
#include "haptic_lab_gui.h"

#define LOG_MODULE_PRX   "HapticGui"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

#ifndef HAPTICLAB_VERSION
// The build stamps the real one; this only keeps a host compile honest.
#define HAPTICLAB_VERSION "0.0.0-dev"
#endif

/// Provided by the loader (`system.cpp`), and declared here the way the
/// CustomGUI entry point declares it. Its `version` is the ABI the kernel
/// presents, recorded in `session.json` so a run says which interface version
/// produced it.
extern const SDK::Interface::IKernel *gIKernel;

namespace {

constexpr uint32_t kWaitForever = 0xFFFFFFFF;

/// Beside the run directories rather than under a `Debug/` of its own, because
/// `mkdir` is single level and one fewer directory is one fewer thing to fail.
constexpr char kLogPath[] = "/hapticlab.log";

} // namespace

extern "C" void haptic_lab_gui_host_panic(const uint8_t *msg, uint32_t len)
{
    LOG_ERROR("Rust panic: %.*s\n", static_cast<int>(len),
              reinterpret_cast<const char *>(msg));
    SDK::KernelProviderGUI::GetInstance().getKernel().sys.exit(1);
    while (true) {
    }
}

Gui::Gui(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mRecorder(kernel)
    , mSession(mPlan, mPatterns, mTiming)
{
}

uint32_t Gui::wallClock() const
{
    return static_cast<uint32_t>(std::time(nullptr));
}

void Gui::note(const char *format, ...)
{
    char line[192];
    const int prefix = std::snprintf(line, sizeof(line), "%lu %lu ",
                                     static_cast<unsigned long>(mKernel.sys.getTimeMs()),
                                     static_cast<unsigned long>(wallClock()));
    if (prefix <= 0 || static_cast<size_t>(prefix) >= sizeof(line)) {
        return;
    }

    va_list args;
    va_start(args, format);
    const int body = std::vsnprintf(line + prefix, sizeof(line) - static_cast<size_t>(prefix) - 2,
                                    format, args);
    va_end(args);
    if (body <= 0) {
        return;
    }

    size_t length = static_cast<size_t>(prefix) + static_cast<size_t>(body);
    if (length > sizeof(line) - 2) {
        length = sizeof(line) - 2;
    }
    line[length++] = '\n';

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kLogPath);
    if (!file || !file->open(/*wMode=*/true, /*override=*/false)) {
        return;
    }
    file->seek(file->size());
    size_t written = 0;
    file->write(line, length, written);
    file->flush();
    file->close();
}

void Gui::queryDisplayConfig()
{
    auto *cfg = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayConfig>();
    if (cfg != nullptr) {
        if (mKernel.comm.sendMessage(cfg, kResponseTimeoutMs) &&
            cfg->getResult() == SDK::MessageResult::SUCCESS) {
            mWidth  = cfg->width;
            mHeight = cfg->height;
        }
        mKernel.comm.releaseMessage(cfg);
    }

    const bool fitsFramebuffer =
        mWidth > 0 && mHeight > 0 &&
        static_cast<uint32_t>(mWidth) * static_cast<uint32_t>(mHeight) <= kMaxPixels;
    if (!fitsFramebuffer) {
        LOG_WARNING("Display config unusable (%dx%d); falling back to %dx%d\n", mWidth, mHeight,
                    kFallbackWidth, kFallbackHeight);
        mWidth  = kFallbackWidth;
        mHeight = kFallbackHeight;
    }
}

void Gui::takeCapabilities()
{
    auto *msg = mKernel.comm.allocateMessage<SDK::Message::RequestSetCapabilities>();
    if (msg == nullptr) {
        return;
    }
    // A forwarded phone notification buzzes the same actuator this experiment
    // is measuring, so leaving it on would put unlabelled presentations in the
    // middle of trials. Music control goes too, because with it on the system
    // eats HOLD_1S, not that anything here holds a button.
    msg->enPhoneNotification = false;
    msg->enMusicControl      = false;
    // The USB charging screen stays: plugging in terminates every running app,
    // and that screen is the one piece of feedback explaining why.
    msg->enUsbChargingScreen = true;
    mKernel.comm.sendMessage(msg);
    mKernel.comm.releaseMessage(msg);
}

void Gui::holdTheBacklight()
{
    auto *msg = mKernel.comm.allocateMessage<SDK::Message::RequestBacklightSet>();
    if (msg == nullptr) {
        return;
    }
    // HARDWARE: `brightness` is inert on this firmware and any non-zero value
    // behaves the same, but zero turns the light off; `autoOffTimeoutMs` of 0
    // holds it on indefinitely, which the SDK simulator's mock does not do
    // (BacklightProbe's suite 2, measured on the watch 2026-08-27).
    msg->brightness       = 100;
    msg->autoOffTimeoutMs = kBacklightHoldMs;
    mKernel.comm.sendMessage(msg);
    mKernel.comm.releaseMessage(msg);
}

void Gui::loadConfig()
{
    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(Haptic::kConfigPath);
    if (!file || !file->open()) {
        LOG_WARNING("no %s; the condition comes from the defaults\n", Haptic::kConfigPath);
        mConfig.seed = wallClock();
        return;
    }

    char   buf[512] = {};
    size_t read     = 0;
    const bool ok   = file->read(buf, sizeof(buf) - 1, read);
    file->close();
    if (!ok || read == 0 || !Haptic::parseConfig(buf, read, mConfig)) {
        LOG_WARNING("%s is unreadable; the condition comes from the defaults\n",
                    Haptic::kConfigPath);
        mConfig.seed = wallClock();
        return;
    }
    if (mConfig.seed == 0) {
        mConfig.seed = wallClock();
    }
    LOG_INFO("config: set %s, %u passes, %s %s, seed %lu\n", Haptic::setName(mConfig.set),
             mConfig.testPasses, Haptic::contactName(mConfig.contact),
             Haptic::maskingName(mConfig.masking), static_cast<unsigned long>(mConfig.seed));
}

void Gui::buildTheSession()
{
    mPatternCount = Haptic::buildSet(mConfig.set, mPatterns);
    Haptic::buildPlan(mConfig.set, mConfig.testPasses, mConfig.seed, mPlan);
    mTiming = Haptic::timingFor(mPatterns, mPatternCount, mDurations);

    mRun.seed    = mConfig.seed;
    mRun.contact = mConfig.contact;
    mRun.masking = mConfig.masking;
    mRun.set     = mConfig.set;
    mRun.setHash = Haptic::setHash(mPatterns, mPatternCount);

    mMeta.appVersion     = HAPTICLAB_VERSION;
    mMeta.firmware       = "unread";
    mMeta.kernelAbi      = gIKernel != nullptr ? gIKernel->version : 0;
    mMeta.wrist          = mConfig.wrist;
    mMeta.watchPosition  = mConfig.watchPosition;
    mMeta.strapTightness = mConfig.strapTightness;
    mMeta.armPosture     = mConfig.armPosture;
    mMeta.ambientTempC   = mConfig.ambientTempC;

    mSession.setDurations(mDurations);
    mProbeCount = Haptic::buildProbes(mProbes);
    mSweepTotal = mConfig.sweepLastEffect;

    // Asked at startup rather than at the first run, so a filesystem that
    // cannot be written to is on the menu before the wrist-hour instead of
    // after it. `mkdir` is create-or-exists.
    mFilesUsable = mKernel.fs.mkdir(Haptic::kRunsDir);
    if (!mFilesUsable) {
        LOG_ERROR("cannot create %s; nothing can be recorded\n", Haptic::kRunsDir);
    }
}

void Gui::openRun(const char *reason)
{
    if (mRunOpen) {
        return;
    }
    const uint32_t wall = wallClock();
    Haptic::Recorder::makeRunId(mRun.runId, wall);
    mMeta.startTick = mKernel.sys.getTimeMs();
    mMeta.startWall = wall;

    mRunOpen = mRecorder.open(mRun, mPlan, mPatterns, mPatternCount, mTiming, mDurations, mMeta);
    if (!mRunOpen) {
        mFilesUsable = false;
        LOG_ERROR("cannot open a run for %s; nothing will be recorded\n", reason);
        note("run open FAILED reason=%s", reason);
        return;
    }
    LOG_INFO("run %s open in %s (%s)\n", mRun.runId, mRecorder.directory(), reason);
    note("run open %s reason=%s seed=%lu set=%s contact=%s masking=%s", mRun.runId, reason,
         static_cast<unsigned long>(mRun.seed), Haptic::setName(mRun.set),
         Haptic::contactName(mRun.contact), Haptic::maskingName(mRun.masking));
}

void Gui::closeRun(const char *reason)
{
    if (!mRunOpen) {
        return;
    }
    mRecorder.close(reason, mKernel.sys.getTimeMs(), wallClock());
    note("run close %s reason=%s caps=%s", mRun.runId, reason, mRecorder.capsHit());
    mRunOpen = false;
}

bool Gui::sendVibro(const Haptic::Note notes[], uint8_t count, uint32_t declaredNotesCount,
                    uint32_t &tickBeforeSend, uint32_t &tickAtAccepted, bool &resultSuccess)
{
    tickBeforeSend = mKernel.sys.getTimeMs();
    resultSuccess  = false;

    auto *msg = mKernel.comm.allocateMessage<Haptic::Vibro>();
    if (msg == nullptr) {
        tickAtAccepted = tickBeforeSend;
        return false;
    }
    Haptic::fillVibroMessage(*msg, notes, count, declaredNotesCount);
    const bool sent = mKernel.comm.sendMessage(msg, Haptic::kVibroSendTimeoutMs);
    tickAtAccepted  = mKernel.sys.getTimeMs();
    resultSuccess   = msg->getResult() == SDK::MessageResult::SUCCESS;
    mKernel.comm.releaseMessage(msg);
    return sent;
}

void Gui::playPattern(uint8_t pattern, uint16_t trial, uint32_t nowMs)
{
    (void)nowMs;
    if (pattern >= mPatternCount) {
        return;
    }
    const Haptic::Pattern &p = mPatterns[pattern];

    Haptic::CommandRecord row {};
    row.trialIndex         = trial;
    row.patternIndex       = pattern;
    row.noteCount          = p.noteCount;
    row.declaredNotesCount = p.noteCount;
    for (uint8_t i = 0; i < p.noteCount && i < Haptic::kMaxNotes; i++) {
        row.notes[i] = p.notes[i];
    }
    row.wallClock = wallClock();
    row.sendOk    = sendVibro(p.notes, p.noteCount, p.noteCount, row.tickBeforeSend,
                              row.tickAtAccepted, row.resultSuccess);
    row.modelledEndTick = row.tickBeforeSend + Haptic::modelledDurationMs(p, mDurations);

    if (trial != Haptic::kNoTrial) {
        // The state machine owns the trial's copy of these two, and the third
        // is arithmetic it does from them.
        mSession.notePlayClocks(row.tickBeforeSend, row.tickAtAccepted, row.wallClock);
    }
    if (mRunOpen) {
        mRecorder.appendCommand(row, mPatterns, mPatternCount);
    }
}

void Gui::runProbe(uint32_t nowMs)
{
    if (mProbeIndex >= mProbeCount) {
        return;
    }
    mProbeStep    = 0;
    mProbeRunning = true;
    mProbeStepAt  = nowMs;
    mMode         = Haptic::Mode::ProbeSent;
    advanceProbeStep(nowMs);
}

void Gui::advanceProbeStep(uint32_t nowMs)
{
    const Haptic::ProbeCase &probe = mProbes[mProbeIndex];
    if (mProbeStep >= probe.stepCount) {
        mProbeRunning = false;
        return;
    }

    const Haptic::ProbeStep &step = probe.steps[mProbeStep];
    if (mProbeStep > 0 && Haptic::sinceTick(mProbeStepAt, nowMs) < static_cast<int32_t>(step.delayBeforeMs)) {
        return;
    }

    Haptic::ProbeRecord row {};
    row.noteCount          = step.noteCount;
    row.declaredNotesCount = step.declaredNotesCount;
    for (uint8_t i = 0; i < step.noteCount && i < Haptic::kMaxNotes; i++) {
        row.notes[i] = step.notes[i];
    }
    row.wallClock = wallClock();
    std::snprintf(row.label, sizeof(row.label), "%s/%u", probe.label, mProbeStep);
    row.sendOk = sendVibro(step.notes, step.noteCount, step.declaredNotesCount,
                           row.tickBeforeSend, row.tickAtAccepted, row.resultSuccess);
    if (mRunOpen) {
        mRecorder.appendProbe(row);
    }

    mProbeStep++;
    mProbeStepAt = nowMs;
    if (mProbeStep >= probe.stepCount) {
        mProbeRunning = false;
        return;
    }

    // A zero delay means the same handler, not the next frame. The first run
    // sent `V4-queue`'s two messages 73 ms apart because the advance waited for
    // a tick, which made a probe labelled "no gap" into an interrupt test.
    if (probe.steps[mProbeStep].delayBeforeMs == 0) {
        advanceProbeStep(nowMs);
    }
}

void Gui::startSweep()
{
    auto *msg = mKernel.comm.allocateMessage<CustomMessage::SweepStart>();
    if (msg == nullptr) {
        return;
    }
    std::snprintf(msg->runId, sizeof(msg->runId), "%s", mRun.runId);
    msg->firstEffect = mConfig.sweepFirstEffect;
    msg->lastEffect  = mConfig.sweepLastEffect;
    msg->repeats     = mConfig.sweepRepeats;
    mKernel.comm.sendMessage(msg);
    mKernel.comm.releaseMessage(msg);

    mSweepEffect = mConfig.sweepFirstEffect;
    mSweepTotal  = mConfig.sweepLastEffect;
    mMode        = Haptic::Mode::Sweep;
}

void Gui::stopSweep()
{
    auto *msg = mKernel.comm.allocateMessage<CustomMessage::SweepStop>();
    if (msg != nullptr) {
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}

void Gui::onSweepProgress(uint16_t effect, uint16_t total, bool finished, uint16_t samples)
{
    mSweepEffect = effect;
    mSweepTotal  = total;
    if (finished) {
        closeRun("sweep-finished");
        mMode = Haptic::Mode::Menu;
    }
    LOG_INFO("sweep effect %u of %u, %u samples\n", effect, total, samples);
}

void Gui::applyStep(const Haptic::Step &step, uint32_t nowMs)
{
    if (step.playPattern) {
        playPattern(step.pattern, step.trial, nowMs);
    }
    if (step.recordTrial && mRunOpen) {
        mRecorder.appendTrial(step.record, mPatterns);
    }
    if (step.finished) {
        closeRun(mSession.stage() == Haptic::Stage::Done ? "completed" : "abandoned");
    }
    // Deliberately no draw here: both callers push once at the end of their own
    // handler. A push costs 21 to 24 ms of a 100 ms frame, so drawing per step
    // as well would spend half the slack redrawing the same picture.
}

void Gui::onTick(uint32_t nowMs)
{
    switch (mMode) {
    case Haptic::Mode::ProbeSent:
        if (mProbeRunning) {
            advanceProbeStep(nowMs);
        } else if (Haptic::sinceTick(mProbeStepAt, nowMs) >= static_cast<int32_t>(kProbeSentMs)) {
            mMode = Haptic::Mode::Probe;
        }
        break;

    case Haptic::Mode::Session: {
        const Haptic::Step step = mSession.onTick(nowMs);
        applyStep(step, nowMs);
        // A run that has been going long enough to hit its cap stops itself
        // rather than filling the filesystem.
        if (mRunOpen && mRecorder.overDurationCap(nowMs)) {
            applyStep(mSession.abort(nowMs), nowMs);
            closeRun("cap-reached");
            mMode = Haptic::Mode::Menu;
        }
        break;
    }

    case Haptic::Mode::Menu:
    case Haptic::Mode::Probe:
    case Haptic::Mode::Sweep:
    case Haptic::Mode::Count:
        break;
    }

    renderAndPush();
}

void Gui::recordEvent(const SDK::Message::EventButton &button, uint32_t nowMs,
                      Haptic::EventDisposition disposition)
{
    if (!mRunOpen) {
        return;
    }
    Haptic::EventRecord row {};
    // `EventButton`'s own clock, not this app's read of the time at handling:
    // queue delay must not be charged to the wearer.
    row.buttonTimestamp = button.timestamp;
    row.tickAtHandling  = nowMs;
    row.wallClock       = wallClock();
    row.trialIndex = mMode == Haptic::Mode::Session ? mSession.trialIndex() : Haptic::kNoTrial;
    row.buttonId    = static_cast<uint8_t>(button.id);
    row.buttonEvent = static_cast<uint8_t>(button.event);
    row.disposition = disposition;
    mRecorder.appendEvent(row);
}

void Gui::onButton(const SDK::Message::EventButton &button, uint32_t nowMs)
{
    using Event = SDK::Message::EventButton::Event;
    const uint8_t id    = static_cast<uint8_t>(button.id);
    const uint8_t event = static_cast<uint8_t>(button.event);

    // Said once here rather than in every arm: the kernel sends PRESS and
    // RELEASE alongside every CLICK, and only CLICK means anything to this app.
    Haptic::EventDisposition disposition = button.event == Event::CLICK
                                               ? Haptic::EventDisposition::Unmapped
                                               : Haptic::EventDisposition::NotAClick;

    switch (mMode) {
    case Haptic::Mode::Menu:
        if (button.event == Event::CLICK) {
            disposition = Haptic::EventDisposition::TakenAsResponse;
            switch (id) {
            case Haptic::kButtonSlot0:
                openRun("probe");
                mMode       = Haptic::Mode::Probe;
                mProbeIndex = 0;
                break;
            case Haptic::kButtonSlot1:
                openRun("session");
                mMode = Haptic::Mode::Session;
                applyStep(mSession.begin(nowMs), nowMs);
                break;
            case Haptic::kButtonSlot2:
                openRun("sweep");
                startSweep();
                break;
            case Haptic::kButtonNotFelt:
                closeRun("abandoned");
                mKernel.sys.exit(0);
                return;
            default:
                disposition = Haptic::EventDisposition::Unmapped;
                break;
            }
        }
        break;

    case Haptic::Mode::Probe:
        if (button.event == Event::CLICK) {
            disposition = Haptic::EventDisposition::TakenAsResponse;
            switch (id) {
            case Haptic::kButtonSlot0:
                mProbeIndex = static_cast<uint8_t>((mProbeIndex + mProbeCount - 1) % mProbeCount);
                break;
            case Haptic::kButtonSlot1:
                runProbe(nowMs);
                break;
            case Haptic::kButtonSlot2:
                mProbeIndex = static_cast<uint8_t>((mProbeIndex + 1) % mProbeCount);
                break;
            case Haptic::kButtonNotFelt:
                closeRun("probe-finished");
                mMode = Haptic::Mode::Menu;
                break;
            default:
                disposition = Haptic::EventDisposition::Unmapped;
                break;
            }
        }
        break;

    case Haptic::Mode::ProbeSent:
        if (button.event == Event::CLICK) {
            disposition = Haptic::EventDisposition::AfterResponse;
        }
        break;

    case Haptic::Mode::Sweep:
        if (button.event == Event::CLICK && id == Haptic::kButtonNotFelt) {
            disposition = Haptic::EventDisposition::TakenAsResponse;
            stopSweep();
        }
        break;

    case Haptic::Mode::Session: {
        const Haptic::Step step = mSession.onButton(id, event, button.timestamp, nowMs, disposition);
        applyStep(step, nowMs);
        // A session runs once per launch. `applyStep` has already closed the
        // run out, and the next condition means a new `config.json`, so R2 on
        // the closing screen exits rather than returning to a menu whose R1
        // would do nothing.
        const bool closed = mSession.stage() == Haptic::Stage::Aborted ||
                            mSession.stage() == Haptic::Stage::Done;
        if (closed && button.event == Event::CLICK && id == Haptic::kButtonNotFelt) {
            disposition = Haptic::EventDisposition::TakenAsResponse;
            recordEvent(button, nowMs, disposition);
            mKernel.sys.exit(0);
            return;
        }
        break;
    }

    case Haptic::Mode::Count:
        break;
    }

    recordEvent(button, nowMs, disposition);
    renderAndPush();
}

void Gui::renderAndPush()
{
    if (!mResumed) {
        return;
    }

    Haptic::FrameInputs inputs;
    inputs.mode         = mMode;
    inputs.patterns     = mPatterns;
    inputs.patternCount = mPatternCount;
    inputs.plan         = &mPlan;
    inputs.session      = mMode == Haptic::Mode::Session ? &mSession : nullptr;
    inputs.probeIndex   = mProbeIndex;
    inputs.probeCount   = mProbeCount;
    inputs.sweepIndex   = mSweepEffect;
    inputs.sweepCount   = mSweepTotal;
    inputs.contact      = mConfig.contact;
    inputs.masking      = mConfig.masking;
    inputs.filesOk      = mFilesUsable;
    inputs.capHit       = mRecorder.capHit();
    inputs.durations    = mDurations;

    haptic_lab_gui_frame frame;
    Haptic::buildFrame(inputs, frame);

    haptic_lab_gui_render(mFrameBuf, kMaxPixels * kBytesPerPixel, static_cast<uint16_t>(mWidth),
                          static_cast<uint16_t>(mHeight), &frame);

    auto *upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (upd != nullptr) {
        upd->pBuffer = mFrameBuf;
        mKernel.comm.sendMessage(upd, kResponseTimeoutMs);
        mKernel.comm.releaseMessage(upd);
    }

    // The wearer answers the frame they can see, so the response window's
    // reference clock is when the frame went out rather than when the state
    // machine decided it should.
    if (mMode == Haptic::Mode::Session) {
        mSession.noteFramePushed(mKernel.sys.getTimeMs());
    }
}

void Gui::run()
{
    LOG_INFO("Started\n");

    if (haptic_lab_gui_abi_fingerprint() != haptic_lab_gui_abi::fingerprint()) {
        LOG_ERROR("ABI mismatch: Rust 0x%08X, C++ 0x%08X; stale libhaptic_lab_gui.a\n",
                  static_cast<unsigned>(haptic_lab_gui_abi_fingerprint()),
                  static_cast<unsigned>(haptic_lab_gui_abi::fingerprint()));
        mKernel.sys.exit(1);
        return;
    }

    note("start version=%s abi=%lu", HAPTICLAB_VERSION,
         static_cast<unsigned long>(gIKernel != nullptr ? gIKernel->version : 0));

    queryDisplayConfig();
    takeCapabilities();
    holdTheBacklight();
    loadConfig();
    buildTheSession();

    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, kWaitForever)) {
            continue;
        }

        switch (msg->getType()) {
        case SDK::MessageType::COMMAND_APP_STOP:
            // Plugging in USB terminates every running app, so this is where a
            // real session most often ends. Throwing the rows away would not
            // make them less real.
            closeRun("stopped-by-kernel");
            msg->setResult(SDK::MessageResult::SUCCESS);
            mKernel.comm.releaseMessage(msg);
            mKernel.sys.exit(0);
            return;

        case SDK::MessageType::COMMAND_APP_GUI_RESUME:
            mResumed = true;
            msg->setResult(SDK::MessageResult::SUCCESS);
            mKernel.comm.releaseMessage(msg);
            holdTheBacklight();
            renderAndPush();
            continue;

        case SDK::MessageType::COMMAND_APP_GUI_SUSPEND:
            mResumed = false;
            msg->setResult(SDK::MessageResult::SUCCESS);
            break;

        case SDK::MessageType::EVENT_GUI_TICK:
            msg->setResult(SDK::MessageResult::SUCCESS);
            mKernel.comm.releaseMessage(msg);
            onTick(mKernel.sys.getTimeMs());
            continue;

        case SDK::MessageType::EVENT_BUTTON: {
            auto *button = static_cast<SDK::Message::EventButton *>(msg);
            onButton(*button, mKernel.sys.getTimeMs());
            msg->setResult(SDK::MessageResult::SUCCESS);
        } break;

        case CustomMessage::SWEEP_PROGRESS: {
            auto *progress = static_cast<CustomMessage::SweepProgress *>(msg);
            onSweepProgress(progress->effect, progress->total, progress->finished != 0,
                            progress->samples);
            msg->setResult(SDK::MessageResult::SUCCESS);
            mKernel.comm.releaseMessage(msg);
            renderAndPush();
            continue;
        }

        default:
            msg->setResult(SDK::MessageResult::FAIL);
            mKernel.comm.sendResponse(msg);
            break;
        }

        if (msg->getResult() == SDK::MessageResult::PENDING) {
            msg->setResult(SDK::MessageResult::FAIL);
        }
        mKernel.comm.releaseMessage(msg);
    }
}
