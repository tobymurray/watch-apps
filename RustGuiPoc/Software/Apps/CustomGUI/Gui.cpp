#include "Gui.hpp"

#include "Commands.hpp"

#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Timer/Timer.hpp"

#include "poc_gui.h"

#define LOG_MODULE_PRX   "RustGui"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

static constexpr uint32_t kWaitForever = 0xFFFFFFFF;
// Paths are relative to the app's own directory, which already exists.
static constexpr char     kFbDumpPath[] = "fb_dump.bin";

extern "C" void poc_gui_host_panic(const uint8_t *msg, uint32_t len)
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
        LOG_WARNING("Display config unusable (%dx%d); falling back to %dx%d\n",
                    mWidth, mHeight, kFallbackWidth, kFallbackHeight);
        mWidth  = kFallbackWidth;
        mHeight = kFallbackHeight;
    }
    LOG_INFO("Display %dx%d @ %ubpp\n", mWidth, mHeight, mColorDepth);
}

void Gui::renderAndPush()
{
    if (!mResumed) {
        return;
    }

    // One clock read. A displayed age and a freshness verdict sampled at
    // different instants can contradict each other on screen, which discredits
    // the diagnostics exactly when they are being read.
    const uint32_t now = mKernel.sys.getTimeMs();
    const uint32_t age =
        mHaveSample ? SDK::Timer::elapsed(now, mLastSampleMs) : 0;

    mState.uptime_ms     = now;
    mState.sample_age_ms = age;
    mState.valid         = (mHaveSample && age <= kStaleAfterMs) ? 1u : 0u;

    poc_gui_render(mFrameBuf,
                   kMaxPixels * kBytesPerPixel,
                   static_cast<uint16_t>(mWidth),
                   static_cast<uint16_t>(mHeight),
                   mScreen,
                   &mState);

    auto *upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (upd) {
        upd->pBuffer = mFrameBuf;
        mKernel.comm.sendMessage(upd, kResponseTimeoutMs);
        mKernel.comm.releaseMessage(upd);
    }
    ++mState.frames;
}

void Gui::dumpFramebuffer()
{
    auto file = mKernel.fs.file(kFbDumpPath);
    if (!file || !file->open(/*wMode=*/true, /*override=*/true)) {
        LOG_WARNING("fb dump: open '%s' failed\n", kFbDumpPath);
        return;
    }

    const size_t bytes = static_cast<size_t>(mWidth) *
                         static_cast<size_t>(mHeight) * kBytesPerPixel;
    size_t written = 0;
    const bool ok = file->write(reinterpret_cast<const char *>(mFrameBuf), bytes, written);
    file->flush();
    file->close();

    LOG_INFO("fb dump: %s screen=%u %dx%d -> %u/%u bytes %s\n",
             kFbDumpPath, static_cast<unsigned>(mScreen), mWidth, mHeight,
             static_cast<unsigned>(written), static_cast<unsigned>(bytes),
             (ok && written == bytes) ? "OK" : "FAIL");
}

void Gui::run()
{
    LOG_INFO("Started\n");

    if (poc_gui_abi_fingerprint() != poc_gui_abi::fingerprint()) {
        LOG_ERROR("ABI mismatch: Rust 0x%08X, C++ 0x%08X -- stale libpoc_gui.a\n",
                  static_cast<unsigned>(poc_gui_abi_fingerprint()),
                  static_cast<unsigned>(poc_gui_abi::fingerprint()));
        mKernel.sys.exit(1);
        return;
    }

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
                mKernel.sys.exit(0);
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
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue;

            case CustomMessage::ACCEL_VALUES: {
                auto *accel = static_cast<CustomMessage::AccelValues *>(msg);
                mState.accel_x_g = accel->x_g;
                mState.accel_y_g = accel->y_g;
                mState.accel_z_g = accel->z_g;
                mLastSampleMs    = mKernel.sys.getTimeMs();
                mHaveSample      = true;
                ++mState.samples;
                msg->setResult(SDK::MessageResult::SUCCESS);
            } break;

            case SDK::MessageType::EVENT_BUTTON: {
                auto *btn = static_cast<SDK::Message::EventButton *>(msg);
                using Id    = SDK::Message::EventButton::Id;
                using Event = SDK::Message::EventButton::Event;

                if (btn->event == Event::CLICK && btn->id == Id::SW4) {
                    LOG_INFO("Back pressed; exiting\n");
                    msg->setResult(SDK::MessageResult::SUCCESS);
                    mKernel.comm.releaseMessage(msg);
                    mKernel.sys.exit(0);
                    return;
                }

                if (btn->event == Event::CLICK && btn->id == Id::SW2 && screenCount > 0) {
                    mScreen = (mScreen + 1) % screenCount;
                    renderAndPush();
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
