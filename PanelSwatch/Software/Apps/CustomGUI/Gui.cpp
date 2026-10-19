#include "Gui.hpp"

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include "Crumb.hpp"
#include "Pages.hpp"

#define LOG_MODULE_PRX   "SwatchGui"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

static constexpr uint32_t kWaitForever = 0xFFFFFFFF;

Gui::Gui(SDK::Kernel &kernel)
    : mKernel(kernel)
{
}

void Gui::queryDisplayConfig()
{
    auto *cfg = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayConfig>();
    crumb(mKernel, "cfg alloc=%d", cfg ? 1 : 0);
    if (cfg) {
        const bool sent = mKernel.comm.sendMessage(cfg, kResponseTimeoutMs);
        crumb(mKernel, "cfg sent=%d result=%d w=%d h=%d depth=%d", sent ? 1 : 0,
              static_cast<int>(cfg->getResult()), static_cast<int>(cfg->width),
              static_cast<int>(cfg->height), static_cast<int>(cfg->colorDepth));
        if (sent && cfg->getResult() == SDK::MessageResult::SUCCESS) {
            // A byte a pixel at 240x240; the watch reports depth 6, its colour bits in that byte.
            mDisplayUsable = cfg->width == 240 && cfg->height == 240 && cfg->colorDepth <= 8;
        }
        mKernel.comm.releaseMessage(cfg);
    }
    crumb(mKernel, "display usable=%d", mDisplayUsable ? 1 : 0);
}

void Gui::renderAndPush()
{
    if (!mResumed || !mDisplayUsable || !mDirty) {
        return;
    }
    mDirty = false;
    crumb(mKernel, "decode page=%u data=%p size=%u", static_cast<unsigned>(mPage),
          static_cast<const void *>(Pages::kAll[mPage].data), static_cast<unsigned>(Pages::kAll[mPage].size));
    Pages::decode(mPage, mFrameBuf, kPixels);
    crumb(mKernel, "decoded first=0x%02X mid=0x%02X", mFrameBuf[0], mFrameBuf[kPixels / 2 + 120]);

    auto *upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (upd) {
        upd->pBuffer = mFrameBuf;
        const bool ok = mKernel.comm.sendMessage(upd, kResponseTimeoutMs);
        mKernel.comm.releaseMessage(upd);
        crumb(mKernel, "pushed ok=%d", ok ? 1 : 0);
    }
}

void Gui::move(int step)
{
    mPage  = (mPage + Pages::kCount + step) % Pages::kCount;
    mDirty = true;
}

void Gui::run()
{
    crumb(mKernel, "=== Swatch GUI start, %u pages", static_cast<unsigned>(Pages::kCount));
    queryDisplayConfig();

    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, kWaitForever)) {
            continue;
        }

        switch (msg->getType()) {
            case SDK::MessageType::COMMAND_APP_STOP:
                crumb(mKernel, "stop");
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                mKernel.sys.exit(0);
                return;

            case SDK::MessageType::COMMAND_APP_GUI_RESUME:
                crumb(mKernel, "resume");
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

            case SDK::MessageType::EVENT_BUTTON: {
                auto *btn = static_cast<SDK::Message::EventButton *>(msg);
                using Id    = SDK::Message::EventButton::Id;
                using Event = SDK::Message::EventButton::Event;

                crumb(mKernel, "button id=%d event=%d", static_cast<int>(btn->id), static_cast<int>(btn->event));
                if (btn->event == Event::CLICK && btn->id == Id::SW4) {
                    msg->setResult(SDK::MessageResult::SUCCESS);
                    mKernel.comm.releaseMessage(msg);
                    mKernel.sys.exit(0);
                    return;
                }
                if (btn->event == Event::CLICK) {
                    if (btn->id == Id::SW1) {
                        move(-1);
                    } else if (btn->id == Id::SW2 || btn->id == Id::SW3) {
                        move(1);
                    }
                }
                msg->setResult(SDK::MessageResult::SUCCESS);
            } break;

            default:
                crumb(mKernel, "unhandled type=%d", static_cast<int>(msg->getType()));
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
