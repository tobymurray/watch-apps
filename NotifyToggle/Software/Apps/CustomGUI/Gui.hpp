#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "notify_toggle_gui.h"

class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    void run();

private:
    void queryDisplayConfig();
    void renderAndPush();
    void refreshLiveState();
    void toggle();
    void applyCapabilities(bool enabled);

    static constexpr int16_t  kFallbackWidth     = 240;
    static constexpr int16_t  kFallbackHeight    = 240;
    static constexpr uint32_t kBytesPerPixel     = 1;
    static constexpr uint32_t kMaxPixels         = 240u * 240u;
    static constexpr uint32_t kResponseTimeoutMs = 1000;

    // Stretch goal: notice the live value changing out from under this app
    // (e.g. the phone app writing the same struct while this screen is open)
    // and refresh without waiting for a toggle. The GUI ticks at ~10 fps
    // (RustGuiPoc's Docs/FINDINGS.md), so this polls about once a second
    // rather than every tick -- plenty responsive for "did something else
    // change this".
    static constexpr uint32_t kReReadEveryTicks = 10;

    SDK::Kernel &mKernel;

    int16_t  mWidth      = 0;
    int16_t  mHeight     = 0;
    uint8_t  mColorDepth = 8;
    bool     mResumed    = false;
    uint32_t mTicksSinceRead = 0;

    notify_toggle_state mState{};

    uint8_t mFrameBuf[kMaxPixels * kBytesPerPixel];
};

#endif // GUI_HPP
