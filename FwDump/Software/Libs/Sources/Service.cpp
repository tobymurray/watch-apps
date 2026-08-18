/**
 ******************************************************************************
 * @file    Service.cpp
 * @brief   The run loop, and what it tells the screen.
 ******************************************************************************
 */

#include "Service.hpp"

#include "SDK/Messages/MessageGuard.hpp"

#include "DeviceContext.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace {

/// The dumper's state as the wire enum. Written as a switch with no default so
/// that adding a FlashDumper::State the wire does not describe is a compile
/// error here rather than a screen quietly showing "idle" during a dump.
CustomMessage::DumpState toWireState(FlashDumper::State state)
{
    switch (state) {
        case FlashDumper::State::Idle:     return CustomMessage::DumpState::Idle;
        case FlashDumper::State::Checking: return CustomMessage::DumpState::Checking;
        case FlashDumper::State::Dumping:  return CustomMessage::DumpState::Dumping;
        case FlashDumper::State::Done:     return CustomMessage::DumpState::Done;
        case FlashDumper::State::Error:    return CustomMessage::DumpState::Error;
    }
    return CustomMessage::DumpState::Error;
}

/// Likewise for the error, and for the same reason: a new failure mode must not
/// be able to reach the screen as "None".
CustomMessage::DumpError toWireError(FlashDumper::Error error)
{
    switch (error) {
        case FlashDumper::Error::None:             return CustomMessage::DumpError::None;
        case FlashDumper::Error::BadRegion:        return CustomMessage::DumpError::BadRegion;
        case FlashDumper::Error::OpenFailed:       return CustomMessage::DumpError::OpenFailed;
        case FlashDumper::Error::ShortWrite:       return CustomMessage::DumpError::ShortWrite;
        case FlashDumper::Error::VerifyFailed:     return CustomMessage::DumpError::VerifyFailed;
        case FlashDumper::Error::ManifestFailed:   return CustomMessage::DumpError::ManifestFailed;
        case FlashDumper::Error::ManifestOverflow: return CustomMessage::DumpError::ManifestOverflow;
    }
    return CustomMessage::DumpError::None;
}

} // namespace

Service::Service(SDK::Kernel& kernel)
    : mKernel(kernel)
{
    configure();
}

// Runs from the constructor, and therefore says nothing.
//
// On a simulator build the service is constructed (simulator/main.cpp) *before*
// the TouchGFX HAL is set up, and the SDK's mock logger routes LOG_INFO through
// touchgfx_printf, which dereferences the HAL singleton. A single log line
// anywhere on this path segfaults the simulator during startup, with no output
// at all to say why -- which is exactly how this app's first simulator run
// died. DumpConfig is kept logger-free for the same reason.
//
// Everything worth saying is said from run(), which the SDK calls once the HAL
// exists. The work itself stays here so that mDumper is valid from construction,
// which nextWaitMs() and poll() both assume.
void Service::configure()
{
    const DumpConfig::Result config = DumpConfig::load(mKernel);
    mRegion       = config.region;
    mConfigStatus = config.status;

#if defined(SIMULATOR)
    // No flash to read here. A deterministic pattern in the first ~2 MB and
    // erased 0xFF beyond mirrors what the verified prior dump found real flash
    // to look like, so the screen shows realistic progress and the CRCs are
    // stable run to run.
    mSyntheticFlash.assign(mRegion.size, 0xFF);
    uint32_t state = 0x1F2E3D4Cu;
    const uint32_t imageBytes = mRegion.size < 0x0020A140u ? mRegion.size : 0x0020A140u;
    for (uint32_t i = 0; i < imageBytes; ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        mSyntheticFlash[i] = static_cast<uint8_t>(state & 0xFFu);
    }
    mWindow = mSyntheticFlash.data();
#else
    // The identity mapping, and the only place this app turns an address into a
    // pointer. Internal flash is memory-mapped, so this is the whole of the
    // "read arbitrary memory" primitive the app rests on.
    mWindow = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(mRegion.base));
#endif

    mDumper.reset(new FlashDumper(mKernel, mRegion, mWindow));
}

