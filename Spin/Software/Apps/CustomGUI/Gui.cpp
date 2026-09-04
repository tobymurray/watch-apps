#include "Gui.hpp"

#include <cstring>

#include "Commands.hpp"

#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Kernel/KernelProviderGUI.hpp"

#include "spin_gui.h"

#define LOG_MODULE_PRX   "SpinGui"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{
constexpr uint32_t kWaitForever = 0xFFFFFFFF;

/// 1 kcal = 4.184 kJ. DIETARY energy between its two units -- not the kJ of
/// mechanical work a bike console reports, which is roughly a quarter of this
/// for the same ride and is entered by the wearer. See work.rs.
constexpr float kKilojoulesPerKilocalorie = 4.184f;

uint16_t displayEnergy(float kcal, bool asKilojoules)
{
    const float value = asKilojoules ? kcal * kKilojoulesPerKilocalorie : kcal;
    if (value <= 0.0f) {
        return 0u;
    }
    // The frame carries a uint16, and clamping beats wrapping to a small
    // number.
    constexpr float kMax = 65535.0f;
    return static_cast<uint16_t>((value > kMax ? kMax : value) + 0.5f);
}

} // namespace

extern "C" void spin_gui_host_panic(const uint8_t *msg, uint32_t len)
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

uint8_t Gui::strapFromAccessoryState(uint8_t accessoryState)
{
    // Mirrors SDK::Gui::SensorStatusRow::hrState(), whose header is
    // TouchGFX-only and cannot be included here.
    switch (accessoryState) {
        case 2: case 3: case 5: return SPIN_GUI_STRAP_SEARCHING;
        case 4:                 return SPIN_GUI_STRAP_CONNECTED;
        default:                return SPIN_GUI_STRAP_ABSENT;
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

void Gui::buildFrame(spin_gui_frame &out) const
{
    std::memset(&out, 0, sizeof(out));

    out.strap          = mStrap;
    out.target_minutes = mTargetMinutes;

    out.energy_is_kj = mEnergyInKilojoules ? 1u : 0u;

    if (mConfirmingDiscard) {
        out.screen = SPIN_GUI_SCREEN_CONFIRM_DISCARD;
        return;
    }

    if (mEnteringWork) {
        out.screen  = SPIN_GUI_SCREEN_ENTER_WORK;
        out.work_kj = mWorkKilojoules;
        // A reference beside the field, never a value in it -- see work.rs.
        out.work_estimate_kj = spin_gui_work_estimate_kj(mTrackData.calories);
        return;
    }

    if (mShowSaved && mDiscarded) {
        out.screen = SPIN_GUI_SCREEN_DISCARDED;
        return;
    }

    if (mShowSaved) {
        out.screen     = SPIN_GUI_SCREEN_SAVED;
        out.elapsed_s  = static_cast<uint32_t>(mSavedSeconds);
        out.avg_hr_bpm = static_cast<uint16_t>(mSavedAvgHr + 0.5f);
        out.energy     = displayEnergy(mSavedCalories, mEnergyInKilojoules);
        out.saved_ok   = mSavedOk ? 1u : 0u;
        return;
    }

    switch (mTrackState) {
        case Track::State::ACTIVE: out.screen = SPIN_GUI_SCREEN_RIDING; break;
        case Track::State::PAUSED: out.screen = SPIN_GUI_SCREEN_PAUSED; break;
        default:                   out.screen = SPIN_GUI_SCREEN_READY;  break;
    }

    out.elapsed_s = static_cast<uint32_t>(mTrackData.totalTime);

    // Taken as published: the Service already decided what the screen should
    // believe, and 0 already means "no heart rate". Re-applying the trust gate
    // here is what blanked the number a second at a time. See HrHold.hpp.
    out.hr_bpm    = static_cast<uint16_t>(mTrackData.hr + 0.5f);
    out.hr_source = (mTrackData.hr > 0.0f) ? mTrackData.hrSource : SPIN_GUI_HR_NONE;

    // Never recomputed from elapsed_s: this is the flag that fired the buzz.
    out.target_reached = mTrackData.targetReached ? 1u : 0u;

    out.hr_zone          = mTrackData.hrZone;
    out.zone_count       = mZoneCount;
    out.hr_zone_fraction = mTrackData.hrZoneFraction;
    out.has_zones        = mTrackData.hasZones ? 1u : 0u;

    // Taken as published, like the heart rate: the Service decides when a split
    // has stopped being worth showing, so this side holds no clock.
    out.last_lap_s = mTrackData.lastLapSeconds;
    out.lap_number = static_cast<uint16_t>(mTrackData.lapNum);
}

void Gui::renderAndPush()
{
    if (!mResumed) {
        return;
    }

    spin_gui_frame frame;
    buildFrame(frame);

    spin_gui_render(mFrameBuf, kMaxPixels * kBytesPerPixel,
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
    using Id = SDK::Message::EventButton::Id;
    using Event = SDK::Message::EventButton::Event;
    CustomMessage::Sender sender(mKernel);

    // HARDWARE, and why every screen here is CLICK-only: Service::
    // setCapabilities() sets enMusicControl = true, the system claims the long
    // press for its overlay, and HOLD_1S never reaches this app. Discard was a
    // press-and-hold once and never fired on the watch -- and the screen it
    // stranded the wearer on could only be left by that same event. Falsified
    // by that setting changing, and only re-testable on the watch.
    if (mConfirmingDiscard) {
        if (event != Event::CLICK) {
            return;
        }
        if (id == Id::SW2) {            // R1: yes
            LOG_INFO("Discard confirmed\n");
            mConfirmingDiscard = false;
            // A ride being thrown away has no work worth asking about.
            sender.trackStop(true, 0);
        } else if (id == Id::SW4) {     // R2: no
            LOG_INFO("Discard cancelled\n");
            mConfirmingDiscard = false;
            renderAndPush();
        }
        return;
    }

    if (event != Event::CLICK) {
        return;
    }

    // Both ways off this screen are labelled on it. The arithmetic is not here:
    // spin_gui_work_add_*() are pure functions of the current value, so what a
    // button does and what its label says come from the same constants.
    if (mEnteringWork) {
        if (id == Id::SW1) {            // L1: +100
            mWorkKilojoules = spin_gui_work_add_hundreds(mWorkKilojoules);
            renderAndPush();
        } else if (id == Id::SW3) {     // L2: +10
            mWorkKilojoules = spin_gui_work_add_tens(mWorkKilojoules);
            renderAndPush();
        } else if (id == Id::SW2) {     // R1: save, with whatever was entered
            LOG_INFO("Saving with %u kJ of work\n",
                     static_cast<unsigned>(mWorkKilojoules));
            mEnteringWork = false;
            sender.trackStop(false, mWorkKilojoules);
        } else if (id == Id::SW4) {     // R2: skip -- save the ride, say nothing
            LOG_INFO("Saving without a work figure\n");
            mEnteringWork = false;
            sender.trackStop(false, 0);
        }
        return;
    }

    // Only PAUSED offers it: a ride you are still riding is not one you are
    // deciding about, and one already saved is on disk.
    if (id == Id::SW3 && !mShowSaved && mTrackState == Track::State::PAUSED) {
        mConfirmingDiscard = true;
        renderAndPush();
        return;
    }

    // R1 acts on every screen and R2 leaves, but never while the clock is
    // running: an accidental exit there costs the whole ride.
    if (mShowSaved) {
        if (id == Id::SW2 || id == Id::SW4) {
            LOG_INFO("Leaving after a finished ride\n");
            mKernel.sys.exit(0);
        }
        return;
    }

    switch (mTrackState) {
        case Track::State::INACTIVE:
            if (id == Id::SW2) {            // R1: start
                sender.trackStart();
            } else if (id == Id::SW4) {     // R2: back out of the app
                LOG_INFO("Back pressed; exiting\n");
                mKernel.sys.exit(0);
            }
            break;

        case Track::State::ACTIVE:
            if (id == Id::SW2) {            // R1: pause
                sender.trackPause();
            } else if (id == Id::SW4) {     // R2: lap
                // The one screen where R2 does not leave. It is the lap button
                // on the SDK's own activity apps -- TrackView::handleKeyEvent
                // in the Cycling example -- and a wearer arriving from those
                // has that in their fingers. Matching the platform beats
                // matching this app's own rule, and R2 was free here precisely
                // because leaving mid-ride is what the rule forbids.
                sender.trackLap();
            }
            break;

        case Track::State::PAUSED:
            if (id == Id::SW2) {            // R1: resume
                sender.trackResume();
            } else if (id == Id::SW1) {     // L1: finish and save
                if (mAskForKilojoules) {
                    // Ask before stopping: the file is finalised when the
                    // Service handles TRACK_STOP. See Commands.hpp.
                    mEnteringWork   = true;
                    mWorkKilojoules = 0;
                    renderAndPush();
                } else {
                    sender.trackStop(false, 0);
                }
            }
            break;
    }
}

void Gui::run()
{
    LOG_INFO("Started\n");

    // A stale libspin_gui.a is otherwise silent until it draws garbage.
    if (spin_gui_abi_fingerprint() != spin_gui_abi::fingerprint()) {
        LOG_ERROR("ABI mismatch: Rust 0x%08X, C++ 0x%08X -- stale libspin_gui.a\n",
                  static_cast<unsigned>(spin_gui_abi_fingerprint()),
                  static_cast<unsigned>(spin_gui_abi::fingerprint()));
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
                // A question walked away from is not one they answered. The
                // ride is still PAUSED under both, so nothing is lost.
                mConfirmingDiscard = false;
                mEnteringWork      = false;
                mWorkKilojoules    = 0;
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            // Nothing animates, and the Service publishes a snapshot a second,
            // so a redraw per tick would be the same pixels at the tick rate.
            case SDK::MessageType::EVENT_GUI_TICK:
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            case CustomMessage::TRACK_STATE_UPDATE: {
                const Track::State state =
                    static_cast<CustomMessage::TrackStateUpd *>(msg)->state;
                // Here rather than on the button press, so a start the Service
                // refused leaves the summary up instead of blanking it.
                if (state == Track::State::ACTIVE && mTrackState == Track::State::INACTIVE) {
                    mShowSaved = false;
                    mDiscarded = false;
                }
                mTrackState = state;
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue;
            }

            case CustomMessage::TRACK_DATA_UPDATE:
                mTrackData = static_cast<CustomMessage::TrackDataUpd *>(msg)->data;
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue;

            case CustomMessage::ACCESSORY_STATUS: {
                auto *upd = static_cast<CustomMessage::AccessoryStatusUpd *>(msg);
                mStrap = strapFromAccessoryState(upd->state);
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue;
            }

            case CustomMessage::RIDE_CONFIG: {
                auto *cfg = static_cast<CustomMessage::RideConfigUpd *>(msg);
                mTargetMinutes       = cfg->targetMinutes;
                mEnergyInKilojoules  = cfg->energyInKilojoules;
                mZoneCount           = cfg->zoneCount;
                mAskForKilojoules    = cfg->askForKilojoules;
            }
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue;

            case CustomMessage::RIDE_SAVED: {
                auto *saved = static_cast<CustomMessage::RideSaved *>(msg);
                mSavedSeconds  = saved->duration;
                mSavedAvgHr    = saved->avgHr;
                mSavedCalories = saved->calories;
                mSavedOk       = saved->ok;
                mDiscarded     = saved->discarded;
                mShowSaved    = true;
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue;
            }

            case SDK::MessageType::EVENT_BUTTON: {
                auto *btn = static_cast<SDK::Message::EventButton *>(msg);
                handleButton(btn->id, btn->event);
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
