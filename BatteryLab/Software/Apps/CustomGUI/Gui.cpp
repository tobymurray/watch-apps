#include "Gui.hpp"

#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include "Commands.hpp"
#include "batterylab_gui.h"

#define LOG_MODULE_PRX   "BatteryLabGui"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

static constexpr uint32_t kWaitForever = 0xFFFFFFFF;

extern "C" void panickit_host_panic(const uint8_t *msg, uint32_t len)
{
    LOG_ERROR("Rust panic: %.*s\n", static_cast<int>(len),
              reinterpret_cast<const char *>(msg));
    SDK::KernelProviderGUI::GetInstance().getKernel().sys.exit(1);
    while (true) {
    }
}

Gui::Gui(SDK::Kernel &kernel)
    : mKernel(kernel)
{
    mState.pct_x1000        = BATTERYLAB_NONE;
    mState.mv               = BATTERYLAB_NONE;
    mState.ma_x1000         = BATTERYLAB_NONE;
    mState.avg_ma_x1000     = BATTERYLAB_NONE;
    mState.mah_x1000        = BATTERYLAB_NONE;
    mState.design_mah_x1000 = BATTERYLAB_NONE;
    mState.connected        = BATTERYLAB_NONE;
    mState.charging         = BATTERYLAB_NONE;
}

void Gui::queryDisplayConfig()
{
    auto *cfg = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayConfig>();
    if (cfg) {
        if (mKernel.comm.sendMessage(cfg, kResponseTimeoutMs) &&
            cfg->getResult() == SDK::MessageResult::SUCCESS) {
            mWidth      = cfg->width;
            mHeight     = cfg->height;
            mColorDepth = cfg->colorDepth;
        }
        mKernel.comm.releaseMessage(cfg);
    }

    const bool fitsFramebuffer =
        mWidth > 0 && mHeight > 0 &&
        static_cast<uint32_t>(mWidth) * static_cast<uint32_t>(mHeight) <= kMaxPixels;
    if (!fitsFramebuffer) {
        mWidth  = kFallbackWidth;
        mHeight = kFallbackHeight;
    }
    // More than a byte a pixel would have the kernel read past mFrameBuf.
    mDisplayUsable = mColorDepth <= kMaxBitsPerPixel;
}

void Gui::renderAndPush()
{
    if (!mResumed || !mDisplayUsable || !mDirty) {
        return;
    }
    mDirty = false;

    batterylab_render(mFrameBuf, kMaxPixels, static_cast<uint16_t>(mWidth),
                      static_cast<uint16_t>(mHeight), &mState);

    auto *upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (upd) {
        upd->pBuffer = mFrameBuf;
        mKernel.comm.sendMessage(upd, kResponseTimeoutMs);
        mKernel.comm.releaseMessage(upd);
    }
}

void Gui::run()
{
    if (batterylab_abi_fingerprint() != batterylab_abi::fingerprint()) {
        LOG_ERROR("ABI mismatch: Rust 0x%08X, C++ 0x%08X -- stale libbatterylab_gui.a\n",
                  static_cast<unsigned>(batterylab_abi_fingerprint()),
                  static_cast<unsigned>(batterylab_abi::fingerprint()));
        mKernel.sys.exit(1);
        return;
    }

    queryDisplayConfig();
    SDK::send_msg<CustomMessage::StatusRequest>(mKernel);

    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, kWaitForever)) {
            continue;
        }

        switch (msg->getType()) {
            case SDK::MessageType::COMMAND_APP_STOP:
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                mKernel.sys.exit(0);
                return;

            case SDK::MessageType::COMMAND_APP_GUI_RESUME:
                mResumed = true;
                mDirty   = true;
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            case SDK::MessageType::COMMAND_APP_GUI_SUSPEND:
                mResumed = false;
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            case SDK::MessageType::EVENT_GUI_TICK:
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue;

            case CustomMessage::STATUS: {
                const auto *st = static_cast<CustomMessage::Status *>(msg);
                mState.pct_x1000        = st->latest.pctX1000;
                mState.mv               = st->latest.mv;
                mState.ma_x1000         = st->latest.maX1000;
                mState.avg_ma_x1000     = st->latest.avgMaX1000;
                mState.mah_x1000        = st->latest.mahX1000;
                mState.design_mah_x1000 = st->latest.designMahX1000;
                mState.connected        = st->latest.connected;
                mState.charging         = st->latest.charging;
                mState.lines            = st->lines;
                mState.write_fails      = st->writeFails;
                mState.service_up_min   = st->serviceUpMin;
                mState.have_status      = 1;
                mDirty = true;
                msg->setResult(SDK::MessageResult::SUCCESS);
            } break;

            case SDK::MessageType::EVENT_BUTTON: {
                auto *btn = static_cast<SDK::Message::EventButton *>(msg);
                using Id    = SDK::Message::EventButton::Id;
                using Event = SDK::Message::EventButton::Event;
                if (btn->event == Event::CLICK && btn->id == Id::SW4) {
                    msg->setResult(SDK::MessageResult::SUCCESS);
                    mKernel.comm.releaseMessage(msg);
                    mKernel.sys.exit(0);
                    return;
                }
                msg->setResult(SDK::MessageResult::SUCCESS);
            } break;

            default:
                msg->setResult(SDK::MessageResult::FAIL);
                mKernel.comm.sendResponse(msg);
                break;
        }

        if (msg->getResult() == SDK::MessageResult::PENDING) {
            msg->setResult(SDK::MessageResult::FAIL);
        }
        mKernel.comm.releaseMessage(msg);
    }
}
