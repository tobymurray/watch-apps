#include "Service.hpp"

// MessageBase and MessageType are complete only here: Kernel.hpp forward
// declares them, which is enough to hold a queue but not to switch on one.
#include "SDK/Messages/MessageGuard.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{
/// Nothing here is periodic, so the wait is unbounded: the chip reaches its
/// low-power state and the loop wakes only for a message that matters.
constexpr uint32_t kWaitForever = 0xFFFFFFFF;
} // namespace

void Service::run()
{
    LOG_INFO("Started; all measurement happens in the GUI\n");

    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, kWaitForever)) {
            continue;
        }

        switch (msg->getType()) {
            case SDK::MessageType::COMMAND_APP_STOP:
                LOG_INFO("Force exit\n");
                mKernel.comm.releaseMessage(msg);
                return;

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                LOG_INFO("Screen closed; nothing left to stay alive for\n");
                mKernel.comm.releaseMessage(msg);
                return;

            default:
                break;
        }

        mKernel.comm.releaseMessage(msg);
    }
}
