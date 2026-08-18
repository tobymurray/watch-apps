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
    LOG_INFO("Leaving the screen; the service keeps recording\n");

    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(nullptr);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(nullptr);

    mKernel.sys.exit();
    // On simulator sys.exit() only sets a flag -- the current tick completes.
}

// IGuiLifeCycleCallback

void Model::onStart()
{
    LOG_INFO("Started\n");
    // The service publishes after each scoring epoch, which can be a minute
    // away. Ask now rather than leave the screen blank that long.
    SDK::send_msg<CustomMessage::SleepRequest>(mKernel);
}

void Model::onResume()
{
    mInvalidate = true;
    SDK::send_msg<CustomMessage::SleepRequest>(mKernel);
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
    if (msg->getType() == CustomMessage::SLEEP_HISTORY) {
        return handleHistory(static_cast<CustomMessage::SleepHistory *>(msg));
    }

    if (msg->getType() != CustomMessage::SLEEP_REPORT) {
        return false;
    }

    mReport.data = static_cast<CustomMessage::SleepReport *>(msg)->data;
    mReport.everReceived = true;

    if (modelListener) {
        modelListener->onReportChanged(mReport);
    }
    return true;
}

bool Model::handleHistory(const CustomMessage::SleepHistory *chunk)
{
    // An empty history is announced as a single chunk carrying no rows, which
    // is the only way the GUI learns there are no nights yet -- as distinct
    // from a burst that never arrived.
    if (chunk->total == 0) {
        mHistory.count        = 0;
        mHistory.everReceived = true;
        mIncoming.count       = 0;
        if (modelListener) {
            modelListener->onHistoryChanged(mHistory);
        }
        return true;
    }

    // A burst starts at 0 and its chunks abut. Anything else means one was
    // dropped or two bursts interleaved, so wait for the next complete burst
    // rather than building a history out of pieces of two -- the queue this
    // arrives through discards its oldest entry when it overflows, so a gap
    // here is a real possibility rather than a theoretical one.
    if (chunk->firstIndex == 0) {
        mIncoming.count = 0;
    } else if (chunk->firstIndex != mIncoming.count) {
        mIncoming.count = 0;
        return true;
    }

    for (uint8_t i = 0; i < chunk->count; i++) {
        const uint8_t at = static_cast<uint8_t>(chunk->firstIndex + i);
        if (at >= kMaxHistory) {
            break;
        }
        const auto &src = chunk->rows[i];
        HistoryRow &dst = mIncoming.rows[at];
        dst.startUtcDays  = src.startUtcDays;
        dst.totalSleepMin = src.totalSleepMin;
        dst.efficiencyPct = src.efficiencyPct;
        dst.hrMinX10      = src.hrMinX10;
        dst.worn          = src.worn;
        dst.interrupted   = src.interrupted;
    }

    // Counted through even past the array, so the end of a burst is still
    // recognised on a watch carrying more nights than this GUI draws.
    mIncoming.count = static_cast<uint8_t>(chunk->firstIndex + chunk->count);

    if (mIncoming.count < chunk->total) {
        return true;   // more to come
    }

    mHistory = mIncoming;
    if (mHistory.count > kMaxHistory) {
        mHistory.count = kMaxHistory;
    }
    mHistory.everReceived = true;
    mIncoming.count       = 0;

    if (modelListener) {
        modelListener->onHistoryChanged(mHistory);
    }
    return true;
}
