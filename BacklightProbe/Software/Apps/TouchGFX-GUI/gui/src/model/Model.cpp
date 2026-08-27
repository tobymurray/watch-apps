#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <gui/common/FrontendApplication.hpp>

#include <cstring>

#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Port/TouchGFX/TouchGFXCommandProcessor.hpp"

#include "BacklightRequest.hpp"

#define LOG_MODULE_PRX      "Model"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

Model::Model()
    : modelListener(nullptr)
    , mKernel(SDK::KernelProviderGUI::GetInstance().getKernel())
{
    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(this);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(this);

    setCapabilities();

#if defined(SIMULATOR)
    std::string fsPath = SDK::Simulator::KernelHolder::Get().getFsPath();
    LOG_INFO("Simulator.\n");
    LOG_INFO("FS path: [%s].\n", fsPath.c_str());
    LOG_INFO("Buttons: 1=L1 2=L2 3=R1 4=R2\n\n");
#endif
}

// Controls

FrontendApplication& Model::application()
{
    return *static_cast<FrontendApplication*>(touchgfx::Application::getInstance());
}

uint32_t Model::liveStepElapsedMs() const
{
    if (!mStatus.everReceived) {
        return 0;
    }
    // Extrapolated from the snapshot's own elapsed value plus the time since it
    // arrived. Never the difference between two snapshots: the queue this
    // arrives through drops its oldest entry on overflow, and a counter derived
    // from two samples shows nonsense the moment one is lost.
    return mStatus.stepElapsedMs + (mKernel.sys.getTimeMs() - mStatus.receivedAtMs);
}

void Model::tick()
{
    // Done here rather than in the message handler: the send blocks for up to
    // its send timeout waiting on the kernel, and tick() is the GUI's own turn.
    if (mPendingSendSeq != 0) {
        doPendingGuiSend();
    }

    if (mInvalidate) {
        mInvalidate = false;
        application().invalidate();
    }
}

void Model::doPendingGuiSend()
{
    const uint32_t seq = mPendingSendSeq;
    mPendingSendSeq    = 0;

    LOG_INFO("GUI-sent backlight request seq=%lu b=%u auto_off=%lu\n",
             static_cast<unsigned long>(seq), static_cast<unsigned>(mPendingSendBrightness),
             static_cast<unsigned long>(mPendingSendAutoOffMs));

    // The same call the service makes, from the other process. That is the whole
    // experiment: if the result differs, the "GUI only" comment above this
    // message block reaches it after all.
    const Backlight::Outcome outcome = Backlight::request(
        mKernel, mPendingSendBrightness, mPendingSendAutoOffMs, mPendingSendTimeoutMs);

    Probe::sendGuiSendResult(mKernel, seq, outcome.sent, outcome.allocationFailed,
                             outcome.completed, static_cast<uint8_t>(outcome.result),
                             outcome.elapsedMs);
}

void Model::setCapabilities()
{
    auto* msg = mKernel.comm.allocateMessage<SDK::Message::RequestSetCapabilities>();
    if (msg) {
        // The USB charging screen stays enabled. It is the kernel's, and it is
        // what appears when the cable goes in, which is precisely the moment
        // this app stops running. Suppressing it would hide the one piece of
        // feedback that explains why a run ended early.
        msg->enUsbChargingScreen = true;

        // Phone notifications off. A notification during the run raises the
        // backlight on the kernel's own initiative, which is a confound in the
        // middle of an experiment about what raises the backlight, and one
        // that would be invisible in the results file afterwards.
        msg->enPhoneNotification = false;
        msg->enMusicControl      = false;
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}

void Model::startProbe()
{
    LOG_INFO("start requested\n");
    Probe::sendCommand(mKernel, CustomMessage::Command::Start);
}

void Model::exitApp()
{
    LOG_INFO("manually exiting the GUI\n");

    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(nullptr);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(nullptr);

    mKernel.sys.exit();
    // On simulator sys.exit() only sets a flag: the current tick completes normally.
}

// IGuiLifeCycleCallback

void Model::onStart()
{
    LOG_INFO("started\n");

    // The service publishes on an interval once its GUI is up, but the first one
    // may be a while away when nothing is running. Ask now, so the screen does
    // not open blank on an app whose whole job is saying what it is doing.
    Probe::sendCommand(mKernel, CustomMessage::Command::Resend);
}

void Model::onResume()
{
    mInvalidate = true;
    Probe::sendCommand(mKernel, CustomMessage::Command::Resend);
}

void Model::onSuspend()
{
    // The kernel may park the GUI at any time, and does whenever the screen
    // blanks. The service is untouched and the plan keeps running, so there is
    // nothing to save.
}

void Model::onStop()
{
    LOG_INFO("force exit from the application\n");
}

// ICustomMessageHandler
bool Model::customMessageHandler(SDK::MessageBase* msg)
{
    if (msg->getType() != CustomMessage::PROBE_STATUS) {
        return false;
    }

    const auto* status = static_cast<CustomMessage::ProbeStatus*>(msg);

    mStatus.stepElapsedMs  = status->stepElapsedMs;
    mStatus.stepDurationMs = status->stepDurationMs;
    mStatus.startedAtMs    = status->startedAtMs;
    mStatus.stepIndex      = status->stepIndex;
    mStatus.stepCount      = status->stepCount;
    mStatus.observeSteps   = status->observeSteps;
    mStatus.state          = static_cast<CustomMessage::ProbeState>(status->state);
    mStatus.action         = status->action;
    mStatus.lastResult     = status->lastResult;
    mStatus.quiet          = status->quiet;
    mStatus.registersAvailable = status->registersAvailable;
    mStatus.logIntact          = status->logIntact;

    std::memcpy(mStatus.label, status->label, CustomMessage::kLabelMax);
    mStatus.label[CustomMessage::kLabelMax - 1] = '\0';

    mStatus.receivedAtMs = mKernel.sys.getTimeMs();
    mStatus.everReceived = true;

    // A GUI-send asked for, and not the one already done. Latched here and
    // performed in tick(); see Model.hpp for why it is not done inline.
    if (status->guiSendSeq != 0 && status->guiSendSeq != mLastHandledSendSeq) {
        mLastHandledSendSeq    = status->guiSendSeq;
        mPendingSendSeq        = status->guiSendSeq;
        mPendingSendBrightness = status->guiBrightness;
        mPendingSendAutoOffMs  = status->guiAutoOffMs;
        mPendingSendTimeoutMs  = status->guiSendTimeoutMs;
    }

    if (modelListener) {
        modelListener->onStatusChanged(mStatus);
    }
    return true;
}
