/**
 ******************************************************************************
 * @file    Gui.hpp
 * @brief   CustomGUI shim satisfying the SDK's `Gui { Gui(kernel); run(); }`
 *          contract (see Libs/Source/AppSystem/EntryPoint/CustomGUI/main.cpp).
 *
 * This class replaces TouchGFX entirely. It owns the display message loop, a
 * software framebuffer, and the display state; it delegates all pixel drawing to
 * the Rust core over the C ABI in poc_gui.h. No TouchGFX, no widget toolkit.
 ******************************************************************************
 */
#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "poc_gui.h"

class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    /// Blocking: pumps the kernel message queue until COMMAND_APP_STOP.
    void run();

private:
    void queryDisplayConfig();
    void renderAndPush();

    // Debug: write the current ABGR2222 framebuffer to a file for the desktop
    // sim to load (`cargo run --bin sim --features sim -- fb_dump.bin`). Lets you
    // byte-verify the device's framebuffer against what render() produces.
    void dumpFramebuffer();

    // A sample older than this is not shown. A sensor UI that keeps displaying
    // the last number it ever saw is worse than one that admits it has nothing:
    // at 10 Hz, half a second is five missed samples and something is wrong.
    static constexpr uint32_t kStaleAfterMs = 500;

    SDK::Kernel &mKernel;

    int16_t  mWidth      = 0;
    int16_t  mHeight     = 0;
    uint8_t  mColorDepth = 8;      // 8bpp ABGR2222 storage (config reports 6 = color bits only)
    bool     mResumed    = false;  // only push while the GUI is foreground
    uint32_t mScreen     = 0;      // which UI is shown; cycled by SW2

    // Everything the renderer is allowed to see, plus when it last changed.
    poc_gui_state mState{};
    uint32_t      mLastSampleMs = 0;
    bool          mHaveSample   = false;

    // Static framebuffer sized for the 8bpp ceiling of a 240x240 panel. The Gui
    // object itself is placement-new'd into static storage by the CustomGUI
    // entry, so this lives in BSS, not on the stack or heap.
    static constexpr uint32_t kMaxPixels = 240u * 240u;
    uint8_t mFrameBuf[kMaxPixels];
};

#endif // GUI_HPP
