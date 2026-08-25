/**
 ******************************************************************************
 * @file    Gui.cpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The screen the probe is trying to open, and nothing more.
 ******************************************************************************
 */

#include "Gui.hpp"

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#define LOG_MODULE_PRX   "ProbeGui"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{

constexpr char kLogPath[] = "probe-gui.txt";

/// ABGR2222: one byte per pixel, two bits per channel, alpha in the top two.
/// From the MSB: A[7:6] B[5:4] G[3:2] R[1:0].
///
/// Note this is *not* the packing the glance controls use -- GlanceColor_t is
/// six bits of RGB with no alpha, so 0x30 is red there and something else here.
/// Two formats, one watch; the display wants this one, and alpha must be 0b11
/// or the pixel is transparent rather than coloured.
constexpr uint8_t rgb2222(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint8_t>((0x3u << 6) | ((b & 0x3u) << 4) | ((g & 0x3u) << 2) | (r & 0x3u));
}

constexpr uint8_t kBlack = rgb2222(0, 0, 0);
constexpr uint8_t kWhite = rgb2222(3, 3, 3);
constexpr uint8_t kRed   = rgb2222(3, 0, 0);
constexpr uint8_t kGreen = rgb2222(0, 3, 0);
constexpr uint8_t kBlue  = rgb2222(0, 0, 3);

constexpr uint32_t kWaitForever = 0xFFFFFFFF;

} // namespace

Gui::Gui(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mLog(kernel, kLogPath)
    , mFrameBuf {}
{
}

void Gui::queryDisplayConfig()
{
    auto *cfg = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayConfig>();
    if (cfg) {
        if (mKernel.comm.sendMessage(cfg, 1000)
            && cfg->getResult() == SDK::MessageResult::SUCCESS) {
            mWidth      = cfg->width;
            mHeight     = cfg->height;
            mColorDepth = cfg->colorDepth;
        }
        mKernel.comm.releaseMessage(cfg);
    }

    // Never trust the config blindly: a wrong size here writes past mFrameBuf.
    if (mWidth <= 0 || mHeight <= 0
        || static_cast<uint32_t>(mWidth) * static_cast<uint32_t>(mHeight) > kMaxPixels) {
        mLog.line("display config unusable (%dx%d); assuming 240x240",
                  static_cast<int>(mWidth), static_cast<int>(mHeight));
        mWidth  = 240;
        mHeight = 240;
    }

    // colorDepth is reported as 6 -- the count of displayed colour bits, three
    // channels of two -- while storage is a full byte per pixel. Logged rather
    // than acted on, because a future firmware reporting something else here is
    // worth seeing.
    mLog.line("display %dx%d, colorDepth=%u",
              static_cast<int>(mWidth), static_cast<int>(mHeight),
              static_cast<unsigned>(mColorDepth));
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
    fill(0, 0, mWidth, mHeight, kBlack);

    // Three bands across the middle. The panel is round, so their ends are cut
    // off by the bezel -- which does not matter, because what is being asked is
    // "is this screen up", not "is this screen pretty".
    const int16_t bandHeight = static_cast<int16_t>(mHeight / 6);
    const int16_t top        = static_cast<int16_t>((mHeight - bandHeight * 3) / 2);

    fill(0, top, mWidth, bandHeight, kRed);
    fill(0, static_cast<int16_t>(top + bandHeight), mWidth, bandHeight, kGreen);
    fill(0, static_cast<int16_t>(top + bandHeight * 2), mWidth, bandHeight, kBlue);

    // A frame around the inscribed square -- the largest box the round display
    // shows whole. It doubles as a check that the whole framebuffer is being
    // presented rather than some corner of it.
    const int16_t side   = static_cast<int16_t>((mWidth < mHeight ? mWidth : mHeight) * 707 / 1000);
    const int16_t left   = static_cast<int16_t>((mWidth - side) / 2);
    const int16_t upper  = static_cast<int16_t>((mHeight - side) / 2);
    const int16_t stroke = 3;

    fill(left, upper, side, stroke, kWhite);
    fill(left, static_cast<int16_t>(upper + side - stroke), side, stroke, kWhite);
    fill(left, upper, stroke, side, kWhite);
    fill(static_cast<int16_t>(left + side - stroke), upper, stroke, side, kWhite);

    mPainted = true;
}

void Gui::push()
{
    if (!mResumed) {
        return;
    }

    if (!mPainted) {
        render();
    }

    auto *upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (upd) {
        // The buffer must stay valid until the response, which it does: it is a
        // member of an object living in static storage.
        upd->pBuffer = mFrameBuf;
        mKernel.comm.sendMessage(upd, 1000);
        mKernel.comm.releaseMessage(upd);
    }
}

void Gui::run()
{
    LOG_INFO("started\n");
    mLog.line("--- gui started ---");
    mLog.line("the kernel launched a GUI for this app");

    queryDisplayConfig();

    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, kWaitForever)) {
            continue;
        }

        switch (msg->getType()) {

            case SDK::MessageType::COMMAND_APP_STOP:
                mLog.line("--- gui stopping (app-stop) ---");
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                mKernel.sys.exit(0); // does not return on a watch
                return;

            case SDK::MessageType::COMMAND_APP_GUI_RESUME:
                mResumed = true;
                mLog.line("gui resumed");
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            case SDK::MessageType::COMMAND_APP_GUI_SUSPEND:
                mResumed = false;
                mLog.line("gui suspended");
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            case SDK::MessageType::EVENT_GUI_TICK: {
                const auto *tick = static_cast<SDK::Message::EventGuiTick *>(msg);
                if (!mHaveStart) {
                    mStartMs   = tick->timestamp;
                    mHaveStart = true;
                    mLog.line("first gui tick at t=%u ms", static_cast<unsigned>(mStartMs));
                }
                const uint32_t elapsed = tick->timestamp - mStartMs;

                // Ack and release before painting, as the TouchGFX port does.
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);

                if (elapsed >= kMaxVisibleMs) {
                    mLog.line("deadline reached after %u ms; leaving",
                              static_cast<unsigned>(elapsed));
                    mKernel.sys.exit(0);
                    return;
                }

                push();
                continue; // already released
            }

            case SDK::MessageType::EVENT_BUTTON: {
                auto *btn = static_cast<SDK::Message::EventButton *>(msg);

                // Every button is logged, not just the one that exits. Whether
                // a GUI launched this way receives input at all is a second
                // question this app can answer for free, and the id/event
                // numbering is worth having written down from the device rather
                // than from the header.
                mLog.line("button id=%u event=%u",
                          static_cast<unsigned>(btn->id),
                          static_cast<unsigned>(btn->event));

                if (btn->event == SDK::Message::EventButton::Event::CLICK
                    && btn->id == SDK::Message::EventButton::Id::SW4) {
                    mLog.line("back pressed; leaving");
                    msg->setResult(SDK::MessageResult::SUCCESS);
                    mKernel.comm.releaseMessage(msg);
                    mKernel.sys.exit(0);
                    // Reached only on the simulator, where exit() returns.
                    return;
                }

                msg->setResult(SDK::MessageResult::SUCCESS);
            } break;

            default:
                mLog.line("gui msg 0x%08X", static_cast<unsigned>(msg->getType()));
                msg->setResult(SDK::MessageResult::FAIL);
                break;
        }

        if (msg->getResult() == SDK::MessageResult::PENDING) {
            msg->setResult(SDK::MessageResult::FAIL);
        }
        mKernel.comm.releaseMessage(msg);
    }
}
