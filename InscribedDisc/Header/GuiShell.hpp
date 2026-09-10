#ifndef INSCRIBED_DISC_GUISHELL_HPP
#define INSCRIBED_DISC_GUISHELL_HPP

// The C++ half of InscribedDisc: the framebuffer, the display config, the
// render and push, the message loop and the panic trampoline.
//
// Header-only so it type-checks on a host with
//   clang++ -fsyntax-only -std=c++17 \
//       -I"$UNA_SDK/Libs/Header" -I InscribedDisc/Header
// which catches a rename across the C ABI without an ARM toolchain.
//
// MEASURED: no std::string, no nothrow new and no exceptions, because each
// drags libstdc++'s EH runtime into an otherwise -fno-exceptions app -- 10,036
// bytes of one app's .uapp, 56,108 -> 46,072. Re-measure by reintroducing one
// and repacking.

#include <cstddef>
#include <cstdint>

#include "SDK/Interfaces/IKernel.hpp"
#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageTypes.hpp"

namespace inscribed_disc {

/// The four buttons, in the id order the event ABI carries them.
enum class Button : uint8_t {
    L1 = 0,
    R1 = 1,
    L2 = 2,
    R2 = 3,
};

/// What a press was.
enum class Press : uint8_t {
    Click = 0,
    Hold = 1,
};

/// What the Rust half asks the shell to do about an event.
///
/// `applyAction` switches over this without a default, so adding a value is a
/// compiler warning rather than a press that silently does nothing.
enum class Action : uint8_t {
    /// The frame changed and nothing else.
    Redraw = 0,
    /// Nothing changed; do not spend a push on it.
    Ignore = 1,
    /// Leave the app.
    Exit = 2,
    /// The app wants data only the shell can fetch; it will arrive as a payload.
    Request = 3,
};

/// Geometry and colour depth, as the kernel reports them.
struct DisplayConfig {
    int16_t width = 0;
    int16_t height = 0;
    uint8_t colorDepth = 0;
    /// False when the panel wants more than one byte a pixel, in which case no
    /// frame may be pushed at all.
    bool usable = false;
};

/// Compile-time configuration a shell may override.
struct Config {
    static constexpr int16_t kFallbackWidth = 240;
    static constexpr int16_t kFallbackHeight = 240;
    static constexpr uint32_t kBytesPerPixel = 1;
    static constexpr uint32_t kMaxPixels = 240u * 240u;
    static constexpr uint32_t kResponseTimeoutMs = 1000;
    /// PROVEN ON THE WATCH: this panel reports 6 -- ABGR2222, six colour bits
    /// and two of alpha, one byte a pixel. Anything above 8 would have the
    /// kernel read past the framebuffer, so a frame is withheld rather than
    /// truncated. Falsified by a panel reporting a depth this refuses.
    static constexpr uint8_t kMaxBitsPerPixel = 8;
    static constexpr uint32_t kWaitForever = 0xFFFFFFFFu;
};

/// What an app must supply to be pumped by [`Shell`].
///
/// A template rather than a base class, so there is no vtable.
///
/// ```
/// struct MyApp {
///     static uint32_t abiFingerprint();               // from the Rust side
///     static uint32_t expectedFingerprint();          // from the header
///     static void render(uint8_t* buf, uint32_t len, uint16_t w, uint16_t h);
///     static Action onButton(Button id, Press press);
///     static Action onTick(uint32_t nowMs);
///     static void onResume();
///     static void onSuspend();
/// };
/// ```
template <typename App>
class Shell {
public:
    explicit Shell(SDK::Kernel& kernel)
        : mKernel(kernel)
    {
    }

    Shell(const Shell&) = delete;
    Shell& operator=(const Shell&) = delete;

    /// Reads the panel's geometry, falling back to 240x240 and withholding
    /// frames entirely if the depth is one this renderer cannot write.
    void queryDisplayConfig()
    {
        auto* cfg = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayConfig>();
        if (cfg) {
            if (mKernel.comm.sendMessage(cfg, Config::kResponseTimeoutMs) &&
                cfg->getResult() == SDK::MessageResult::SUCCESS) {
                mDisplay.width = cfg->width;
                mDisplay.height = cfg->height;
                mDisplay.colorDepth = cfg->colorDepth;
            }
            mKernel.comm.releaseMessage(cfg);
        }

        const bool fitsFramebuffer =
            mDisplay.width > 0 && mDisplay.height > 0 &&
            static_cast<uint32_t>(mDisplay.width) * static_cast<uint32_t>(mDisplay.height) <=
                Config::kMaxPixels;

        if (!fitsFramebuffer) {
            mDisplay.width = Config::kFallbackWidth;
            mDisplay.height = Config::kFallbackHeight;
        }
        mDisplay.usable = mDisplay.colorDepth <= Config::kMaxBitsPerPixel;
    }

    const DisplayConfig& display() const { return mDisplay; }

