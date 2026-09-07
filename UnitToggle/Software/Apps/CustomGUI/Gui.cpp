#include "Gui.hpp"

#include "SDK/Interfaces/IKernel.hpp"
#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include "SDK/AppConfig/AppConfig.hpp"

#include "AppConfigFields.hpp"
#include "DebugLog.hpp"
#include "FirmwareGate.hpp"
#include "LiveSettings.hpp"
#include "SettingsPersist.hpp"
#include "UnitSetting.hpp"
#include "unit_toggle_gui.h"

#define LOG_MODULE_PRX   "UnitGui"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

static constexpr uint32_t kWaitForever = 0xFFFFFFFF;

extern "C" void unit_toggle_host_panic(const uint8_t *msg, uint32_t len)
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
    // This panel reports 6 -- ABGR2222, six colour bits and two of alpha, one
    // byte a pixel. More than a byte would have the kernel read past mFrameBuf.
    mDisplayUsable = mColorDepth <= kMaxBitsPerPixel;
    if (!mDisplayUsable) {
        LOG_ERROR("Panel wants %ubpp; this renderer only writes one byte per pixel\n",
                  mColorDepth);
    }
    LOG_INFO("Display %dx%d @ %ubpp\n", mWidth, mHeight, mColorDepth);
}

// Patched by the loader (system.cpp); `version` is the ABI the kernel presents,
// which FirmwareGate.hpp treats as a floor rather than an identity.
extern const SDK::Interface::IKernel *gIKernel;

bool Gui::resolveFirmwareSupport()
{
    FirmwareGate::Outcome outcome = FirmwareGate::Outcome::UnknownFirmware;
    mAddresses = FirmwareGate::resolve(mKernel, gIKernel->version, outcome);
    if (!mAddresses) {
        LOG_ERROR("firmware not verified for this build -- refusing every raw address\n");
    }
    return mAddresses != nullptr;
}

bool Gui::readKernelUnits(bool &outImperial)
{
    auto *settings = mKernel.comm.allocateMessage<SDK::Message::RequestSystemSettings>();
    if (settings == nullptr) {
        return false;
    }
    const bool answered = mKernel.comm.sendMessage(settings, kResponseTimeoutMs) &&
                          settings->getResult() == SDK::MessageResult::SUCCESS;
    const bool imperial = settings->imperialUnits;
    mKernel.comm.releaseMessage(settings);

    if (!answered) {
        return false;
    }
    outImperial = imperial;
    return true;
}

bool Gui::rawAgreesWithKernel(bool kernelImperial)
{
    bool rawImperial = false;
    if (LiveSettings::readFlag(mKernel.fs, *mAddresses, UnitSetting::liveFlag(*mAddresses),
                               rawImperial) != LiveSettings::Status::Ok) {
        return false;
    }

    DebugLog::appendf(mKernel.fs, "witness: raw=%d kernel=%d (%s)", rawImperial ? 1 : 0,
                       kernelImperial ? 1 : 0,
                       rawImperial == kernelImperial ? "agree" : "DISAGREE");
    return rawImperial == kernelImperial;
}

// Works out what the screen is entitled to claim about the units.
void Gui::refreshLiveState()
{
    // The supported message first, and on its own: it answers whatever the gate
    // decided, so a firmware this app may not write to can still be told the
    // truth about what its units are.
    bool kernelImperial = false;
    const bool kernelAnswered = readKernelUnits(kernelImperial);

    mState.imperial = kernelImperial ? 1 : 0;
    mState.known    = kernelAnswered ? 1 : 0;

    if (!mAddresses) {
        // Whichever way the gate refused, it never opened a file -- what it
        // decides is whether R1 may write, not whether the units can be read.
        mState.status = kernelAnswered ? UNIT_TOGGLE_STATUS_UNSUPPORTED
                                       : UNIT_TOGGLE_STATUS_UNREADABLE;
        return;
    }

    // Past the gate, the raw byte is what R1 will write, so it has to agree
    // with the message before the screen claims anything about it. Disagreement
    // means the offset is not the units field on this firmware.
    if (!kernelAnswered || !rawAgreesWithKernel(kernelImperial)) {
        LOG_WARNING("could not confirm the units against the kernel's own report\n");
        mState.known  = 0;
        mState.status = UNIT_TOGGLE_STATUS_UNREADABLE;
        return;
    }

    switch (mLastPress) {
        case PressOutcome::Unreliable:
            // The read above agrees, and says nothing: the revert put the byte
            // back to a value that already agreed. Only this remembers that the
            // press proved the address is not the units field.
            mState.known  = 0;
            mState.status = UNIT_TOGGLE_STATUS_UNREADABLE;
            return;
        case PressOutcome::NotPersisted:
            if (mState.imperial == mLastPressValue) {
                mState.status = UNIT_TOGGLE_STATUS_NOT_SAVED;
                return;
            }
            // Something else has set the units since -- the phone app, most
            // likely -- so what this press failed to write is no longer what
            // the screen is showing.
            mLastPress = PressOutcome::Clean;
            break;
        case PressOutcome::Clean:
            break;
    }

    mState.status = mSaveToSettings ? UNIT_TOGGLE_STATUS_OK : UNIT_TOGGLE_STATUS_LIVE_ONLY;
}

