/**
 ******************************************************************************
 * @file    Service.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The glance: the only part of this app that touches the SDK.
 ******************************************************************************
 *
 * A service with no GUI, driven entirely by the glance carousel. Everything it
 * decides -- where it is, what the sun does there, which event comes next, what
 * words go on the screen -- lives in the headers next to this one and is tested
 * on a host. What is left here is the SDK: read the clock, read the file, ask
 * the kernel how big the glance area is, and send three strings when they
 * change.
 *
 * ## Why `Glance` and not an autostart `Utility`
 *
 * SleepLab is a `Utility` because it has to be awake all night whether anybody
 * looks at it or not; its glance is a side effect of a service that already
 * exists. This app is the opposite. Sunrise is arithmetic over a date and a
 * coordinate, both of which are just as available in three milliseconds as they
 * would have been if the app had been running since boot -- so there is nothing
 * to keep warm, and a `Glance`-type app is exactly right: the carousel starts
 * the service, ticks it while it is on screen, and `run()` returns on
 * `EVENT_GLANCE_STOP`.
 *
 * The consequence, and it is the reason to write it down: this app cannot have
 * a home widget. A widget is pushed by a service that is alive when nobody is
 * looking, which is the one thing this app type is not. If a morning "sun sets
 * at" widget is ever wanted, the app type has to change first.
 *
 * ## The screen
 *
 * Two rows -- an icon and a time each, the next event above the one after it --
 * and a caption under them. Five controls, which is more than any SDK glance
 * example asks for, so the count the kernel offers is checked and the icons are
 * dropped rather than the glance declined if it will not stretch to five. See
 * `Render.hpp` for what the rows say without them.
 *
 * ## The two things this file has to get right
 *
 * **Send when, and only when, something changed.** The tick is a frame clock --
 * the message says 60 Hz -- and every `setText()` invalidates the form whether
 * or not the string differs, so setting the text unconditionally would push a
 * full control array over IPC sixty times a second to redraw an unchanged
 * minute. The strings are therefore built into buffers, compared, and only then
 * set. `Form::isInvalid()` decides whether to send, and `setValid()` is called
 * after sending and nowhere else -- SleepLab's glance sent nothing at all for
 * weeks because that call sat at the end of the *build* instead.
 *
 * **Nothing here decides what it says.** If a caption ever needs changing it
 * changes in `Render.cpp`, where a host test can see it.
 *
 ******************************************************************************
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Glance/GlanceControl.hpp"

#include "HomeConfig.hpp"
#include "Render.hpp"
#include "Schedule.hpp"
#include "Solar.hpp"
#include "WallClock.hpp"

class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    ~Service() = default;

    void run();

private:
    /// Ask the kernel for the glance area. False means it offered less than
    /// this app can draw in, and the app leaves rather than drawing something
    /// clipped.
    bool glanceConfig();
    void glanceCreate();

    /// Position the controls for the screen that is about to be drawn. Called
    /// when the shape changes -- two rows to one message and back -- and not on
    /// every update, because moving a control invalidates it.
    void layout(bool rowsMode);

    /// Recompute what the screen should say, at most once a second.
    void update();
    /// Everything the renderer needs, gathered from the clock and the config.
    Sun::View compose(int64_t nowUtc);
    /// Copy the lines into the controls, but only the ones that differ.
    void apply(const Sun::Lines &lines);

    /// Write down what the kernel offered and what was drawn from it, so the
    /// next round of layout tuning is done against a measurement instead of
    /// against another guess. One small file, rewritten only when it changes.
    void noteGeometry();

    /// Show or hide a control, and invalidate it when that changed.
    ///
    /// `Control::setVisible()` does not invalidate -- it writes the flag and
    /// returns -- so a control that is hidden and nothing else would keep being
    /// drawn until something unrelated happened to dirty the form. Exactly the
    /// class of silent failure that killed SleepLab's glance.
    static void showControl(SDK::Glance::Control &control, bool shown, bool &state);
    /// Send the form if anything invalidated it.
    void push();

    /// Local clock reading of a UTC instant, via `localtime_r` rather than by
    /// adding an offset: two days a year the offset at the event differs from
    /// the offset now, and those are exactly the two days somebody checks.
    static Sun::Clock clockAt(int64_t utc);

    // -- Layout ---------------------------------------------------------------
    //
    // No position in this file is a constant. The first version hard-coded
    // bands copied from SleepLab -- an app with one line at font 30 and 36
    // pixels to put it in -- and stacking two of those plus a caption clipped
    // the bottom row on the watch. `Sun::layoutFor()` decides the arrangement,
    // the font and every box from the area the kernel reports; this file only
    // puts controls where it says.

    /// Two icons, two times and a caption.
    static constexpr uint32_t kControlsWanted = 5;
    /// Below this the glance is declined outright: two times and a caption is
    /// the least this app can say without lying about which event is which.
    static constexpr uint32_t kControlsNeeded = 3;

    SDK::Kernel              &mKernel;
    SDK::Glance::Form         mGlance;
    SDK::Glance::ControlImage mIconFirst;
    SDK::Glance::ControlText  mFirst;
    SDK::Glance::ControlImage mIconSecond;
    SDK::Glance::ControlText  mSecond;
    SDK::Glance::ControlText  mSub;

    /// Where everything goes, which way round, and at what size, for the panel
    /// this glance actually got.
    Sun::Layout mLayout;

    /// False when the kernel would not give this glance five controls, or when
    /// the rows came out too short to hold an icon. Either way the rows carry
    /// words instead of pictures.
    bool mWithIcons = false;

    /// What is currently on screen, because the SDK will not say: which shape
    /// the controls are laid out for, which icons they carry, and what is
    /// visible.
    bool           mRowsMode      = true;
    bool           mLaidOut       = false;
    Sun::EventKind mFirstIconKind   = Sun::EventKind::Rise;
    bool           mIconFirstShown  = true;
    bool           mIconSecondShown = true;
    bool           mSecondShown     = true;

    Sun::HomeConfig mHome;

    /// What each control currently holds, because the SDK will not say.
    Sun::Lines mShown;
    bool       mHaveShown = false;

    /// The local day `mToday` and `mTomorrow` were computed for, so the pair is
    /// recomputed once at midnight rather than once a second.
    int64_t  mScheduleDay = INT64_MIN;
    Sun::Day mToday;
    Sun::Day mTomorrow;

    /// Second the screen was last recomputed for. The tick is a frame clock;
    /// nothing on this screen changes faster than the wall clock does.
    int64_t mLastSecond = -1;

    bool mActive = false;
};

#endif // SERVICE_HPP
