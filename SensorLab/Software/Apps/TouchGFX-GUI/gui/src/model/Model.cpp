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
    LOG_INFO("Buttons: 1=L1 scroll up  2=L2 scroll down  3=R1 run  4=R2 exit\n");
    LOG_INFO("The simulator has four sensor sources and thins delivery in a way "
             "the hardware does not. It exercises this screen and nothing "
             "else.\n\n");
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
    LOG_INFO("Leaving the screen; an open soak keeps recording\n");

    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(nullptr);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(nullptr);

    mKernel.sys.exit();
    // On simulator sys.exit() only sets a flag -- the current tick completes.
}

void Model::requestUpdate()
{
    // Also the evidence that a GUI is attached, which is what the service
    // relies on: the simulator does not deliver COMMAND_APP_NOTIF_GUI_RUN to a
    // service (ledger row T5), and a message only a GUI can send is better
    // evidence than the notification anyway.
    SDK::send_msg<CustomMessage::SensorLabRequest>(mKernel);
}

void Model::send(CustomMessage::Command command)
{
    auto msg = SDK::make_msg<CustomMessage::SensorLabCommand>(mKernel);
    if (!msg) {
        return;
    }
    msg->command = static_cast<uint8_t>(command);
    msg.send();
}

// IGuiLifeCycleCallback

void Model::onStart()
{
    LOG_INFO("Started\n");
    // The service publishes after each interval, which can be a minute away.
    // Ask now rather than leave the screen blank for that long.
    requestUpdate();
}

void Model::onResume()
{
    mInvalidate = true;
    requestUpdate();
}

void Model::onSuspend()
{
    // The kernel may park the GUI at any time. The service is untouched and an
    // open soak keeps recording, so there is nothing to save.
}

void Model::onStop()
{
    LOG_INFO("Force exit from the application\n");
}

// ICustomMessageHandler

bool Model::customMessageHandler(SDK::MessageBase *msg)
{
    switch (msg->getType()) {
        case CustomMessage::SENSORLAB_STATUS: {
            const auto *s = static_cast<CustomMessage::SensorLabStatus *>(msg);
            mState.status = s->data;
            mState.status.everReceived = true;
            if (modelListener) {
                modelListener->onStateChanged(mState);
            }
            return true;
        }

        case CustomMessage::SENSORLAB_ROSTER: {
            const auto *r = static_cast<CustomMessage::SensorRoster *>(msg);
            mState.total = r->total;
            // By index, never by arrival order. A burst that lost its middle
            // message leaves a gap here rather than shifting every row after it.
            for (uint8_t i = 0; i < r->count
                                && i < CustomMessage::kRosterRowsPerMsg; i++) {
                const uint8_t idx = r->rows[i].typeIdx;
                if (idx < SensorLab::Catalogue::kTypeCount) {
                    mState.rows[idx]     = r->rows[i];
                    mState.rowsSeen[idx] = true;
                }
            }
            if (modelListener) {
                modelListener->onStateChanged(mState);
            }
            return true;
        }

        default:
            return false;
    }
}
