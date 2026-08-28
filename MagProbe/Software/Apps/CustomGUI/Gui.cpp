#include "Gui.hpp"

#include "Commands.hpp"

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Timer/Timer.hpp"

#define LOG_MODULE_PRX   "MagGui"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace {

constexpr uint32_t kWaitForever = 0xFFFFFFFF;

/// Where a framebuffer dump goes. Relative to the app's own directory, which
/// already exists: paths on this device are sandbox-relative and an absolute
/// one does not resolve.
constexpr char kFbDumpPath[] = "fb_dump.bin";

Mag::Vec3 vec(float x, float y, float z)
{
    return Mag::Vec3{x, y, z};
}

} // namespace

Gui::Gui(SDK::Kernel& kernel)
    : mKernel(kernel)
{
}

void Gui::queryDisplayConfig()
{
    auto* cfg = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayConfig>();
    if (cfg != nullptr) {
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

void Gui::renderAndPush()
{
    if (!mResumed) {
        return;
    }

    const uint32_t now = mKernel.sys.getTimeMs();

    // A stale Service is its own finding, and it is not the same as a sensor
    // with no producer. Drawing the last status forever would make a dead
    // Service look like a working one reporting nothing.
    if (mHaveStatus && SDK::Timer::elapsed(now, mLastStatusMs) > kServiceStaleMs) {
        mView.delivery   = Mag::Delivery::Stalled;
        mView.haveHeading = false;
    }

    mView.uptimeMs = now;
    mView.frames   = mFrames;

    Canvas canvas(mFrameBuf,
                  static_cast<uint16_t>(mWidth),
                  static_cast<uint16_t>(mHeight),
                  kMaxPixels);
    Render::render(canvas, mScreen, mView);

    auto* upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (upd != nullptr) {
        upd->pBuffer = mFrameBuf;
        mKernel.comm.sendMessage(upd, kResponseTimeoutMs);
        mKernel.comm.releaseMessage(upd);
    }
    ++mFrames;
}

void Gui::sendControl(uint8_t action)
{
    auto msg = SDK::make_msg<CustomMessage::MagControl>(mKernel);
    if (!msg) {
        LOG_WARNING("MagControl allocation failed\n");
        return;
    }
    msg->action = action;
    msg.send(0);
}

void Gui::handleButton(const SDK::MessageBase* msg)
{
    const auto* btn = static_cast<const SDK::Message::EventButton*>(msg);
    using Id    = SDK::Message::EventButton::Id;
    using Event = SDK::Message::EventButton::Event;

    if (btn->event != Event::CLICK && btn->event != Event::LONG_PRESS) {
        return;
    }

    if (btn->event == Event::CLICK) {
        switch (btn->id) {
            case Id::SW1:  // L1, top left: previous screen
                mScreen = static_cast<Render::Screen>(
                    (static_cast<uint8_t>(mScreen) + Render::kScreenCount - 1) %
                    Render::kScreenCount);
                renderAndPush();
                return;

            case Id::SW3:  // L2, bottom left: next screen
                mScreen = static_cast<Render::Screen>(
                    (static_cast<uint8_t>(mScreen) + 1) % Render::kScreenCount);
                renderAndPush();
                return;

            case Id::SW2:  // R1, top right: start or stop the sweep
                // One button both ways, because a separate stop is a way to
                // leave a sweep running by accident.
                sendControl(static_cast<uint8_t>(
                    mView.calibrating ? CustomMessage::Action::CalibrationStop
                                      : CustomMessage::Action::CalibrationStart));
                return;

            default:
                return;
        }
    }

    // Long presses, kept off the click path so a fumbled press does not reset a
    // sweep or flip a convention.
    switch (btn->id) {
        case Id::SW2:
            sendControl(static_cast<uint8_t>(CustomMessage::Action::CalibrationReset));
            break;

        case Id::SW1:
            // Settles the one thing about this watch nobody has written down:
            // which way the accelerometer vector points at rest.
            sendControl(static_cast<uint8_t>(CustomMessage::Action::ToggleUpConvention));
            break;

        case Id::SW3: {
            // The framebuffer, byte for byte, so a layout can be compared
            // against what the host renderer produces for the same state.
            auto file = mKernel.fs.file(kFbDumpPath);
            if (!file || !file->open(true, true)) {
                LOG_WARNING("fb dump: open '%s' failed\n", kFbDumpPath);
                break;
            }
            const size_t bytes =
                static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight);
            size_t     written = 0;
            const bool ok = file->write(reinterpret_cast<const char*>(mFrameBuf), bytes, written);
            file->flush();
            file->close();
            LOG_INFO("fb dump: %u/%u bytes %s\n",
                     static_cast<unsigned>(written), static_cast<unsigned>(bytes),
                     (ok && written == bytes) ? "OK" : "FAIL");
        } break;

        default:
            break;
    }
}

void Gui::run()
{
    LOG_INFO("Started\n");

    queryDisplayConfig();

    while (true) {
        SDK::MessageBase* msg = nullptr;
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

            case CustomMessage::MAG_STATUS: {
                const auto* s = static_cast<const CustomMessage::MagStatus*>(msg);

                mView.resolve      = static_cast<Mag::Resolve>(s->resolve);
                mView.delivery     = static_cast<Mag::Delivery>(s->delivery);
                mView.shape        = static_cast<Mag::FrameShape>(s->shape);
                mView.units        = static_cast<Mag::Units>(s->units);
                mView.accelResolve = static_cast<Mag::Resolve>(s->accelResolve);
                mView.calQuality   = static_cast<Mag::HardIron::Quality>(s->calQuality);

                mView.fieldCount     = s->fieldCount;
                mView.stride         = s->stride;
                mView.samples        = s->samples;
                mView.batches        = s->batches;
                mView.ageMs          = s->ageMs;
                mView.raw            = vec(s->rawX, s->rawY, s->rawZ);
                mView.magnitude      = s->magnitude;
                mView.spreadFraction = s->spread;
                mView.rawBits0       = s->rawBits0;
                mView.rawInt0        = s->rawInt0;
                mView.headingDeg     = s->headingDeg;
                mView.dipDeg         = s->dipDeg;

                const uint8_t flags = s->flags;
                mView.haveHeading = (flags & CustomMessage::StatusFlag::kHeadingValid) != 0;
                mView.levelled    = (flags & CustomMessage::StatusFlag::kLevelled) != 0;
                mView.accelFresh  = (flags & CustomMessage::StatusFlag::kAccelFresh) != 0;
                mView.calibrating = (flags & CustomMessage::StatusFlag::kCalibrating) != 0;

                mLastStatusMs = mKernel.sys.getTimeMs();
                mHaveStatus   = true;

                msg->setResult(SDK::MessageResult::SUCCESS);
            } break;

            case CustomMessage::MAG_CALIBRATION: {
                const auto* c = static_cast<const CustomMessage::MagCalibration*>(msg);
                mView.calQuality = static_cast<Mag::HardIron::Quality>(c->quality);
                mView.calSamples = c->samples;
                mView.calOffsets = vec(c->offsetX, c->offsetY, c->offsetZ);
                mView.calSpans   = vec(c->spanX, c->spanY, c->spanZ);
                msg->setResult(SDK::MessageResult::SUCCESS);
            } break;

            case SDK::MessageType::EVENT_BUTTON:
                handleButton(msg);
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

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
