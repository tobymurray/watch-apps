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
#include "notify_toggle_gui.h"

#define LOG_MODULE_PRX   "NotifyGui"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

static constexpr uint32_t kWaitForever = 0xFFFFFFFF;

extern "C" void notify_toggle_host_panic(const uint8_t *msg, uint32_t len)
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
    mAddresses = FirmwareGate::resolve(mKernel.fs, gIKernel->version, outcome);
    mGateOutcome = outcome;
    if (!mAddresses) {
        LOG_ERROR("firmware not verified for this build -- refusing every raw address\n");
    }
    return mAddresses != nullptr;
}

// Works out what the screen is entitled to claim about the live byte.
void Gui::refreshLiveState()
{
    if (!mAddresses) {
        mState.enabled = 0;
        mState.known   = 0;
        mState.status  = (mGateOutcome == FirmwareGate::Outcome::SettingsUnreadable)
                             ? NOTIFY_TOGGLE_STATUS_NO_SETTINGS
                             : NOTIFY_TOGGLE_STATUS_UNSUPPORTED;
        return;
    }

    bool enabled = false;
    const auto status = LiveSettings::readNotificationsFlag(mKernel.fs, *mAddresses, enabled);

    const bool known = (status == LiveSettings::Status::Ok);
    if (!known) {
        LOG_WARNING("could not confirm the live notifications flag (status=%d)\n",
                    static_cast<int>(status));
    }

    mState.enabled = enabled ? 1 : 0;
    mState.known   = known ? 1 : 0;
    DebugLog::appendf(mKernel.fs, "live read -> status=%d enabled=%d known=%d",
                       static_cast<int>(status), mState.enabled, mState.known);

    if (!known) {
        mState.status = NOTIFY_TOGGLE_STATUS_UNREADABLE;
    } else if (mPersistFailed) {
        mState.status = NOTIFY_TOGGLE_STATUS_NOT_SAVED;
    } else if (!mSaveToSettings) {
        mState.status = NOTIFY_TOGGLE_STATUS_LIVE_ONLY;
    } else {
        mState.status = NOTIFY_TOGGLE_STATUS_OK;
    }
}

