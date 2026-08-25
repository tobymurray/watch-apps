/**
 ******************************************************************************
 * @file    Gui.cpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The labelled half of the experiment.
 ******************************************************************************
 */

#include "Gui.hpp"

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#define LOG_MODULE_PRX   "BtnGui"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{

constexpr char kLogPath[] = "button-gui.txt";

/// ABGR2222: A[7:6] B[5:4] G[3:2] R[1:0], alpha opaque. Not the packing
/// GlanceColor_t uses -- see RunGuiProbe/Software/Apps/CustomGUI/Gui.cpp.
constexpr uint8_t rgb2222(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint8_t>((0x3u << 6) | ((b & 0x3u) << 4) | ((g & 0x3u) << 2) | (r & 0x3u));
}

constexpr uint8_t kBlack  = rgb2222(0, 0, 0);
constexpr uint8_t kAmber  = rgb2222(3, 2, 0);
constexpr uint8_t kGrey   = rgb2222(1, 1, 1);

/// The marker cycles through these on every pin transition.
constexpr uint8_t kCycle[] = {
    rgb2222(3, 3, 3),
    rgb2222(3, 0, 0),
    rgb2222(0, 3, 0),
    rgb2222(0, 0, 3),
};
constexpr uint32_t kCycleCount = sizeof kCycle / sizeof kCycle[0];

} // namespace

Gui::Gui(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mLog(kernel, kLogPath)
    , mSampler(kernel, mLog)
    , mFrameBuf {}
{
}

void Gui::queryDisplayConfig()
{
    auto *cfg = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayConfig>();
    if (cfg) {
        if (mKernel.comm.sendMessage(cfg, 1000)
            && cfg->getResult() == SDK::MessageResult::SUCCESS) {
            mWidth  = cfg->width;
            mHeight = cfg->height;
        }
        mKernel.comm.releaseMessage(cfg);
    }

    if (mWidth <= 0 || mHeight <= 0
        || static_cast<uint32_t>(mWidth) * static_cast<uint32_t>(mHeight) > kMaxPixels) {
        mWidth  = 240;
        mHeight = 240;
    }
}

void Gui::fill(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t colour)
{
    if (w <= 0 || h <= 0) {
        return;
    }

    const int16_t x0 = (x < 0) ? 0 : x;
    const int16_t y0 = (y < 0) ? 0 : y;
    const int16_t x1 = (x + w > mWidth) ? mWidth : static_cast<int16_t>(x + w);
    const int16_t y1 = (y + h > mHeight) ? mHeight : static_cast<int16_t>(y + h);

    for (int16_t row = y0; row < y1; ++row) {
        uint8_t *line = &mFrameBuf[static_cast<uint32_t>(row) * static_cast<uint32_t>(mWidth)];
        for (int16_t col = x0; col < x1; ++col) {
            line[col] = colour;
        }
    }
}

void Gui::render()
{
    if (mSampler.calibrating()) {
        // Amber, edge to edge, meaning "hands off". Nothing subtle, because the
        // one way to ruin this run is to press something during the window that
        // decides which pins are worth watching.
        fill(0, 0, mWidth, mHeight, kAmber);
        mDirty = false;
        return;
    }

    fill(0, 0, mWidth, mHeight, kBlack);

    // A frame, so a black screen and a crashed app look different.
    const int16_t side  = static_cast<int16_t>((mWidth < mHeight ? mWidth : mHeight) * 707 / 1000);
    const int16_t left  = static_cast<int16_t>((mWidth - side) / 2);
    const int16_t upper = static_cast<int16_t>((mHeight - side) / 2);
    fill(left, upper, side, 3, kGrey);
    fill(left, static_cast<int16_t>(upper + side - 3), side, 3, kGrey);
    fill(left, upper, 3, side, kGrey);
    fill(static_cast<int16_t>(left + side - 3), upper, 3, side, kGrey);

    // The marker. Its colour advances on every transition, so a press is
    // visible the moment the sampler sees it.
    const int16_t marker = static_cast<int16_t>(side / 2);
    fill(static_cast<int16_t>((mWidth - marker) / 2),
         static_cast<int16_t>((mHeight - marker) / 2),
         marker, marker, kCycle[mMarker % kCycleCount]);

    mDirty = false;
}

void Gui::push()
{
    if (!mResumed || !mDirty) {
        return;
    }

    render();

    auto *upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (upd) {
        upd->pBuffer = mFrameBuf;
        mKernel.comm.sendMessage(upd, 1000);
        mKernel.comm.releaseMessage(upd);
    }
}

void Gui::run()
{
    LOG_INFO("started\n");
    mLog.line("--- button probe gui ---");
    if (!Probe::Sampler::available()) {
        mLog.line("this build cannot read registers; nothing to measure");
    }

    queryDisplayConfig();

    while (true) {
        const uint32_t now = mKernel.sys.getTimeMs();
        if (!mHaveStart) {
            mStartMs   = now;
            mHaveStart = true;
        }

        if ((now - mStartMs) >= kMaxVisibleMs) {
            mLog.line("deadline reached; leaving");
            mKernel.sys.exit(0);
            return;
        }
        if (mLeaving && static_cast<int32_t>(now - mLeaveAtMs) >= 0) {
            mLog.line("--- leaving ---");
            mKernel.sys.exit(0);
            return;
        }

        const bool wasCalibrating = mSampler.calibrating();
        if (mSampler.poll() > 0) {
            ++mMarker;
            mDirty = true;
        }
        if (wasCalibrating && !mSampler.calibrating()) {
            mDirty = true;
        }

        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, Probe::Sampler::kPollMs)) {
            // A timeout, which is the normal case and the reason this loop is
            // written the way it is: it is what makes the sampling rate a
            // property of this app rather than of the kernel's tick.
            push();
            continue;
        }

        switch (msg->getType()) {

            case SDK::MessageType::COMMAND_APP_STOP:
                mLog.line("--- app-stop ---");
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

            case SDK::MessageType::EVENT_BUTTON: {
                auto *btn = static_cast<SDK::Message::EventButton *>(msg);

                // The label. Written with the same clock as the pin lines above
                // it, which is what makes the file correlatable.
                mLog.line("t=%lu BUTTON id=%u event=%u",
                          static_cast<unsigned long>(mKernel.sys.getTimeMs()),
                          static_cast<unsigned>(btn->id),
                          static_cast<unsigned>(btn->event));

                if (btn->id == SDK::Message::EventButton::Id::SW4
                    && btn->event == SDK::Message::EventButton::Event::CLICK
                    && !mLeaving) {
                    // Not `exit()` here: the release edge of this very press is
                    // still to come, and it belongs in the log with the others.
                    mLeaving   = true;
                    mLeaveAtMs = mKernel.sys.getTimeMs() + kLeaveDelayMs;
                    mLog.line("R2 clicked; leaving in %u ms",
                              static_cast<unsigned>(kLeaveDelayMs));
                }

                msg->setResult(SDK::MessageResult::SUCCESS);
            } break;

            case SDK::MessageType::EVENT_GUI_TICK:
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            default:
                msg->setResult(SDK::MessageResult::FAIL);
                break;
        }

        if (msg->getResult() == SDK::MessageResult::PENDING) {
            msg->setResult(SDK::MessageResult::FAIL);
        }
        mKernel.comm.releaseMessage(msg);
        push();
    }
}
