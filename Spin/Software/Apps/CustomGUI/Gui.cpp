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

/// 1 kcal = 4.184 kJ. This converts *dietary* energy between its two units --
/// it is not, and must never be presented as, the kilojoules of mechanical work
/// a power meter reports. Those need power, which this watch cannot measure,
/// and would be roughly a quarter of this number for the same ride.
constexpr float kKilojoulesPerKilocalorie = 4.184f;

uint16_t displayEnergy(float kcal, bool asKilojoules)
{
    const float value = asKilojoules ? kcal * kKilojoulesPerKilocalorie : kcal;
    if (value <= 0.0f) {
        return 0u;
    }
    // The frame carries a uint16; a ride big enough to overflow it has gone
    // wrong somewhere else, but clamping beats wrapping to a small number.
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
    // Mirrors SDK::Gui::SensorStatusRow::hrState(): UNAVAILABLE(0)/IDLE(1) ->
    // absent, SEARCHING(2)/CONNECTING(3)/LOST(5) -> searching, CONNECTED(4) ->
    // connected. That header is TouchGFX-only, so the mapping is repeated here
    // rather than included -- it is three cases and one comment.
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

    if (mHoldingDiscard) {
        out.screen = SPIN_GUI_SCREEN_CONFIRM_DISCARD;
        const uint16_t pct =
            static_cast<uint16_t>(mHoldTicks) * 100u / kHoldTicksForFull;
        out.hold_pct = static_cast<uint8_t>(pct > 100u ? 100u : pct);
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

    // Taken as published, not re-judged here. The Service already decided what
    // the screen should believe -- including holding a reading through a
    // momentary dip in the arbiter's confidence -- and 0 already means "no
    // heart rate". Re-applying the trust gate here was what blanked the number
    // for a single second at a time on a signal that was not actually moving.
    out.hr_bpm    = static_cast<uint16_t>(mTrackData.hr + 0.5f);
    out.hr_source = (mTrackData.hr > 0.0f) ? mTrackData.hrSource : SPIN_GUI_HR_NONE;

    // Passed through, never recomputed here from elapsed_s against the target:
    // this is the same flag that fired the buzz, so the screen cannot announce
    // the target a second before or after the wrist felt it.
    out.target_reached = mTrackData.targetReached ? 1u : 0u;

    out.hr_zone   = mTrackData.hrZone;
    out.has_zones = mTrackData.hasZones ? 1u : 0u;
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

    // Discard is held, not tapped -- the SDK's own activity apps gate it behind
    // a hold and it is the one action here that destroys data. The kernel times
    // the hold and says when it is long enough; releasing before that cancels.
    // It sits on L2, the bottom-left button, a whole panel away from L1's
    // Finish: the two endings of a ride should not be neighbours.
    if (mHoldingDiscard) {
        if (id != Id::SW3) {
            return;
        }
        if (event == Event::HOLD_1S) {
            LOG_INFO("Discard confirmed\n");
            mHoldingDiscard = false;
            sender.trackStop(true);
        } else if (event == Event::RELEASE) {
            LOG_INFO("Discard cancelled\n");
            mHoldingDiscard = false;
            renderAndPush();
        }
        return;
    }

    if (event == Event::PRESS) {
        // Only PAUSED offers it: a ride you are still riding is not one you are
        // deciding about, and one already saved is on disk.
        if (id == Id::SW3 && !mShowSaved && mTrackState == Track::State::PAUSED) {
            mHoldingDiscard = true;
            mHoldTicks      = 0;
            renderAndPush();
        }
        return;
    }

    if (event != Event::CLICK) {
        return;
    }

    // R1 is the one button that does the obvious thing on every screen, so it
    // is the only one a wearer has to find mid-ride. R2 leaves, but never while
    // the clock is running -- an accidental exit there costs the whole ride.
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
            }
            break;

        case Track::State::PAUSED:
            if (id == Id::SW2) {            // R1: resume
                sender.trackResume();
            } else if (id == Id::SW1) {     // L1: finish and save
                sender.trackStop(false);
            }
            break;
    }
}

void Gui::run()
{
    LOG_INFO("Started\n");

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
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            // Nothing on any screen animates, and the Service publishes a
            // snapshot every second while a ride is running, so a redraw per
            // tick would be the same pixels at the tick rate. Ticks are
            // acknowledged and dropped; the messages below are what redraw.
            case SDK::MessageType::EVENT_GUI_TICK:
                msg->setResult(SDK::MessageResult::SUCCESS);
                // The one thing that animates. Everywhere else a tick would
                // redraw the same pixels at the tick rate.
                if (mHoldingDiscard) {
                    if (mHoldTicks < kHoldTicksForFull) {
                        ++mHoldTicks;
                    }
                    mKernel.comm.releaseMessage(msg);
                    renderAndPush();
                    continue;
                }
                break;

            case CustomMessage::TRACK_STATE_UPDATE: {
                const Track::State state =
                    static_cast<CustomMessage::TrackStateUpd *>(msg)->state;
                // A ride starting clears the last one's summary. Doing it here
                // rather than on the button press means it is the Service's
                // acknowledgement that clears the screen, so a start that was
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
