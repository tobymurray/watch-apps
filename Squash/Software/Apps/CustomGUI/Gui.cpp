#include "Gui.hpp"

#include <cstring>

#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Kernel/KernelProviderGUI.hpp"

#include "squash_gui.h"

#define LOG_MODULE_PRX   "SquashGui"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{
constexpr uint32_t kWaitForever = 0xFFFFFFFF;
} // namespace

/// Called by PanicKit's panic handler with "file:line: message". Without it a
/// Rust panic would hang the GUI thread silently, and the only way out would be
/// a reboot.
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
    , mSender(kernel)
{
}

void Gui::run()
{
    LOG_INFO("Started\n");

    // A stale libsquash_gui.a is otherwise silent until it draws garbage.
    if (squash_gui_abi_fingerprint() != squash_gui_abi::fingerprint()) {
        LOG_ERROR("ABI mismatch: Rust 0x%08X, C++ 0x%08X -- stale libsquash_gui.a\n",
                  static_cast<unsigned>(squash_gui_abi_fingerprint()),
                  static_cast<unsigned>(squash_gui_abi::fingerprint()));
        mKernel.sys.exit(1);
        return;
    }

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
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue;

            case SDK::MessageType::COMMAND_APP_GUI_SUSPEND:
                mResumed = false;
                // A picker walked away from is not a label the wearer chose,
                // and the one the Service is writing is unaffected.
                mPicking = false;
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            // Nothing animates, and the Service publishes a snapshot a second,
            // so a redraw per tick would be the same pixels at the tick rate.
            case SDK::MessageType::EVENT_GUI_TICK:
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            case CustomMessage::STATUS_UPDATE:
                mStatus = static_cast<CustomMessage::Status *>(msg)->status;
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue;

            case CustomMessage::STATE_UPDATE: {
                auto *upd = static_cast<CustomMessage::StateUpd *>(msg);
                const Session::State state = upd->state;
                // Here rather than on the button press, so a start the Service
                // refused leaves the previous result up instead of blanking it.
                if (state == Session::State::ACTIVE &&
                    mState == Session::State::INACTIVE) {
                    mShowEnded = false;
                    mDiscarded = false;
                } else if (state == Session::State::INACTIVE &&
                           mState != Session::State::INACTIVE) {
                    mShowEnded = true;
                    mSavedOk   = upd->savedOk != 0u;
                }
                mState = state;
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue;
            }

            case SDK::MessageType::EVENT_BUTTON: {
                auto *evt = static_cast<SDK::Message::EventButton *>(msg);
                handleButton(evt->id, evt->event);
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue;
            }

            default:
                break;
        }

        mKernel.comm.releaseMessage(msg);
    }
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

void Gui::buildFrame(squash_gui_frame &out) const
{
    std::memset(&out, 0, sizeof(out));

    // Straight through: the Service owns every one of these and the renderer is
    // a pure function of them.
    out.elapsed_s     = mStatus.elapsedS;
    out.rec_s         = mStatus.recS;
    out.rec_cap_s     = mStatus.recCapS;
    out.rec_kb        = mStatus.recKb;
    out.rec_cap_kb    = mStatus.recCapKb;
    out.gyro_mag      = mStatus.gyroMag;
    out.accel_var_k   = mStatus.accelVarK;
    out.rally_s       = mStatus.rallyS;
    out.rest_s        = mStatus.restS;
    out.off_court_s   = mStatus.offCourtS;
    out.markers       = mStatus.markers;
    out.hr_bpm        = mStatus.hrBpm;
    out.label_s       = mStatus.labelS;
    out.sat_accel_pct = mStatus.satAccelPct;
    out.sat_gyro_pct  = mStatus.satGyroPct;
    out.hr_trust      = mStatus.hrTrust;
    out.hr_source     = mStatus.hrSource;
    out.recording     = mStatus.recording;
    out.rec_stop      = mStatus.recStop;
    out.armed         = mStatus.armed;
    out.label         = mStatus.label;

    if (mPicking) {
        out.screen     = SQUASH_GUI_SCREEN_LABEL;
        out.label_pick = mLabelPick;
        return;
    }

    switch (mState) {
        case Session::State::ACTIVE:
            out.screen = SQUASH_GUI_SCREEN_PROFILE;
            return;
        case Session::State::PAUSED:
            out.screen = SQUASH_GUI_SCREEN_PAUSED;
            return;
        case Session::State::INACTIVE:
        default:
            break;
    }

    if (mShowEnded) {
        out.screen   = mDiscarded ? SQUASH_GUI_SCREEN_DISCARDED : SQUASH_GUI_SCREEN_SAVED;
        out.saved_ok = mSavedOk ? 1u : 0u;
        return;
    }

    out.screen = SQUASH_GUI_SCREEN_READY;
}

