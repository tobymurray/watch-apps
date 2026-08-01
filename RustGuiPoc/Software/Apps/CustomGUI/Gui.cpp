/**
 ******************************************************************************
 * @file    Gui.cpp
 * @brief   CustomGUI shim implementation. Mirrors the message-loop idiom of
 *          Libs/Source/Port/TouchGFX/TouchGFXCommandProcessor.cpp, minus
 *          TouchGFX: query display config, then on each GUI tick have the Rust
 *          core paint the framebuffer and push it via RequestDisplayUpdate.
 ******************************************************************************
 */
#include "Gui.hpp"

#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Interfaces/IFileSystem.hpp"

#include "poc_gui.h"

#define LOG_MODULE_PRX   "RustGui"
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
    if (cfg) {
        // Request-response: the kernel fills width/height/colorDepth/format.
        if (mKernel.comm.sendMessage(cfg, 1000) &&
            cfg->getResult() == SDK::MessageResult::SUCCESS) {
            mWidth      = cfg->width;
            mHeight     = cfg->height;
            mColorDepth = cfg->colorDepth;
        }
        mKernel.comm.releaseMessage(cfg);
    }

    // Fall back / clamp to what the static framebuffer can hold. Never trust the
    // config blindly — a wrong size here writes past mFrameBuf.
    if (mWidth <= 0 || mHeight <= 0 ||
        static_cast<uint32_t>(mWidth) * static_cast<uint32_t>(mHeight) > kMaxPixels) {
        LOG_WARNING("Display config unusable (%dx%d); falling back to 240x240\n",
                    mWidth, mHeight);
        mWidth  = 240;
        mHeight = 240;
    }
    LOG_INFO("Display %dx%d @ %ubpp\n", mWidth, mHeight, mColorDepth);
}

void Gui::renderAndPush()
{
    if (!mResumed) {
        return;
    }

    poc_gui_render(mFrameBuf,
                   static_cast<uint16_t>(mWidth),
                   static_cast<uint16_t>(mHeight),
                   mScreen,
                   mFrame);

    auto *upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (upd) {
        upd->pBuffer = mFrameBuf;   // buffer must stay valid until response (it is static)
        mKernel.comm.sendMessage(upd, 1000);
        mKernel.comm.releaseMessage(upd);
    }
    ++mFrame;
}

void Gui::dumpFramebuffer()
{
    // The framebuffer is exactly what poc_gui_render() produced, so this file is
    // byte-comparable to the sim's render() output for the same screen.
    static constexpr char kDir[]  = "Apps/RustGuiPoc";
    static constexpr char kPath[] = "Apps/RustGuiPoc/fb_dump.bin";

    mKernel.fs.mkdir(kDir);

    auto file = mKernel.fs.file(kPath);
    if (!file || !file->open(/*wMode=*/true, /*override=*/true)) {
        LOG_WARNING("fb dump: open '%s' failed\n", kPath);
        return;
    }

    const size_t bytes = static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight); // 8bpp: 1 byte/px
    size_t written = 0;
    const bool ok = file->write(reinterpret_cast<const char *>(mFrameBuf), bytes, written);
    file->flush();
    file->close();

    LOG_INFO("fb dump: %s screen=%u %dx%d -> %u/%u bytes %s\n",
             kPath, static_cast<unsigned>(mScreen), mWidth, mHeight,
             static_cast<unsigned>(written), static_cast<unsigned>(bytes),
             (ok && written == bytes) ? "OK" : "FAIL");
}

void Gui::run()
{
    LOG_INFO("Started\n");

    queryDisplayConfig();
    const uint32_t screenCount = poc_gui_screen_count();

    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, kWaitForever)) {
            continue;
        }

        switch (msg->getType()) {

            case SDK::MessageType::COMMAND_APP_STOP:
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                mKernel.sys.exit(0); // no return
                return;

            case SDK::MessageType::COMMAND_APP_GUI_RESUME:
                mResumed = true;
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            case SDK::MessageType::COMMAND_APP_GUI_SUSPEND:
                mResumed = false;
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            case SDK::MessageType::EVENT_GUI_TICK:
                // Ack + release before painting (mirrors the TouchGFX port).
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue; // already released

            case SDK::MessageType::EVENT_BUTTON: {
                auto *btn = static_cast<SDK::Message::EventButton *>(msg);
                using Id    = SDK::Message::EventButton::Id;
                using Event = SDK::Message::EventButton::Event;

                // SW4 (bottom-right) is BACK. The screens here are a cycle, not
                // a stack, so there is nothing to go back *to* — back leaves the
                // app, which is what the SDK's own apps do with R2. This loop
                // owns the message queue and swallows every button, so without
                // it the only way out of the app is rebooting the watch.
                //
                // Acked and released first, exactly as COMMAND_APP_STOP does:
                // sys.exit() does not return on the device.
                if (btn->event == Event::CLICK && btn->id == Id::SW4) {
                    LOG_INFO("Back pressed; exiting\n");
                    msg->setResult(SDK::MessageResult::SUCCESS);
                    mKernel.comm.releaseMessage(msg);
                    mKernel.sys.exit(0);
                    // Reached only on the simulator, where exit() sets a flag
                    // and returns rather than tearing the app down.
                    return;
                }

                // SW2 (top-right / SELECT) cycles screens — the "move between
                // two UIs" demo. SW3 (bottom-left / DOWN) long-press dumps the
                // framebuffer for the desktop sim. Everything else is ignored.
                if (btn->event == Event::CLICK && btn->id == Id::SW2 && screenCount > 0) {
                    mScreen = (mScreen + 1) % screenCount;
                    renderAndPush(); // repaint immediately so the switch feels instant
                } else if (btn->event == Event::LONG_PRESS && btn->id == Id::SW3) {
                    dumpFramebuffer();
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
