#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <gui/common/FrontendApplication.hpp>

#include <cstring>

#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Port/TouchGFX/TouchGFXCommandProcessor.hpp"

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

uint32_t Model::liveElapsedMs() const
{
    if (!mStatus.everReceived) {
        return 0;
    }
    // Extrapolated from the snapshot's own elapsed value plus the time since it
    // arrived. Never the difference between two snapshots: the queue this
    // arrives through drops its oldest entry on overflow.
    return mStatus.elapsedMs + (mKernel.sys.getTimeMs() - mStatus.receivedAtMs);
}

void Model::tick()
{
    if (mInvalidate) {
        mInvalidate = false;
        application().invalidate();
    }
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

void Model::startPwm()
{
    LOG_INFO("start requested\n");
    Pwm::sendCommand(mKernel, CustomMessage::Command::Start);
}

void Model::stopPwm()
{
    LOG_INFO("stop requested\n");
    Pwm::sendCommand(mKernel, CustomMessage::Command::Stop);
}

void Model::exitApp()
{
    // Leaves the screen only. The service keeps the pin and keeps climbing:
    // the kernel blanking the display is one of the events that might make it
    // reassert PF3, and watching that happen is part of the experiment.
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
    Pwm::sendCommand(mKernel, CustomMessage::Command::Resend);
}

void Model::onResume()
{
    mInvalidate = true;
    Pwm::sendCommand(mKernel, CustomMessage::Command::Resend);
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
    if (msg->getType() != CustomMessage::PWM_STATUS) {
        return false;
    }

    const auto* status = static_cast<CustomMessage::PwmStatus*>(msg);

    mStatus.elapsedMs     = status->elapsedMs;
    mStatus.holdMs        = status->holdMs;
    mStatus.edges         = status->edges;
    mStatus.periods       = status->periods;
    mStatus.cyclesPerUs   = status->cyclesPerUs;
    mStatus.runElapsedMs  = status->runElapsedMs;
    mStatus.kernelWrites  = status->kernelWrites;
    mStatus.rungIndex     = status->rungIndex;
    mStatus.rungCount     = status->rungCount;
    mStatus.requestedDuty = status->requestedDuty;
    mStatus.pairDuty      = status->pairDuty;
    mStatus.achievedDuty  = status->achievedDuty;
    mStatus.state         = static_cast<CustomMessage::PwmState>(status->state);
    mStatus.driving       = status->driving;

    std::memcpy(mStatus.label, status->label, CustomMessage::kLabelMax);
    mStatus.label[CustomMessage::kLabelMax - 1] = '\0';

    mStatus.receivedAtMs = mKernel.sys.getTimeMs();
    mStatus.everReceived = true;

    if (modelListener) {
        modelListener->onStatusChanged(mStatus);
    }
    return true;
}