void Gui::renderAndPush()
{
    if (!mResumed) {
        return;
    }

    squash_gui_frame frame;
    buildFrame(frame);

    squash_gui_render(mFrameBuf, kMaxPixels * kBytesPerPixel,
                      static_cast<uint16_t>(mWidth), static_cast<uint16_t>(mHeight),
                      &frame);

    auto *upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (upd) {
        upd->pBuffer = mFrameBuf;
        mKernel.comm.sendMessage(upd, kResponseTimeoutMs);
        mKernel.comm.releaseMessage(upd);
    }
}

void Gui::handleButton(SDK::Message::EventButton::Id id,
                       SDK::Message::EventButton::Event event)
{
    using Id    = SDK::Message::EventButton::Id;
    using Event = SDK::Message::EventButton::Event;

    // CLICK only. Service::setCapabilities() leaves enMusicControl off so the
    // system does not claim the long press, but nothing here depends on a HOLD
    // arriving: four buttons and one short press each is enough for every
    // screen, and a gesture no screen needs is a gesture that cannot be wrong.
    if (event != Event::CLICK) {
        return;
    }

    if (mPicking) {
        switch (id) {
            case Id::SW3: // L2
                mLabelPick = squash_gui_label_next(mLabelPick);
                break;
            case Id::SW2: // R1
                mSender.setLabel(mLabelPick);
                mPicking = false;
                break;
            case Id::SW4: // R2
                mPicking = false;
                break;
            default:
                break;
        }
        return;
    }

    switch (mState) {
        case Session::State::ACTIVE:
            switch (id) {
                case Id::SW1: // L1 -- the hot pair, one press
                    mSender.setLabel(squash_gui_label_toggle(mStatus.label));
                    break;
                case Id::SW3: // L2 -- the other four
                    // Opens on whatever is being written, so the common case is
                    // confirming rather than hunting.
                    mLabelPick = (mStatus.label == SQUASH_GUI_LABEL_NONE)
                                     ? SQUASH_GUI_LABEL_RALLY
                                     : mStatus.label;
                    mPicking = true;
                    break;
                case Id::SW2: // R1
                    mSender.sessionPause();
                    break;
                case Id::SW4: // R2
                    mSender.mark();
                    break;
                default:
                    break;
            }
            return;

        case Session::State::PAUSED:
            switch (id) {
                case Id::SW1: // L1
                    mSender.sessionStop(false);
                    break;
                case Id::SW3: // L2
                    // Remembered before the Service answers, because the reply
                    // carries the state and not which of the two endings it was.
                    mDiscarded = true;
                    mSender.sessionStop(true);
                    break;
                case Id::SW2: // R1
                    mSender.sessionResume();
                    break;
                default:
                    break;
            }
            return;

        case Session::State::INACTIVE:
        default:
            if (mShowEnded) {
                // Both endings have one way on, and it is not an exit: leaving
                // by R2 from here would be one press away from the button that
                // just saved.
                if (id == Id::SW2) { // R1
                    mShowEnded = false;
                    mDiscarded = false;
                }
                return;
            }
            switch (id) {
                case Id::SW2: // R1
                    mSender.sessionStart();
                    break;
                case Id::SW4: // R2
                    mKernel.sys.exit(0);
                    break;
                default:
                    break;
            }
            return;
    }
}
