/**
 ******************************************************************************
 * @file    Gui.hpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The labelled half of the experiment: the kernel names the button,
 *          the sampler names the pin.
 ******************************************************************************
 *
 * A GUI receives `EVENT_BUTTON`, and it says which button and what happened --
 * `id=3 event=2` is R2, clicked. A GUI can also read the input registers. Run
 * both in one loop and the log answers the question nothing else can: **which
 * pin is which button**, with the kernel itself supplying the labels.
 *
 * That is why the discovery half is a GUI and not the glance. Doing it on the
 * glance would mean pressing buttons with no ground truth about which one the
 * kernel thought it was, and inferring the mapping from the order somebody
 * remembers pressing things in. Here the two streams go into one file with one
 * clock, and the correlation is a fact in the file rather than a claim about
 * the session.
 *
 * ## Leaving
 *
 * R2 exits, as everywhere -- but not instantly. The press that leaves is also
 * data, and quitting on the message would cut the log off before the release
 * edge that completes it. So R2 schedules the exit a few hundred milliseconds
 * out and the loop keeps sampling until then. There is a deadline behind that,
 * as in RunGuiProbe, so no sequence of events leaves the watch stuck in here.
 *
 ******************************************************************************
 */

#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "ProbeLog.hpp"
#include "Sampler.hpp"

class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    void run();

private:
    void queryDisplayConfig();
    void render();
    void push();
    void fill(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t colour);

    SDK::Kernel &mKernel;
    Probe::Log   mLog;
    Probe::Sampler mSampler;

    int16_t mWidth   = 0;
    int16_t mHeight  = 0;
    bool    mResumed = false;
    /// Set whenever the screen needs repainting: the picture only changes when
    /// the calibration ends or a pin moves, and pushing an unchanged
    /// framebuffer sixty times a second is a lot of IPC to say nothing.
    bool    mDirty   = true;

    /// Which colour the marker is showing. Advanced by every transition, so the
    /// screen changes the instant a pin moves -- live proof the sampler works,
    /// without unplugging the watch to read a file.
    uint32_t mMarker = 0;

    /// When to leave, and whether that has been decided yet.
    uint32_t mLeaveAtMs = 0;
    bool     mLeaving   = false;

    /// Grace period between R2 and actually going, so the release edge of the
    /// press that ends the session is in the log like every other one.
    static constexpr uint32_t kLeaveDelayMs = 400;
    /// Backstop. Long enough to press four buttons a few times each.
    static constexpr uint32_t kMaxVisibleMs = 120000;

    uint32_t mStartMs   = 0;
    bool     mHaveStart = false;

    static constexpr uint32_t kMaxPixels = 240u * 240u;
    uint8_t mFrameBuf[kMaxPixels];
};

#endif // GUI_HPP
