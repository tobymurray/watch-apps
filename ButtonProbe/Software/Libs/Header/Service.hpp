/**
 ******************************************************************************
 * @file    Service.hpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The glance half: can a service on the carousel see a button at all?
 ******************************************************************************
 *
 * The GUI half finds out *which pin is which button*, with the kernel supplying
 * the labels. This half asks the question that actually matters for the feature
 * behind all of this: **can a glance service see the edge?**
 *
 * They are genuinely different questions. A GUI is the foreground app; the
 * kernel is feeding it button events and ticking it for animation, and it is
 * unsurprising that it can also read a register. A glance service is a
 * different process in a different lifecycle, woken by a carousel that owns the
 * buttons itself. Everything about it might differ -- the scheduling it gets,
 * whether it is running at all at the instant of the press, whether the
 * carousel scrolls the card away before a poll comes round.
 *
 * So this half runs the same sampler, on the same clock, in the message loop a
 * real glance would have, and writes what it sees to its own file.
 *
 * ## The card is the instrument
 *
 * It shows the number of edges seen and the last pin that moved -- `PD4>0`.
 * Press R1 while the card is up and, if this works at all, the card says which
 * pin it was. Which means the experiment can be run and read without unplugging
 * anything; the log file is for afterwards, and for the parts too fast to read.
 *
 * ## Two phases, chosen by a file
 *
 * The app has two experiments in it and they cannot both run at once: one
 * samples pins from the glance, the other hands the screen to the GUI so the
 * kernel will name the buttons. Something has to choose, and the glance has no
 * button to be told with -- which is the whole problem this app exists to
 * study.
 *
 * So the choice is a file. Drop an empty **`gui.on`** into the app's folder over
 * USB and the card, once you stop on it, asks the kernel to launch the GUI
 * (`RequestAppRunGui`, which RunGuiProbe established works from here). Delete
 * the file and the same card samples pins itself instead. The card says which
 * mode it is in, so there is no guessing which experiment just ran.
 *
 * A marker file rather than a config value with a parser: it is one bit, it is
 * set by creating or deleting a file, and both states are visible in a
 * directory listing.
 *
 * ## Why it polls instead of waiting
 *
 * `RunGuiProbe` measured the glance tick at about 1 Hz. A press is over inside
 * one tick. See Sampler.hpp -- the loop below blocks on `getMessage` with a
 * short timeout rather than waiting for the tick, and that choice is the thing
 * this app is really testing.
 *
 ******************************************************************************
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Glance/GlanceControl.hpp"

#include "ProbeLog.hpp"
#include "Sampler.hpp"

class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    ~Service() = default;

    void run();

private:
    bool glanceConfig();
    void glanceCreate();

    /// Ask the kernel to launch this app's GUI. Only in `gui.on` mode.
    void requestGui();

    /// Work out what the two lines should say and set the ones that changed.
    void refresh();
    void push();

    static constexpr uint32_t kControlsNeeded = 2;

    SDK::Kernel   &mKernel;
    Probe::Log     mLog;
    Probe::Sampler mSampler;

    SDK::Glance::Form        mGlance;
    SDK::Glance::ControlText mTop;
    SDK::Glance::ControlText mBottom;

    bool mActive  = false;

    /// Whether `gui.on` was present when this viewing started, and whether the
    /// launch has been asked for yet. Read once per viewing rather than per
    /// poll: this loop goes round more than a hundred times a second and the
    /// answer cannot change under it in any way that matters.
    bool mGuiMode   = false;
    bool mRequested = false;
    bool mLaunched  = false;

    /// Kernel milliseconds at the start of this viewing, for the launch dwell.
    uint32_t mStartMs = 0;

    /// How long the card is left up before asking for the GUI. Long enough to
    /// read the card and to scroll past without triggering it.
    static constexpr uint32_t kLaunchDwellMs = 2000;

    /// The form is built once per process, not once per viewing:
    /// `createText()` appends. See RunGuiProbe, which learned this the same way.
    bool mCreated = false;

    /// What the two lines currently hold, because the SDK will not say.
    char mShownTop[GLANCE_TEXT_SIZE]    = {};
    char mShownBottom[GLANCE_TEXT_SIZE] = {};
    bool mHaveShown = false;
};

#endif // SERVICE_HPP
