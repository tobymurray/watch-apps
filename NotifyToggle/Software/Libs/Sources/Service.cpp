#include "Service.hpp"

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#define LOG_MODULE_PRX   "NotifySvc"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

static constexpr uint32_t kWaitForever = 0xFFFFFFFF;

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
{
}

void Service::run()
{
    LOG_INFO("Started\n");

    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, kWaitForever)) {
            continue;
        }

        switch (msg->getType()) {
            case SDK::MessageType::COMMAND_APP_STOP:
            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                LOG_INFO("Exiting\n");
                mKernel.comm.releaseMessage(msg);
                return;

            default:
                msg->setResult(SDK::MessageResult::FAIL);
                break;
        }

        mKernel.comm.releaseMessage(msg);
    }
}