void Gui::toggle()
{
    if (!mAddresses) {
        LOG_WARNING("switch: firmware not supported; not touching the units\n");
        return;
    }

    if (mLastPress == PressOutcome::Unreliable) {
        // A press already established that this byte is not the units field.
        // Pressing again would write it twice more -- forward, then back -- in
        // kernel RAM on a part with no MPU, and learn nothing new.
        LOG_WARNING("switch: the address failed its own witness this run; not writing again\n");
        DebugLog::append(mKernel.fs, "R1 ignored: address already proved unreliable this run");
        return;
    }

    mLastPress = PressOutcome::Clean;

    bool current = false;
    if (LiveSettings::readFlag(mKernel.fs, *mAddresses, UnitSetting::liveFlag(*mAddresses),
                               current) != LiveSettings::Status::Ok) {
        LOG_WARNING("switch: could not confirm the current units; not writing\n");
        mLastPress = PressOutcome::Unreliable;
        refreshLiveState();
        return;
    }

    const bool desired = !current;
    const auto status = LiveSettings::writeFlag(mKernel.fs, *mAddresses,
                                                UnitSetting::liveFlag(*mAddresses), desired);

    if (status != LiveSettings::Status::Ok && status != LiveSettings::Status::NoChange) {
        LOG_ERROR("switch: write failed (status=%d); left unchanged\n", static_cast<int>(status));
        mLastPress = PressOutcome::Unreliable;
        refreshLiveState();
        return;
    }

    // If the byte just written is really the units field, the kernel's own
    // report of it changes too; when it does not, this wrote to something else,
    // so put the byte back and claim nothing.
    bool kernelImperial = false;
    if (!readKernelUnits(kernelImperial) || kernelImperial != desired) {
        LOG_ERROR("switch: the kernel does not report the units this just wrote; reverting\n");
        DebugLog::appendf(mKernel.fs, "R1: kernel reports %d after writing %d -- reverting",
                           kernelImperial ? 1 : 0, desired ? 1 : 0);
        LiveSettings::writeFlag(mKernel.fs, *mAddresses, UnitSetting::liveFlag(*mAddresses),
                                current);
        mLastPress = PressOutcome::Unreliable;
        refreshLiveState();
        return;
    }

    refreshLiveState();

    if (!mSaveToSettings) {
        DebugLog::append(mKernel.fs, "R1: saving is off, leaving settings.json alone");
        return;
    }

    // Deferred to here, not done at launch: the check writes scratch files, and
    // opening the app should not cost the wearer's flash a write it never asked
    // for. Once per run is enough -- the firmware cannot change under a run.
    if (!mPrimitivesChecked) {
        mPrimitivesChecked = true;
        mPrimitivesOk =
            SettingsPersist::validatePrimitives(mKernel.fs, *mAddresses, UnitSetting::kField);
    }
    if (!mPrimitivesOk) {
        LOG_ERROR("switch: the File primitives did not behave; not writing\n");
        mLastPress      = PressOutcome::NotPersisted;
        mLastPressValue = desired ? 1 : 0;
        refreshLiveState();
        return;
    }

    // The live change is already what the wearer saw, so a failure here does not
    // undo it -- it changes what the screen may claim, nothing else.
    const auto persistStatus =
        SettingsPersist::persistFlag(mKernel.fs, *mAddresses, UnitSetting::kField, desired);
    if (persistStatus != SettingsPersist::Status::Ok) {
        LOG_ERROR("switch: persist to settings.json failed (status=%d); live value still changed\n",
                   static_cast<int>(persistStatus));
        DebugLog::appendf(mKernel.fs, "R1 persist FAILED: status=%d", static_cast<int>(persistStatus));
        mLastPress      = PressOutcome::NotPersisted;
        mLastPressValue = desired ? 1 : 0;
        refreshLiveState();
    } else {
        DebugLog::append(mKernel.fs, "R1 persist OK");
    }
}

