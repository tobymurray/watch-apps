#include "Gui.hpp"

#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include "DebugLog.hpp"
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
    LOG_INFO("Display %dx%d @ %ubpp\n", mWidth, mHeight, mColorDepth);
}

// Queries the watch's actual running firmware version via a supported SDK
// message -- never assumed, and never taken from the manifest's
// minKernelVersion, which is only a floor the phone's install flow checks
// (see SettingsAddresses.hpp for why an exact match matters here: a future
// firmware that still satisfies that floor could have moved every address
// this app depends on). Resolves mAddresses for that exact version, or
// leaves it null -- and every LiveSettings/SettingsPersist call site below
// refuses outright when it's null, rather than touching a single raw
// address on a firmware this app hasn't been reverse-engineered against.
bool Gui::resolveFirmwareSupport()
{
    auto *info = mKernel.comm.allocateMessage<SDK::Message::RequestSystemInfo>();
    if (!info) {
        LOG_ERROR("resolveFirmwareSupport: could not allocate RequestSystemInfo\n");
        return false;
    }

    bool ok = false;
    if (mKernel.comm.sendMessage(info, kResponseTimeoutMs) &&
        info->getResult() == SDK::MessageResult::SUCCESS) {
        DebugLog::appendf(mKernel.fs, "firmwareVersion=%s", info->firmwareVersion);
        mAddresses = SettingsAddresses::resolve(info->firmwareVersion);
        ok = (mAddresses != nullptr);
        if (!ok) {
            LOG_ERROR("firmware %s not reverse-engineered for this app -- refusing to touch settings.json\n",
                       info->firmwareVersion);
            DebugLog::appendf(mKernel.fs, "unsupported firmware %s -- refusing", info->firmwareVersion);
        }
    } else {
        LOG_ERROR("resolveFirmwareSupport: could not query RequestSystemInfo\n");
        DebugLog::append(mKernel.fs, "could not query firmware version -- refusing");
    }

    mKernel.comm.releaseMessage(info);
    return ok;
}

// Reads the kernel's live, in-RAM WatchSettings.phone.notifications byte
// directly (see LiveSettings.hpp for the address derivation and why the
// SDK's sandboxed filesystem API is conclusively unreachable to
// settings.json on this firmware -- LiveSettings.hpp's own doc comment has
// the full story; the file-based approach that first ruled it out is gone
// from this tree now, kept only in git history and in
// Docs/Investigations/2026-08-31-live-settings-persistence/).
// Called at startup, after every toggle, and periodically while
// the app sits open, so what this screen shows is never more than
// kReReadEveryTicks ticks stale relative to the live value.
void Gui::refreshLiveState()
{
    if (!mAddresses) {
        mState.enabled = 0;
        mState.known   = 0;
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

    // LiveSettings::writeNotificationsFlag reads fresh internally too (never
    // trusts mState), but the desired value has to be computed from a fresh
    // read here regardless, since it is this call site that decides "the
    // other value".
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

    // Best-effort: persist to 2:/settings.json so the change survives a
    // reboot, not just this power-on. A failure here does not undo or block
    // the live change above, which already succeeded and is what the user
    // just saw take effect -- it's logged for diagnosis via the same
    // DebugLog channel everything else in this app uses.
    const auto persistStatus = SettingsPersist::persistNotificationsFlag(mKernel.fs, *mAddresses, desired);
    if (persistStatus != SettingsPersist::Status::Ok) {
        LOG_ERROR("toggle: persist to settings.json failed (status=%d); live value still changed\n",
                   static_cast<int>(persistStatus));
        DebugLog::appendf(mKernel.fs, "R1 persist FAILED: status=%d", static_cast<int>(persistStatus));
    } else {
        DebugLog::append(mKernel.fs, "R1 persist OK");
    }
}

void Gui::renderAndPush()
{
    if (!mResumed) {
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
    DebugLog::append(mKernel.fs, "ABI fingerprint OK");

    queryDisplayConfig();
    DebugLog::appendf(mKernel.fs, "display %dx%d @%ubpp", mWidth, mHeight, mColorDepth);

    // Gates every LiveSettings/SettingsPersist call for the rest of this
    // run: on an unsupported firmware, mAddresses stays null and the app
    // still launches, still shows a screen, still lets R2 back out -- it
    // just can never read or write the real flag, and says so via the same
    // "unknown" state refreshLiveState() already shows for any other
    // fail-closed case.
    resolveFirmwareSupport();

    refreshLiveState();
    // Applied immediately at startup, not just on a toggle: capabilities are
    // scoped to "while this app is running" (IAppCapabilities.hpp), so
    // reopening the app has to re-request whatever the real flag says now,
    // not merely display it.
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

                // Stretch goal: pick up an external change (e.g. the phone
                // app writing the same live struct via BLE while this screen
                // is open) without requiring a toggle or a relaunch.
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
                    DebugLog::appendf(mKernel.fs, "R1 result: enabled=%d known=%d",
                                       mState.enabled, mState.known);
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
