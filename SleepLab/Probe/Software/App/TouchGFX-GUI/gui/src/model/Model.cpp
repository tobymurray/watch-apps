#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <gui/common/FrontendApplication.hpp>

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

FrontendApplication &Model::application()
{
    return *static_cast<FrontendApplication *>(touchgfx::Application::getInstance());
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
    auto *msg = mKernel.comm.allocateMessage<SDK::Message::RequestSetCapabilities>();
    if (msg) {
        msg->enUsbChargingScreen = true;
        msg->enPhoneNotification = false;
        msg->enMusicControl      = false;
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}

void Model::exitApp()
{
    LOG_INFO("Leaving the screen; the probe keeps recording\n");

    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(nullptr);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(nullptr);

    mKernel.sys.exit();
    // On simulator sys.exit() only sets a flag -- the current tick completes.
}

// IGuiLifeCycleCallback

void Model::onStart()
{
    LOG_INFO("Started\n");
    // The service publishes after each row, which can be up to a minute away.
    // Ask now rather than leave the screen blank for that long.
    SDK::send_msg<CustomMessage::ProbeRequest>(mKernel);
}

void Model::onResume()
{
    mInvalidate = true;
    SDK::send_msg<CustomMessage::ProbeRequest>(mKernel);
}

void Model::onSuspend()
{
    // The kernel may park the GUI at any time. The service is untouched and
    // keeps recording, so there is nothing to save.
}

void Model::onStop()
{
    LOG_INFO("Force exit from the application\n");
}

// ICustomMessageHandler

bool Model::customMessageHandler(SDK::MessageBase *msg)
{
    if (msg->getType() != CustomMessage::PROBE_STATUS) {
        return false;
    }

    const auto *s = static_cast<CustomMessage::ProbeStatus *>(msg);

    mStatus.rowsWritten   = s->rowsWritten;
    mStatus.rowFailures   = s->rowFailures;
    mStatus.bytesWritten  = s->bytesWritten;
    mStatus.runningMs     = s->runningMs;
    mStatus.subscribed    = s->subscribed;
    mStatus.hrMode        = s->hrMode;
    mStatus.lastAccN      = s->lastAccN;
    mStatus.lastHrN       = s->lastHrN;
    mStatus.lastTouchWorn = s->lastTouchWorn;
    mStatus.lastTouchN    = s->lastTouchN;
    mStatus.totalBeatN    = s->totalBeatN;
    mStatus.totalSpo2N    = s->totalSpo2N;
    mStatus.battPctX10    = s->battPctX10;
    mStatus.charging      = s->charging;
    mStatus.usb           = s->usb;
    mStatus.everReceived  = true;

    if (modelListener) {
        modelListener->onStatusChanged(mStatus);
    }
    return true;
}
