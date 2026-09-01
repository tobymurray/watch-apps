#include "Service.hpp"

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include "DebugLog.hpp"

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

    // Diagnostic only, for this debug build: every example app in the SDK
    // does its filesystem/config work from the Service process, never the
    // GUI process (Barcode's AppConfig, Treadmill/Running/Cycling's
    // SettingsSerializer, all of it). NotifyToggle deviated from that and
    // did it GUI-side. This checks whether that deviation matters -- whether
    // Service's Kernel::fs is rooted somewhere different than GUI's turned
    // out to be (see gui-debug.log: one ".." from the GUI process lands on
    // what looks like the internal firmware volume, not "Apps/").
    DebugLog::setLogPath("service-debug.log");
    DebugLog::append(mKernel.fs, "=== NotifyToggle Service started (debug build) ===");
    DebugLog::listDirectory(mKernel.fs, "/");
    DebugLog::listDirectory(mKernel.fs, "../");
    DebugLog::listDirectory(mKernel.fs, "../../");
    DebugLog::probeDriveRoots(mKernel.fs);
    DebugLog::probeSharedData(mKernel.fs);
    DebugLog::probeTwoHopResolution(mKernel.fs);

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