void Service::run()
{
    LOG_INFO("started\n");

    // Before anything else: capture the context a flash image cannot carry about
    // itself -- which unit, which die, whether isolation was off, the option
    // bytes, and a raw sweep of RCC/GPIO/NVIC. Written to a file as well as
    // logged, because the log needs a UART capture and the file does not; the
    // first real dump taken with this app lost its register context exactly that
    // way. See DeviceContext.hpp.
    //
    // Done at start, so merely launching the app records the context even if no
    // dump is ever run -- which also makes it cheap to capture the same data
    // from a firmware version you are about to replace.
    const DeviceContext::Result context = DeviceContext::read(mKernel);
    DeviceContext::log(context);
    if (!DeviceContext::write(mKernel, context, mRegion, DumpConfig::describe(mConfigStatus),
                              mKernel.sys.getTimeMs())) {
        // Worth a line, not worth stopping for: the dump is the job, and the
        // context is a courtesy the dump does not depend on.
        LOG_INFO("could not write %s\n", DeviceContext::kPath);
    }

    LOG_INFO("region base=%08lX size=%08lX chunk=%08lX subwrite=%08lX (%s)\n",
             static_cast<unsigned long>(mRegion.base), static_cast<unsigned long>(mRegion.size),
             static_cast<unsigned long>(mRegion.chunk),
             static_cast<unsigned long>(mRegion.subwrite), DumpConfig::describe(mConfigStatus));

#if defined(SIMULATOR)
    // Said here rather than where the buffer is filled, and said loudly: on this
    // build the dump is of a synthetic pattern, so a DONE and a matching CRC
    // prove the machinery works and prove nothing whatever about the watch's
    // flash.
    LOG_INFO("SIMULATOR: %lu bytes of synthetic pattern stand in for %08lX -- "
             "this dumps no real flash\n",
             static_cast<unsigned long>(mSyntheticFlash.size()),
             static_cast<unsigned long>(mRegion.base));
#endif

    // Find out what is already on disk without being asked. The screen's first
    // frame is then able to say "12/32 already done" rather than offering a
    // fresh start that would silently redo completed work.
    mDumper->beginScan();
    mLastSliceAtMs = mKernel.sys.getTimeMs();

    while (true) {
        SDK::MessageBase* msg;
        if (mKernel.comm.getMessage(msg, nextWaitMs())) {
            switch (msg->getType()) {
                case SDK::MessageType::COMMAND_APP_STOP:
                    LOG_INFO("force exit from the application\n");
                    // Deliberately no cancel(): the chunk in flight is
                    // abandoned, but every chunk already flushed stays on disk
                    // and the next run resumes from them.
                    mKernel.comm.releaseMessage(msg);
                    return;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                    LOG_INFO("GUI is now running\n");
                    mGuiStarted = true;
                    publish();
                    break;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                    LOG_INFO("GUI has stopped\n");
                    mGuiStarted = false;
                    // Does NOT end the service, and does not stop the dump. The
                    // screen blanks minutes before a 4 MB dump finishes; losing
                    // the dump with it would make the app pointless.
                    break;

                default:
                    handleCommand(msg);
                    break;
            }
            mKernel.comm.releaseMessage(msg);
        }

        poll();
    }
}

void Service::poll()
{
    const uint32_t nowMs = mKernel.sys.getTimeMs();

    // A gap far longer than a slice means the app was stopped, not merely slow.
    // Only meaningful mid-dump: an idle service waits kIdleWaitMs by design, and
    // reporting that as a stall would cry wolf on every idle second.
    if (mDumper->state() == FlashDumper::State::Dumping
            && (nowMs - mLastSliceAtMs) > kStallThresholdMs) {
        mStalledMs = nowMs - mLastSliceAtMs;
        LOG_INFO("lost %lu ms mid-dump -- was USB connected?\n",
                 static_cast<unsigned long>(mStalledMs));
    }
    mLastSliceAtMs = nowMs;

    mDumper->step(kSliceBudgetBytes);

    // A state change is published at once rather than waiting out the throttle:
    // DONE is the one word the user is waiting for, and it is also the signal
    // that the cable is safe to connect.
    const FlashDumper::State state = mDumper->state();
    if (state != mLastPublishedState) {
        if (state == FlashDumper::State::Dumping) {
            mStartedAtMs = nowMs;
        }
        publish();
        return;
    }

    if (mGuiStarted && (nowMs - mLastPublishAtMs) >= kPublishPeriodMs) {
        publish();
    }
}

