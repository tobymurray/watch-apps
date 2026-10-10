#include "Gui.hpp"

#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include "Commands.hpp"
#include "slumber_gui.h"

#define LOG_MODULE_PRX   "SlumberGui"
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

    slumber_render(mFrameBuf, kMaxPixels, static_cast<uint16_t>(mWidth),
                   static_cast<uint16_t>(mHeight), &mState);

    auto *upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (upd) {
        upd->pBuffer = mFrameBuf;
        mKernel.comm.sendMessage(upd, kResponseTimeoutMs);
        mKernel.comm.releaseMessage(upd);
    }
}

void Gui::sendMark()
{
    auto mark = SDK::make_msg<CustomMessage::Mark>(mKernel);
    if (!mark) {
        return;
    }
    mark->code = mState.selected;
    slumber_mark_label(mState.selected, reinterpret_cast<uint8_t *>(mark->label),
                       CustomMessage::kMarkLabelCap);
    if (mark.send()) {
        mMarkPending = true;
    }
}

void Gui::move(int step)
{
    const uint32_t n = slumber_mark_count();
    if (n == 0) {
        return;
    }
    mState.selected    = static_cast<uint8_t>((mState.selected + n + step) % n);
    mState.just_logged = 0;
    mDirty             = true;
}

void Gui::run()
{
    if (slumber_abi_fingerprint() != slumber_abi::fingerprint()) {
        LOG_ERROR("ABI mismatch: Rust 0x%08X, C++ 0x%08X -- stale libslumber_gui.a\n",
                  static_cast<unsigned>(slumber_abi_fingerprint()),
                  static_cast<unsigned>(slumber_abi::fingerprint()));
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
                auto *st = static_cast<CustomMessage::Status *>(msg);
                mState.service_up_min = st->serviceUpMin;
                mState.rows_today     = st->rowsToday;
                mState.write_fails    = st->writeFails;
                mState.mark_ago_min   = st->markAgoMin;
                mState.last_mark      = st->lastMark;
                mState.acc_ok         = st->accOk;
                mState.hr_ok          = st->hrOk;
                mState.have_status    = 1;
                if (mMarkPending) {
                    mMarkPending       = false;
                    mState.just_logged = st->lastMark == mState.selected ? 1 : 0;
                }
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
                if (btn->event == Event::CLICK) {
                    if (btn->id == Id::SW1) {
                        move(-1);
                    } else if (btn->id == Id::SW3) {
                        move(1);
                    } else if (btn->id == Id::SW2) {
                        sendMark();
                    }
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
