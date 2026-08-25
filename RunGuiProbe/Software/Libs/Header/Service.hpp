/**
 ******************************************************************************
 * @file    Service.hpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The glance half: waits on its own card, then asks for a screen.
 ******************************************************************************
 *
 * One experiment, run on a watch:
 *
 *   A `Glance`-type app's service is started by the carousel, ticked while its
 *   card is on screen, and stopped when the card scrolls away. It never
 *   receives a button event -- `EVENT_BUTTON` goes to a GUI, and while the
 *   carousel is up there is no GUI. So a glance cannot react to R1.
 *
 *   But `RequestAppRunGui` exists, and it is a *service* message: "kernel,
 *   launch my GUI." Nothing in the SDK says a glance service may not send it.
 *   Nothing says it may, either.
 *
 * This app sends it, three seconds after its card appears, and writes down what
 * came back. That is the whole app.
 *
 * ## Why this is worth an app rather than a branch of SunGlance
 *
 * The result decides whether a much larger piece of work is possible at all --
 * reading the button GPIO directly from a glance service, which is the only
 * remaining way to notice R1. If the kernel refuses this request, the GPIO work
 * is wasted before it starts, because there would be nothing to do with the
 * press once it was detected. Better to find that out from twenty lines than
 * from a driver.
 *
 * And it is worth keeping afterwards: it is the smallest thing that exercises
 * "service asks, kernel launches", which is a seam several future apps would
 * sit on.
 *
 * ## The three answers, and how to tell them apart
 *
 * The request can fail to send, be refused, or be granted -- see
 * `Probe::Phase`. They mean different things and the log distinguishes them.
 * A fourth outcome has no message at all: if the kernel will not load a GUI ELF
 * for an app of this type, `probe.txt` never appears, because the app never
 * starts. That is a result too, and the README says how to read it.
 *
 * ## What this file does *not* do
 *
 * It does not return from `run()` on `EVENT_GLANCE_STOP` once a launch has been
 * granted. Every other glance app should -- SunGlance does -- but here the
 * carousel handing the screen to a GUI is likely to stop the glance, and a
 * service that exits at that moment may take its own GUI down with it and turn
 * a success into a puzzle. So after a granted request this service stays in its
 * message loop, logging, until the kernel tells it to stop.
 *
 ******************************************************************************
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Glance/GlanceControl.hpp"

#include "ProbeLog.hpp"
#include "ProbePlan.hpp"

class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    ~Service() = default;

    void run();

private:
    /// Ask the kernel for the glance area and lay the two lines out in it.
    /// False means it offered less than two text controls, which is less than
    /// this app can report an answer with.
    bool glanceConfig();
    void glanceCreate();

    /// Send `RequestAppRunGui` and record which of the three answers came back.
    void fire();

    /// Copy the lines into the controls, but only the ones that differ --
    /// `setText()` invalidates whether or not the string changed, and the tick
    /// is a frame clock.
    void apply(const Probe::Lines &lines);
    /// Send the form if anything invalidated it.
    void push();

    /// A short name for a message type, for the log. Unknown types are logged
    /// as hex rather than dropped: an unexpected message arriving after the
    /// request is exactly the kind of thing this app is here to notice.
    static const char *label(uint32_t type);

    /// Two text controls. Every SDK glance example asks for three, so this is
    /// not a demanding ask -- but it is checked rather than assumed.
    static constexpr uint32_t kControlsNeeded = 2;

    SDK::Kernel &mKernel;
    Probe::Log   mLog;

    SDK::Glance::Form        mGlance;
    SDK::Glance::ControlText mTop;
    SDK::Glance::ControlText mBottom;

    Probe::Phase mPhase = Probe::Phase::Waiting;

    /// Kernel timestamp of the first tick of this viewing, and whether one has
    /// arrived. The dwell is measured from the first tick rather than from
    /// `EVENT_GLANCE_START`, because the start message carries no timestamp.
    uint32_t mStartMs   = 0;
    bool     mHaveStart = false;

    /// Ticks seen this viewing, logged at the end. A card that was never ticked
    /// looks identical to one that was, until you have this number.
    uint32_t mTicks = 0;

    bool mActive = false;

    /// Set once the kernel has granted a launch. Keeps `run()` in its loop
    /// through `EVENT_GLANCE_STOP` -- see the note at the top of this file.
    bool mLaunched = false;

    /// Whether the two controls exist yet.
    ///
    /// `Form::createText()` appends: it is a builder, not an accessor. Most
    /// glance services never notice, because they return from `run()` when the
    /// card scrolls away and meet `EVENT_GLANCE_START` once per process. This
    /// one deliberately survives a viewing, so it can meet a second start --
    /// and building the form again would send four controls, then six, until
    /// the kernel's limit was passed and the card broke on a watch that had
    /// been scrolled past often enough.
    bool mCreated = false;

    /// What the controls currently hold, because the SDK will not say.
    Probe::Lines mShown {};
    bool         mHaveShown = false;
};

#endif // SERVICE_HPP
