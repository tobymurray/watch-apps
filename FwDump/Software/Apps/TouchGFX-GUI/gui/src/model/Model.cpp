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

// Controls

FrontendApplication& Model::application()
{
    return *static_cast<FrontendApplication*>(touchgfx::Application::getInstance());
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
        // The USB charging screen stays enabled. It is the kernel's, not ours,
        // and it is what appears when the cable goes in -- which is precisely
        // the moment this app stops running. Suppressing it would hide the one
        // piece of feedback that explains why the dump stopped.
        msg->enUsbChargingScreen = true;
        msg->enPhoneNotification = false;
        msg->enMusicControl      = false;
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}

void Model::startDump()
{
    LOG_INFO("start requested\n");
    FwDump::sendCommand(mKernel, CustomMessage::DumpCommand::Start);
}

void Model::exitApp()
{
    LOG_INFO("manually exiting the GUI\n");

    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(nullptr);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(nullptr);

    mKernel.sys.exit();
    // On simulator sys.exit() only sets a flag -- the current tick completes normally.
}

// IGuiLifeCycleCallback

void Model::onStart()
{
    LOG_INFO("started\n");

    // The service publishes on an interval once its GUI is up, but the first one
    // may be up to kPublishPeriodMs away -- and if no dump is running it is
    // longer than that. Ask now, so the screen does not open blank on an app
    // whose entire job is telling you what it is doing.
    FwDump::sendCommand(mKernel, CustomMessage::DumpCommand::Resend);
}

void Model::onResume()
{
    mInvalidate = true;
    FwDump::sendCommand(mKernel, CustomMessage::DumpCommand::Resend);
}

void Model::onSuspend()
{
    // The kernel may park the GUI at any time -- and does, every time the screen
    // blanks during a dump. The service is untouched and keeps going, so there
    // is nothing to save here.
}

void Model::onStop()
{
    LOG_INFO("force exit from the application\n");
}

// ICustomMessageHandler
bool Model::customMessageHandler(SDK::MessageBase* msg)
{
    if (msg->getType() != CustomMessage::FWDUMP_STATUS) {
        return false;
    }

    const auto* status = static_cast<CustomMessage::FwDumpStatus*>(msg);

    mStatus.bytesDone      = status->bytesDone;
    mStatus.bytesTotal     = status->bytesTotal;
    mStatus.elapsedMs      = status->elapsedMs;
    mStatus.etaSec         = status->etaSec;
    mStatus.kbPerSec       = status->kbPerSec;
    mStatus.wholeCrc       = status->wholeCrc;
    mStatus.regionBase     = status->regionBase;
    mStatus.regionSize     = status->regionSize;
    mStatus.stalledMs      = status->stalledMs;
    mStatus.chunksDone     = status->chunksDone;
    mStatus.chunksTotal    = status->chunksTotal;
    mStatus.chunksVerified = status->chunksVerified;
    mStatus.chunksPresent  = status->chunksPresent;
    mStatus.errorChunk     = status->errorChunk;
    mStatus.state          = static_cast<CustomMessage::DumpState>(status->state);
    mStatus.error          = static_cast<CustomMessage::DumpError>(status->error);
    mStatus.configStatus   = status->configStatus;
    mStatus.scanComplete   = status->scanComplete;
    mStatus.everReceived   = true;

    if (modelListener) {
        modelListener->onStatusChanged(mStatus);
    }
    return true;
}
