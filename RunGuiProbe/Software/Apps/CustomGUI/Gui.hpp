/**
 ******************************************************************************
 * @file    Gui.hpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The screen the probe is trying to open, and nothing more.
 ******************************************************************************
 *
 * Satisfies the SDK's CustomGUI contract -- `Gui { Gui(kernel); run(); }`, see
 * Libs/Source/AppSystem/EntryPoint/CustomGUI/main.cpp -- with no TouchGFX. The
 * pattern from RustGuiPoc, minus the Rust: this app needs a screen that is
 * unmistakably *this* screen, not a widget toolkit.
 *
 * There is no text, because text needs a font and a font needs either TouchGFX
 * or a rasteriser, and neither would make the answer any clearer. Three colour
 * bands inside a white frame cannot be confused with any other screen on the
 * watch, which is the entire requirement.
 *
 * ## It always leaves
 *
 * This loop owns the message queue and swallows every button, so an app with no
 * way out is an app you reboot the watch to escape -- and rebooting is the one
 * thing that would destroy the evidence in RAM about what just happened. So
 * there are two exits, and the second does not depend on anything working:
 *
 *   - R2 (SW4), the back button, as in every SDK app; and
 *   - a deadline. After `kMaxVisibleMs` the GUI exits on its own, whether or
 *     not any button event ever arrived. If buttons turn out not to reach a GUI
 *     launched this way, that is a finding, not a stuck watch.
 *
 ******************************************************************************
 */

#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "ProbeLog.hpp"

class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    /// Blocking: pumps the kernel message queue until it is told to stop, a
    /// button says to leave, or the deadline passes.
    void run();

private:
    void queryDisplayConfig();
    /// Paint the pattern into mFrameBuf. Called once -- nothing here animates.
    void render();
    void push();

    /// Filled rectangle, clipped to the framebuffer. The clip is not decoration:
    /// the display config is read from a message, and a wrong height here would
    /// write past a static buffer.
    void fill(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t colour);

    SDK::Kernel &mKernel;
    Probe::Log   mLog;

    int16_t  mWidth      = 0;
    int16_t  mHeight     = 0;
    uint8_t  mColorDepth = 8;
    bool     mResumed    = false;
    bool     mPainted    = false;

    /// Kernel timestamp of the first GUI tick, and whether one has arrived.
    uint32_t mStartMs   = 0;
    bool     mHaveStart = false;

    /// How long the probe screen stays up before leaving of its own accord.
    static constexpr uint32_t kMaxVisibleMs = 30000;

    /// Sized for the 8bpp ceiling of a 240x240 panel. The Gui object is
    /// placement-new'd into static storage by the CustomGUI entry point, so
    /// this lives in BSS rather than on a stack or a heap.
    static constexpr uint32_t kMaxPixels = 240u * 240u;
    uint8_t mFrameBuf[kMaxPixels];
};

#endif // GUI_HPP
