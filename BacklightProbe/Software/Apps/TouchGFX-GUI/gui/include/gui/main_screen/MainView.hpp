#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>

#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/Image.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

/**
 * @class MainView
 * @brief The screen, which here is also an instrument.
 *
 * Most app screens report. This one participates in the measurement, in two
 * ways that are the whole reason it is not just a progress display.
 *
 * ## 1. It is the photometric target
 *
 * The panel is a reflective memory LCD with no integrated backlight: the
 * "backlight" is a discrete front-light shining onto it. What a camera or a
 * light meter sees is therefore the front-light's output multiplied by whatever
 * the framebuffer is reflecting. Metering a mostly-black screen measures the
 * front-light through a near-zero coefficient and would make a real brightness
 * difference look like no difference at all.
 *
 * So while the plan runs, the screen is a **full white field** (see mField) with
 * black text on it: the maximum-reflectance target, held constant across every
 * rung of the ladder so the only thing varying between two photographs is the
 * light. Idle and Done go back to the normal dark scheme; they are not being
 * measured.
 *
 * ## 2. It is the clock the video is read against
 *
 * No app can read the backlight's state back: `IBacklight` is not obtainable
 * through `queryInterface`, and `REQUEST_BACKLIGHT_SET` returns no state. So
 * every auto-off timing in Suite 2 has to be read off a video of the watch, and
 * the only way to know *when* in that video the light died is for the watch to
 * be displaying the time itself.
 *
 * Hence the counter: during an OBSERVE step the headline is milliseconds since
 * the request, updated every frame. Point a phone at it, and the frame where the
 * screen dims carries its own timestamp.
 *
 * ## Why the screen sometimes refuses to repaint
 *
 * A repaint is a `REQUEST_DISPLAY_UPDATE` to the same kernel that owns the
 * backlight. A kernel that counts display activity as user activity would extend
 * the very auto-off timer being measured. That is a plausible confound and it is
 * not one this app can rule out, so it does the next best thing: it holds still
 * during the steps where it can (HOLD, i.e. all of Suite 1's metering) and moves
 * only during the steps where a moving clock is the point (OBSERVE).
 *
 * `Model::Status::quiet` carries that decision from the plan, and this class
 * obeys it rather than deciding for itself. The results file records which mode
 * each step ran in, so a reader can see the confound where it applies.
 *
 * ## Layout on a round panel
 *
 * The framebuffer is a 240x240 square and the panel is a circle, so text
 * anchored near an edge loses characters: confirmed the hard way on this
 * hardware. Everything sits inside a horizontal inset and is grouped about the
 * middle, where the chord is widest. Nothing is placed in a corner. The widgets
 * are hand-written rather than Designer-placed, as FwDump's and MapManager's
 * are, because there is no Designer in this environment.
 */
class MainView : public MainViewBase
{
public:
    MainView();
    virtual ~MainView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    /// Repaints the counter while an OBSERVE step runs, and does nothing at all
    /// otherwise. See the class comment for why "nothing at all" is deliberate.
    virtual void handleTickEvent() override;

    /// Take in a new snapshot from the service and repaint.
    void onStatusChanged(const Model::Status& status);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    /// Longest line any state formats, plus room. Larger than FwDump's because
    /// a step label arrives from the plan and can be most of kLabelMax.
    static constexpr uint16_t kLineBufSize = 56;

    // Horizontal insets, widening towards the vertical centre because that is
    // how a circle works: the usable half-width at a row `dy` from centre is
    // sqrt(120^2 - dy^2). Each inset is chosen for its own row, not for the
    // framebuffer.
    static constexpr int16_t kInsetX = 30;   ///< Title row.
    static constexpr int16_t kLineW  = 240 - 2 * kInsetX;

    static constexpr int16_t kDetailX = 26;
    static constexpr int16_t kDetailW = 240 - 2 * kDetailX;

    static constexpr int16_t kHint1X = 34;
    static constexpr int16_t kHint1W = 240 - 2 * kHint1X;

    static constexpr int16_t kHint2X = 40;   ///< The tightest row.
    static constexpr int16_t kHint2W = 240 - 2 * kHint2X;

    /// Progress bar geometry. It shows position through the plan, not through a
    /// step: a bar that reset on every step would be motion without information.
    static constexpr int16_t kBarX      = 45;
    static constexpr int16_t kBarW      = 240 - 2 * kBarX;
    static constexpr int16_t kBarY      = 118;
    static constexpr int16_t kBarH      = 8;
    static constexpr int16_t kBarBorder = 1;

    void refresh();

    /// Fills one text area's wildcard from a printf-style format. Everything on
    /// the screen goes through here, so truncation and invalidation are handled
    /// once.
    void setLine(touchgfx::TextAreaWithOneWildcard& area,
                 touchgfx::Unicode::UnicodeChar* buffer, const char* format, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 4, 5)))
#endif
        ;

    void setBar(bool visible, uint32_t done, uint32_t total);

    /// Switches the whole screen between the dark scheme and the white
    /// measuring field, recolouring every text widget to match. Called only when
    /// the mode actually changes, since it repaints everything.
    void setMeasuringField(bool measuring);

    // Widgets --------------------------------------------------------------

    /// The measuring field: a full-screen Box, added before anything else so it
    /// sits behind the text. White while the plan runs, black otherwise. See the
    /// class comment: this widget is the reason a brightness photograph means
    /// anything.
    touchgfx::Box mField;

    touchgfx::TextAreaWithOneWildcard mTitle;
    touchgfx::Unicode::UnicodeChar    mTitleBuf[kLineBufSize];

    /// The one thing to read from across a room, and during OBSERVE the thing a
    /// video is read against: milliseconds since the request.
    touchgfx::TextAreaWithOneWildcard mHeadline;
    touchgfx::Unicode::UnicodeChar    mHeadlineBuf[kLineBufSize];

    touchgfx::Box mBarTrack;
    touchgfx::Box mBarFill;

    touchgfx::TextAreaWithOneWildcard mDetail;
    touchgfx::Unicode::UnicodeChar    mDetailBuf[kLineBufSize];

    touchgfx::TextAreaWithOneWildcard mDetail2;
    touchgfx::Unicode::UnicodeChar    mDetail2Buf[kLineBufSize];

    touchgfx::TextAreaWithOneWildcard mHint;
    touchgfx::Unicode::UnicodeChar    mHintBuf[kLineBufSize];

    touchgfx::TextAreaWithOneWildcard mHint2;
    touchgfx::Unicode::UnicodeChar    mHint2Buf[kLineBufSize];

    /// Shown only when a press would do something.
    touchgfx::Image mStartIcon;

    /// Which scheme is currently painted, so setMeasuringField only repaints on
    /// an actual change.
    bool mMeasuring = false;

    /// Whether the last snapshot said to hold still. The tick handler reads this
    /// rather than the presenter, so a tick that lands between snapshots cannot
    /// repaint during a quiet step.
    bool mQuiet = true;

    /// Last counter value painted, in whole hundredths of a second. The counter
    /// is only invalidated when this changes, so a frame rate faster than the
    /// digits are readable does not turn into display updates nobody can see --
    /// which matters here, since every one of those is a message to the kernel
    /// that owns the light.
    uint32_t mLastCounterCentis = 0xFFFFFFFFu;
};

#endif // MAINVIEW_HPP