void Gui::renderAndPush()
{
    if (!mResumed || !mDisplayUsable) {
        return;
    }

    unit_toggle_render(mFrameBuf, kMaxPixels * kBytesPerPixel,
                       static_cast<uint16_t>(mWidth), static_cast<uint16_t>(mHeight),
                       &mState);

    auto *upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (upd) {
        upd->pBuffer = mFrameBuf;
        mKernel.comm.sendMessage(upd, kResponseTimeoutMs);
        mKernel.comm.releaseMessage(upd);
    }
}

void Gui::run()
{
    LOG_INFO("Started\n");
    DebugLog::setLogPath("gui-debug.log");
    DebugLog::appendf(mKernel.fs, "=== UnitToggle GUI %s started (debug build) ===",
                       UNIT_TOGGLE_VERSION);
    LOG_INFO("UnitToggle %s\n", UNIT_TOGGLE_VERSION);

    if (unit_toggle_abi_fingerprint() != unit_toggle_abi::fingerprint()) {
        LOG_ERROR("ABI mismatch: Rust 0x%08X, C++ 0x%08X -- stale libunit_toggle_gui.a\n",
                  static_cast<unsigned>(unit_toggle_abi_fingerprint()),
                  static_cast<unsigned>(unit_toggle_abi::fingerprint()));
        DebugLog::append(mKernel.fs, "ABI fingerprint mismatch -- exiting");
        mKernel.sys.exit(1);
        return;
    }
    DebugLog::appendf(mKernel.fs, "ABI fingerprint OK (0x%08X)",
                       static_cast<unsigned>(unit_toggle_abi_fingerprint()));

    queryDisplayConfig();
    DebugLog::appendf(mKernel.fs, "display %dx%d @%ubpp usable=%d", mWidth, mHeight, mColorDepth,
                       mDisplayUsable ? 1 : 0);

    // Read before the gate, which it decides how much of to run. Not in the
    // constructor: reading it can log, and the simulator has no logger yet.
    {
        SDK::AppConfig config(mKernel, UnitToggleConfig::kConfigFile, UnitToggleConfig::kFields);
        mSaveToSettings = config.getBool(UnitToggleConfig::kSaveToSettings);
        DebugLog::appendf(mKernel.fs, "config: loaded=%d saveToSettings=%d (set by the wearer=%d)",
                           config.isLoaded() ? 1 : 0, mSaveToSettings ? 1 : 0,
                           config.has(UnitToggleConfig::kSaveToSettings) ? 1 : 0);
    }

    resolveFirmwareSupport();

    // For the case the firmware cannot heal itself: it restores a missing
    // settings.json from its own backup at boot, so this only ever fires when
    // that backup is gone too. Runs whether or not saving is on, and only ever
    // moves back a file this mechanism moved.
    if (mAddresses && SettingsPersist::recoverInterruptedCommit(mKernel.fs, *mAddresses)) {
        LOG_WARNING("recovered a settings file left aside by an interrupted commit\n");
    }

    refreshLiveState();

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
                // The phone app can change the units while this app is
                // suspended, so the next tick re-reads rather than serving a
                // frame from before it. Scheduled rather than done here: no
                // other app in this repo blocks a resume on a kernel round-trip.
                mTicksSinceRead = kReReadEveryTicks;
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            case SDK::MessageType::COMMAND_APP_GUI_SUSPEND:
                mResumed = false;
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            case SDK::MessageType::EVENT_GUI_TICK: {
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);

                // The phone app can change the same setting while this screen is
                // open, so the value is re-read rather than assumed unchanged.
                // Only while it is on screen: polling the kernel to redraw
                // nothing costs two messages a second for no one.
                if (mResumed && ++mTicksSinceRead >= kReReadEveryTicks) {
                    mTicksSinceRead = 0;
                    const uint8_t before = mState.imperial;
                    const uint8_t beforeKnown = mState.known;
                    refreshLiveState();
                    if (mState.imperial != before || mState.known != beforeKnown) {
                        LOG_INFO("units changed externally; display updated\n");
                    }
                }

                renderAndPush();
                continue;
            }

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

                // Not while suspended: a press that reaches a screen nobody is
                // looking at would change the watch-wide setting with nothing
                // drawn to say it had.
                if (btn->event == Event::CLICK && btn->id == Id::SW2 && mResumed) {
                    DebugLog::append(mKernel.fs, "R1 pressed");
                    toggle();
                    LOG_INFO("Switched: units now %s (known=%d)\n",
                             mState.imperial ? "imperial" : "metric", mState.known);
                    DebugLog::appendf(mKernel.fs, "R1 result: imperial=%d known=%d screenStatus=%d",
                                       mState.imperial, mState.known, mState.status);
                    renderAndPush();
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
