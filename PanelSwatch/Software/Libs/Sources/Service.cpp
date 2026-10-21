#include "Service.hpp"

#include "Crumb.hpp"

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

static constexpr uint32_t kWaitForever = 0xFFFFFFFF;

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
{
}

void Service::run()
{
    crumb(mKernel, "svc start");
    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, kWaitForever)) {
            continue;
        }
        const auto type = msg->getType();
        crumb(mKernel, "svc msg type=%d", static_cast<int>(type));
        if (type != SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN) {
            msg->setResult(SDK::MessageResult::FAIL);
        }
        mKernel.comm.releaseMessage(msg);
        if (type == SDK::MessageType::COMMAND_APP_STOP ||
            type == SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP) {
            return;
        }
    }
}