uint32_t Service::nextWaitMs() const
{
    switch (mDumper->state()) {
        case FlashDumper::State::Checking:
        case FlashDumper::State::Dumping:
            return kBusyWaitMs;

        case FlashDumper::State::Idle:
        case FlashDumper::State::Done:
        case FlashDumper::State::Error:
            // Nothing to get back to. The wait ends the moment a message
            // arrives, so this bounds how long an idle service sleeps, not how
            // fast it answers a Start.
            return kIdleWaitMs;
    }
    return kIdleWaitMs;
}

void Service::handleCommand(SDK::MessageBase* msg)
{
    if (msg->getType() != CustomMessage::FWDUMP_COMMAND) {
        return; // Not one of ours.
    }

    const auto* command = static_cast<CustomMessage::FwDumpCommand*>(msg);
    switch (static_cast<CustomMessage::DumpCommand>(command->command)) {
        case CustomMessage::DumpCommand::Start:
            handleStart();
            break;

        case CustomMessage::DumpCommand::Resend:
            publish();
            break;
    }
}

void Service::handleStart()
{
    // A press while the dump is already running, or while the presence scan is,
    // is a no-op rather than a restart. FlashDumper enforces this too; saying it
    // here as well keeps the log honest about what was ignored and why.
    if (mDumper->state() == FlashDumper::State::Dumping
            || mDumper->state() == FlashDumper::State::Checking) {
        LOG_INFO("start ignored: already running\n");
        return;
    }

    LOG_INFO("start: %u of %u chunks already on disk\n", mDumper->chunksPresent(),
             mRegion.nchunks());
    mStalledMs   = 0;
    mStartedAtMs = mKernel.sys.getTimeMs();
    mDumper->beginDump();
    publish();
}

void Service::publish()
{
    mLastPublishAtMs    = mKernel.sys.getTimeMs();
    mLastPublishedState = mDumper->state();

    if (!mGuiStarted) {
        // Nobody is listening. Stamping the throttle above anyway means a GUI
        // that opens in a moment gets a fresh snapshot from its own
        // COMMAND_APP_NOTIF_GUI_RUN rather than a stale one.
        return;
    }

    // Built through the guard rather than the message's own constructor: this
    // app targets SDK 1.3, where allocateMessage cannot forward constructor
    // arguments, so the snapshot is assigned into a default-constructed message.
    auto msg = SDK::make_msg<CustomMessage::FwDumpStatus>(mKernel);
    if (!msg) {
        return; // Pool exhausted; the next tick will try again.
    }

    const uint64_t bytesDone = mDumper->bytesDone();

    msg->state          = static_cast<uint8_t>(toWireState(mDumper->state()));
    msg->error          = static_cast<uint8_t>(toWireError(mDumper->error()));
    msg->configStatus   = static_cast<uint8_t>(mConfigStatus);
    msg->scanComplete   = mDumper->scanComplete();
    msg->bytesDone      = bytesDone;
    msg->bytesTotal     = mDumper->bytesTotal();
    msg->chunksDone     = static_cast<uint16_t>(mDumper->chunksDone());
    msg->chunksTotal    = static_cast<uint16_t>(mRegion.nchunks());
    msg->chunksVerified = static_cast<uint16_t>(mDumper->chunksVerified());
    msg->chunksPresent  = static_cast<uint16_t>(mDumper->chunksPresent());
    msg->errorChunk     = static_cast<uint16_t>(mDumper->errorChunk());
    msg->wholeCrc       = mDumper->wholeCrc();
    msg->regionBase     = mRegion.base;
    msg->regionSize     = mRegion.size;
    msg->stalledMs      = mStalledMs;

    // Rate and ETA are computed here, not on the screen: a GUI that derived
    // them by differencing two snapshots would show nonsense whenever one was
    // dropped, and the queue it reads from discards its oldest entry when full.
    const uint32_t elapsedMs =
        (mStartedAtMs == 0) ? 0 : (mKernel.sys.getTimeMs() - mStartedAtMs);
    msg->elapsedMs = elapsedMs;

    if (elapsedMs > 0 && bytesDone > 0) {
        msg->kbPerSec = static_cast<uint32_t>((bytesDone * 1000ull) / (1024ull * elapsedMs));

        const uint64_t remaining = mDumper->bytesTotal() - bytesDone;
        msg->etaSec = static_cast<uint32_t>((remaining * elapsedMs) / (bytesDone * 1000ull));
    }
    // else: 0/0, which the screen renders as "--" rather than as an instant
    // finish or an infinite wait. The first slice or two of a pass extrapolate
    // to nonsense from a handful of bytes.

    msg.send();
}