void Gui::applyCapabilities(bool enabled)
{
    auto *msg = mKernel.comm.allocateMessage<SDK::Message::RequestSetCapabilities>();
    if (msg) {
        msg->enPhoneNotification = enabled;
        msg->enUsbChargingScreen = true;
        msg->enMusicControl      = true;
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}

void Gui::toggle()
{
    if (!mAddresses) {
        LOG_WARNING("toggle: firmware not supported; not touching settings.json\n");
        return;
    }

    mPersistFailed = false;

    bool current = false;
    if (LiveSettings::readNotificationsFlag(mKernel.fs, *mAddresses, current) != LiveSettings::Status::Ok) {
        LOG_WARNING("toggle: could not confirm the current value; not writing\n");
        refreshLiveState();
        return;
    }

    const bool desired = !current;
    const auto status = LiveSettings::writeNotificationsFlag(mKernel.fs, *mAddresses, desired);

    if (status != LiveSettings::Status::Ok && status != LiveSettings::Status::NoChange) {
        LOG_ERROR("toggle: write failed (status=%d); left unchanged\n", static_cast<int>(status));
        refreshLiveState();
        return;
    }

    refreshLiveState();
    applyCapabilities(mState.enabled != 0);

    if (!mSaveToSettings) {
        DebugLog::append(mKernel.fs, "R1: saving is off, leaving settings.json alone");
        return;
    }

    // Deferred to here, not done at launch: the check writes scratch files, and
    // opening the app should not cost the wearer's flash a write it never asked
    // for. Once per run is enough -- the firmware cannot change under a run.
    if (!mPrimitivesChecked) {
        mPrimitivesChecked = true;
        mPrimitivesOk = SettingsPersist::validatePrimitives(mKernel.fs, *mAddresses);
    }
    if (!mPrimitivesOk) {
        LOG_ERROR("toggle: the File primitives did not behave; not writing\n");
        mPersistFailed = true;
        refreshLiveState();
        return;
    }

    // The live change is already what the wearer saw, so a failure here does not
    // undo it -- it changes what the screen may claim, nothing else.
    const auto persistStatus = SettingsPersist::persistNotificationsFlag(mKernel.fs, *mAddresses, desired);
    if (persistStatus != SettingsPersist::Status::Ok) {
        LOG_ERROR("toggle: persist to settings.json failed (status=%d); live value still changed\n",
                   static_cast<int>(persistStatus));
        DebugLog::appendf(mKernel.fs, "R1 persist FAILED: status=%d", static_cast<int>(persistStatus));
        mPersistFailed = true;
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

    notify_toggle_render(mFrameBuf, kMaxPixels * kBytesPerPixel,
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
    DebugLog::append(mKernel.fs, "=== NotifyToggle GUI started (debug build) ===");

    if (notify_toggle_abi_fingerprint() != notify_toggle_abi::fingerprint()) {
        LOG_ERROR("ABI mismatch: Rust 0x%08X, C++ 0x%08X -- stale libnotify_toggle_gui.a\n",
                  static_cast<unsigned>(notify_toggle_abi_fingerprint()),
                  static_cast<unsigned>(notify_toggle_abi::fingerprint()));
        DebugLog::append(mKernel.fs, "ABI fingerprint mismatch -- exiting");
        mKernel.sys.exit(1);
        return;
    }
    DebugLog::appendf(mKernel.fs, "ABI fingerprint OK (0x%08X)",
                       static_cast<unsigned>(notify_toggle_abi_fingerprint()));

    queryDisplayConfig();
    DebugLog::appendf(mKernel.fs, "display %dx%d @%ubpp usable=%d", mWidth, mHeight, mColorDepth,
                       mDisplayUsable ? 1 : 0);

    // Read before the gate, which it decides how much of to run. Not in the
    // constructor: reading it can log, and the simulator has no logger yet.
    {
        SDK::AppConfig config(mKernel, NotifyToggleConfig::kConfigFile, NotifyToggleConfig::kFields);
        mSaveToSettings = config.getBool(NotifyToggleConfig::kSaveToSettings);
        DebugLog::appendf(mKernel.fs, "config: loaded=%d saveToSettings=%d (set by the wearer=%d)",
                           config.isLoaded() ? 1 : 0, mSaveToSettings ? 1 : 0,
                           config.has(NotifyToggleConfig::kSaveToSettings) ? 1 : 0);
    }

    resolveFirmwareSupport();

    refreshLiveState();
    // IAppCapabilities scopes these to "while this app is running", so reopening
    // has to re-request the flag rather than merely display it.
    if (mState.known) {
        applyCapabilities(mState.enabled != 0);
    }

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

            case SDK::MessageType::EVENT_GUI_TICK: {
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);

                // Something else can write the same struct while this screen is
                // open, so the value is re-read rather than assumed unchanged.
                if (++mTicksSinceRead >= kReReadEveryTicks) {
                    mTicksSinceRead = 0;
                    const uint8_t before = mState.enabled;
                    const uint8_t beforeKnown = mState.known;
                    refreshLiveState();
                    if (mState.enabled != before || mState.known != beforeKnown) {
                        LOG_INFO("live value changed externally; display updated\n");
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

                if (btn->event == Event::CLICK && btn->id == Id::SW2) {
                    DebugLog::append(mKernel.fs, "R1 pressed");
                    toggle();
                    LOG_INFO("Toggled: notifications now %s (known=%d)\n",
                             mState.enabled ? "ON" : "OFF", mState.known);
                    DebugLog::appendf(mKernel.fs, "R1 result: enabled=%d known=%d screenStatus=%d",
                                       mState.enabled, mState.known, mState.status);
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