    /// Renders one frame and hands the whole buffer to the kernel.
    ///
    /// Whole frames only: `RequestDisplayUpdate`'s x/y/width/height are marked
    /// reserved.
    void renderAndPush()
    {
        if (!mResumed || !mDisplay.usable) {
            return;
        }

        App::render(mFrameBuf, Config::kMaxPixels * Config::kBytesPerPixel,
                    static_cast<uint16_t>(mDisplay.width),
                    static_cast<uint16_t>(mDisplay.height));

        auto* upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
        if (upd) {
            upd->pBuffer = mFrameBuf;
            mKernel.comm.sendMessage(upd, Config::kResponseTimeoutMs);
            mKernel.comm.releaseMessage(upd);
        }
    }

    /// Whether the linked archive is the one this header describes.
    ///
    /// Compile-time offset assertions cannot answer this: a stale archive and a
    /// newer header each satisfy their own, having been compiled separately.
    bool abiMatches() const { return App::abiFingerprint() == App::expectedFingerprint(); }

    /// The pump. Returns the process exit code.
    int run()
    {
        if (!abiMatches()) {
            return 1;
        }
        queryDisplayConfig();

        for (;;) {
            SDK::MessageBase* msg = nullptr;
            if (!mKernel.comm.getMessage(msg, Config::kWaitForever)) {
                continue;
            }

            switch (msg->getType()) {

                case SDK::MessageType::COMMAND_APP_STOP:
                    msg->setResult(SDK::MessageResult::SUCCESS);
                    mKernel.comm.releaseMessage(msg);
                    return 0;

                case SDK::MessageType::COMMAND_APP_GUI_RESUME:
                    mResumed = true;
                    App::onResume();
                    msg->setResult(SDK::MessageResult::SUCCESS);
                    break;

                case SDK::MessageType::COMMAND_APP_GUI_SUSPEND:
                    mResumed = false;
                    App::onSuspend();
                    msg->setResult(SDK::MessageResult::SUCCESS);
                    break;

                case SDK::MessageType::EVENT_GUI_TICK: {
                    msg->setResult(SDK::MessageResult::SUCCESS);
                    mKernel.comm.releaseMessage(msg);
                    mTickMs += kTickPeriodMs;
                    if (applyAction(App::onTick(mTickMs))) {
                        return 0;
                    }
                    continue;
                }

                case SDK::MessageType::EVENT_BUTTON: {
                    auto* btn = static_cast<SDK::Message::EventButton*>(msg);
                    Button id;
                    if (mapButton(btn->id, id)) {
                        const Press press = (btn->event == SDK::Message::EventButton::Event::CLICK)
                                                ? Press::Click
                                                : Press::Hold;
                        msg->setResult(SDK::MessageResult::SUCCESS);
                        mKernel.comm.releaseMessage(msg);
                        if (applyAction(App::onButton(id, press))) {
                            return 0;
                        }
                        continue;
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

private:
    /// PROVEN ON THE WATCH: the GUI ticks at 10 fps, a median 100 ms frame gap
    /// across every hardware run, against the SDK's documented 30-60. Falsified
    /// by re-running the frame-gap capture in RustGuiPoc/Captures.
    static constexpr uint32_t kTickPeriodMs = 100;

    /// Returns true when the app should exit.
    bool applyAction(Action action)
    {
        switch (action) {
            case Action::Redraw:
                renderAndPush();
                return false;
            case Action::Ignore:
                return false;
            case Action::Request:
                // The app sees the result as a payload on a later tick.
                return false;
            case Action::Exit:
                return true;
        }
        return false;
    }

    static bool mapButton(typename SDK::Message::EventButton::Id id, Button& out)
    {
        using Id = typename SDK::Message::EventButton::Id;
        switch (id) {
            case Id::SW1: out = Button::L1; return true;
            case Id::SW2: out = Button::R1; return true;
            case Id::SW3: out = Button::L2; return true;
            case Id::SW4: out = Button::R2; return true;
            default: return false;
        }
    }

    SDK::Kernel& mKernel;
    DisplayConfig mDisplay{};
    bool mResumed = false;
    uint32_t mTickMs = 0;
    uint8_t mFrameBuf[Config::kMaxPixels * Config::kBytesPerPixel]{};
};

}  // namespace inscribed_disc

extern "C" {

/// Called by the kit's Rust panic handler. Must not return normally.
void inscribed_disc_host_panic(const uint8_t* msg, uint32_t len);
}

/// Defines `inscribed_disc_host_panic` to log and exit.
///
/// A macro because exactly one translation unit must define it.
#define INSCRIBED_DISC_DEFINE_HOST_PANIC(LOG_ERROR_FN)                                \
    extern "C" void inscribed_disc_host_panic(const uint8_t* msg, uint32_t len)       \
    {                                                                                 \
        LOG_ERROR_FN("Rust panic: %.*s\n", static_cast<int>(len),                     \
                     reinterpret_cast<const char*>(msg));                             \
        SDK::KernelProviderGUI::GetInstance().getKernel().sys.exit(1);                 \
        for (;;) {                                                                    \
        }                                                                             \
    }

#endif  // INSCRIBED_DISC_GUISHELL_HPP
