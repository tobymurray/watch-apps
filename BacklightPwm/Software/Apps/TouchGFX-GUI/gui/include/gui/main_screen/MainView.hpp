#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>

#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/Image.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

/**
 * @class MainView
 * @brief The screen, which is also the photometric target.
 *
 * The panel is a reflective memory LCD with no integrated backlight: the light
 * is a discrete front-light shining onto it. What a camera or a meter sees is the
 * front-light's output multiplied by whatever the framebuffer reflects, so
 * metering a mostly-black screen measures the light through a near-zero
 * coefficient and would make six real duty cycles look like one brightness.
 *
 * So while the ladder runs, the screen is a full white field with black text: the
 * maximum-reflectance target, identical to the one `BacklightProbe` uses, so the
 * two apps' photographs can be compared directly. That comparison is the entire
 * result, and it only holds if the target is the same.
 *
 * Unlike `BacklightProbe` this screen never repaints on a tick. That app had to
 * move a counter because it was timing an event it could not observe; this one
 * is driving the light itself, so there is nothing to time and no reason to send
 * the kernel a display update in the middle of a measurement.
 *
 * The widgets are hand-written rather than Designer-placed, as FwDump's and
 * MapManager's are, because there is no Designer in this environment. Everything
 * sits inside a horizontal inset and is grouped about the middle, where the round
 * panel is widest; nothing is placed in a corner.
 */
class MainView : public MainViewBase
{
public:
    MainView();
    virtual ~MainView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    /// Deliberately does nothing. See the class comment.
    virtual void handleTickEvent() override;

    /// Take in a new snapshot from the service and repaint.
    void onStatusChanged(const Model::Status& status);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    /// Longest line any state formats, plus room.
    static constexpr uint16_t kLineBufSize = 40;

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

    /// Progress bar geometry. It shows position through the ladder, not through a
    /// rung: a bar that reset on every rung would be motion without information.
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
    /// sits behind the text. White only while the pin is actually being driven,
    /// so a refused run cannot be mistaken at a glance for a real one.
    touchgfx::Box mField;

    touchgfx::TextAreaWithOneWildcard mTitle;
    touchgfx::Unicode::UnicodeChar    mTitleBuf[kLineBufSize];

    /// The one thing to read from across a room: the duty cycle being requested,
    /// which is the number to photograph next to the light.
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

};

#endif // MAINVIEW_HPP
