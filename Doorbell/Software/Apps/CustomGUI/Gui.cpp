#include "Gui.hpp"

#include <cstdio>

#include "Commands.hpp"
#include "Paint.hpp"

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#define LOG_MODULE_PRX   "DoorbellGui"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{
constexpr uint32_t kWaitForever = 0xFFFFFFFF;

uint8_t backgroundFor(int last)
{
    switch (last) {
        case 1:  return Paint::GREEN;   // rang with a mailbox written
        case 2:  return Paint::BLUE;    // rang with nothing written
        case 3:  return Paint::AMBER;   // rang through the service
        case 4:  return Paint::RED;     // three in a row
        default: return Paint::BLACK;
    }
}
} // namespace

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

    const bool fits = mWidth > 0 && mHeight > 0 &&
                      static_cast<uint32_t>(mWidth) * static_cast<uint32_t>(mHeight) <= kMaxPixels;
    if (!fits) {
        LOG_WARNING("Display config unusable (%dx%d); falling back to %dx%d\n",
                    mWidth, mHeight, kFallbackWidth, kFallbackHeight);
        mWidth  = kFallbackWidth;
        mHeight = kFallbackHeight;
    }
    LOG_INFO("Display %dx%d @ %ubpp\n", mWidth, mHeight, mColorDepth);
}

void Gui::render()
{
    if (!mResumed) {
        return;
    }

    Paint::fill(mFrameBuf, mWidth, mHeight, backgroundFor(static_cast<int>(mLast)));

    // Total rings, three digits, roughly centred on a 240 wide round face.
    Paint::number(mFrameBuf, mWidth, mHeight, 66, 78, 6, mRings, 3, Paint::WHITE);

    // One block per click banked toward the gesture, so a half-finished
    // triple is visible rather than silent.
    for (uint8_t i = 0; i < kGestureClicks; i++) {
        const uint8_t c = (i < mPending) ? Paint::WHITE : Paint::GREY;
        Paint::rect(mFrameBuf, mWidth, mHeight,
                    static_cast<int16_t>(78 + i * 30), 150, 24, 10, c);
    }

    auto *upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (upd) {
        upd->pBuffer = mFrameBuf;
        mKernel.comm.sendMessage(upd, kResponseTimeoutMs);
        mKernel.comm.releaseMessage(upd);
    }
}

bool Gui::writeCommandFile()
{
    auto file = mKernel.fs.file(kCommandPath);
    if (!file) {
        LOG_ERROR("No file object for %s\n", kCommandPath);
        return false;
    }

    // Overwrite rather than append: the phone reads the newest command, and a
    // file that only grows would eventually stop fitting one FTS read.
    if (!file->open(true, true)) {
        LOG_ERROR("Could not open %s for write\n", kCommandPath);
        return false;
    }

    char    body[96];
    const int len = snprintf(body, sizeof(body),
                             "{\"action\":\"toggle\",\"seq\":%u,\"t\":%u}\n",
                             static_cast<unsigned>(mRings + 1),
                             static_cast<unsigned>(mKernel.sys.getTimeMs()));
    if (len <= 0) {
        file->close();
        return false;
    }

    size_t     written = 0;
    const bool ok      = file->write(body, static_cast<size_t>(len), written) &&
                         written == static_cast<size_t>(len);

    // Flushed before the doorbell goes out, or the phone can arrive first.
    const bool flushed = file->flush();
    file->close();

    LOG_INFO("wrote %s len=%d ok=%d flushed=%d\n", kCommandPath, len,
             static_cast<int>(ok), static_cast<int>(flushed));
    return ok && flushed;
}

void Gui::vibrate(uint8_t effect)
{
    auto vib = SDK::make_msg<SDK::Message::RequestVibroPlay>(mKernel);
    if (!vib) {
        return;
    }
    vib->notesCount        = 1;
    vib->notes[0].effect   = effect;
    vib->notes[0].pause    = 0;
    vib.send();
}

