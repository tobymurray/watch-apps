#include "Service.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

static constexpr uint32_t kWaitForever = 0xFFFFFFFF;

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mSender(kernel)
    , mState(Barcode::makeDefaultState())
{
}

void Service::run()
{
    LOG_INFO("Started\n");

    while (true) {
        SDK::MessageBase *msg;
        if (!mKernel.comm.getMessage(msg, kWaitForever)) {
            continue;
        }

        switch (msg->getType()) {
            case SDK::MessageType::COMMAND_APP_STOP:
                LOG_INFO("Force exit from the application\n");
                // We must release message because this is the last event.
                mKernel.comm.releaseMessage(msg);
                return;

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                LOG_INFO("GUI is now running\n");
                publish();
                break;

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                LOG_INFO("GUI has stopped\n");
                // Nothing here changes on its own, so unlike Stopwatch there
                // is no reason to keep the thread resident once the GUI is
                // gone.
                mKernel.comm.releaseMessage(msg);
                return;

            default:
                handleCommand(msg);
                break;
        }

        mKernel.comm.releaseMessage(msg);
    }
}

void Service::handleCommand(SDK::MessageBase *msg)
{
    switch (msg->getType()) {
        case CustomMessage::BARCODE_SET_ID:
            mState = static_cast<CustomMessage::BarcodeSetId *>(msg)->state;
            break;

        case CustomMessage::BARCODE_REQUEST:
            // A plain request for the current state; publish() below answers it.
            break;

        default:
            // Not one of ours -- nothing to publish.
            return;
    }

    publish();
}

void Service::publish()
{
    mSender.state(mState);
}
