#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <gui/common/FrontendApplication.hpp>

#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Port/TouchGFX/TouchGFXCommandProcessor.hpp"

#include <cstring>

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
    LOG_INFO("Manually exiting the application\n");

    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(nullptr);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(nullptr);

    mKernel.sys.exit();
    // On simulator sys.exit() only sets a flag -- the current tick completes normally.
}

// IGuiLifeCycleCallback

void Model::onStart()
{
    LOG_INFO("Started\n");

    // The service publishes a snapshot periodically once its GUI is up, but
    // that first one may be up to kPublishPeriodMs away -- ask for one now
    // so the screen doesn't sit blank that whole time.
    MapManager::sendMsg<CustomMessage::MapManagerRequest>(mKernel);
}

void Model::onResume()
{
    mInvalidate = true;
    MapManager::sendMsg<CustomMessage::MapManagerRequest>(mKernel);
}

void Model::onSuspend()
{
    // The kernel may park the GUI at any time. The service is untouched and
    // keeps verifying, so there is nothing to save here.
}

void Model::onStop()
{
    LOG_INFO("Force exit from the application\n");
}

// ICustomMessageHandler
bool Model::customMessageHandler(SDK::MessageBase *msg)
{
    if (msg->getType() != CustomMessage::MAP_MANAGER_PROGRESS) {
        return false;
    }

    const auto *progress = static_cast<CustomMessage::MapManagerProgress *>(msg);
    std::memcpy(mProgress.packName, progress->packName, sizeof(mProgress.packName));
    mProgress.bytesDone     = progress->bytesDone;
    mProgress.bytesTotal    = progress->bytesTotal;
    mProgress.elapsedMs     = progress->elapsedMs;
    mProgress.packsVerified = progress->packsVerified;
    mProgress.packsTotal    = progress->packsTotal;
    mProgress.anyInProgress = progress->anyInProgress;
    mProgress.everReceived  = true;

    if (modelListener) {
        modelListener->onProgressChanged(mProgress);
    }
    return true;
}