void Gui::ringFromGui(bool writeFile)
{
    if (writeFile) {
        writeCommandFile();
    }

    const uint32_t at   = mKernel.sys.getTimeMs();
    const bool     sent = SDK::send_msg<SDK::Message::CommandAppNewActivity>(mKernel);

    mRings++;
    mLast  = writeFile ? Last::WITH_FILE : Last::NO_FILE;
    mDirty = true;

    LOG_INFO("ring %u from gui file=%d t=%u sent=%d\n", static_cast<unsigned>(mRings),
             static_cast<int>(writeFile), static_cast<unsigned>(at),
             static_cast<int>(sent));

    vibrate(SDK::Message::RequestVibroPlay::Effect::STRONG_CLICK_100);
}

void Gui::ringViaService()
{
    auto msg = SDK::make_msg<CustomMessage::RingFromService>(mKernel);
    if (!msg) {
        LOG_ERROR("No message for the service ring\n");
        return;
    }
    msg->seq = mRings + 1;
    const bool sent = msg.send();

    mRings++;
    mLast  = Last::VIA_SERVICE;
    mDirty = true;

    LOG_INFO("ring %u handed to service t=%u sent=%d\n", static_cast<unsigned>(mRings),
             static_cast<unsigned>(mKernel.sys.getTimeMs()), static_cast<int>(sent));

    vibrate(SDK::Message::RequestVibroPlay::Effect::DOUBLE_CLICK_100);
}

void Gui::countGestureClick(uint32_t now)
{
    // getTimeMs wraps at 49 days; an unsigned difference stays correct across it.
    if (mPending == 0 || (now - mWindowStartMs) > kGestureWindowMs) {
        mPending       = 1;
        mWindowStartMs = now;
    } else {
        mPending++;
    }
    mDirty = true;

    if (mPending < kGestureClicks) {
        LOG_INFO("gesture click %u of %u\n", static_cast<unsigned>(mPending),
                 static_cast<unsigned>(kGestureClicks));
        return;
    }

    mPending = 0;
    LOG_INFO("gesture complete t=%u\n", static_cast<unsigned>(now));
    ringFromGui(true);
}

void Gui::onButton(const SDK::Message::EventButton &btn)
{
    using Id    = SDK::Message::EventButton::Id;
    using Event = SDK::Message::EventButton::Event;

    const uint32_t now = mKernel.sys.getTimeMs();

    if (btn.id == Id::SW2 && btn.event == Event::CLICK) {
        countGestureClick(now);
        return;
    }

    if (btn.id == Id::SW1 && btn.event == Event::CLICK) {
        ringFromGui(false);
        return;
    }

    if (btn.id == Id::SW1 && btn.event == Event::LONG_PRESS) {
        // Three with nothing in between, to see whether the kernel coalesces them.
        LOG_INFO("burst of 3 t=%u\n", static_cast<unsigned>(now));
        for (uint8_t i = 0; i < 3; i++) {
            const bool sent = SDK::send_msg<SDK::Message::CommandAppNewActivity>(mKernel);
            mRings++;
            LOG_INFO("burst ring %u sent=%d\n", static_cast<unsigned>(mRings),
                     static_cast<int>(sent));
        }
        mLast  = Last::BURST;
        mDirty = true;
        vibrate(SDK::Message::RequestVibroPlay::Effect::ALERT_750MS_100);
        return;
    }

    if (btn.id == Id::SW3 && btn.event == Event::CLICK) {
        ringViaService();
        return;
    }
}

void Gui::run()
{
    // A log line proves nothing unless it says which build wrote it.
    LOG_INFO("Started v%s id=%s\n", BUILD_VERSION, APP_ID);

    queryDisplayConfig();

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
                if (mDirty) {
                    mDirty = false;
                    render();
                }
                continue;

            case SDK::MessageType::EVENT_BUTTON: {
                auto *btn = static_cast<SDK::Message::EventButton *>(msg);
                using Id    = SDK::Message::EventButton::Id;
                using Event = SDK::Message::EventButton::Event;

                LOG_INFO("button id=%u event=%u t=%u\n",
                         static_cast<unsigned>(btn->id),
                         static_cast<unsigned>(btn->event),
                         static_cast<unsigned>(btn->timestamp));

                if (btn->event == Event::CLICK && btn->id == Id::SW4) {
                    LOG_INFO("Back pressed; exiting\n");
                    msg->setResult(SDK::MessageResult::SUCCESS);
                    mKernel.comm.releaseMessage(msg);
                    mKernel.sys.exit(0);
                    return;
                }

                onButton(*btn);
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
