#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>

#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/Image.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

/**
 * @class MainView
 * @brief The one screen: what to do, whether it is working, and whether the
 *        cable is safe to plug in.
 *
 * The dump runs unattended with the cable out, so this display is the only
 * window into it while it happens and the first thing seen on picking the watch
 * back up. It has to answer those three questions at a glance, and no state may
 * be mistakable for a crash or for a different state -- "finished" and "stalled
 * at 31/32" looking alike is the failure that matters here, because one of them
 * means plug in and the other means do not.
 *
 * ## Layout on a round panel
 *
 * The framebuffer is a 240x240 square but the panel is a circle, so text
 * anchored near the top loses characters off the left edge -- confirmed the hard
 * way on this hardware. Everything here sits inside a horizontal inset (see
 * kInsetX) and is grouped vertically about the middle, where the chord is
 * widest. Nothing is placed in a corner.
 *
 * The widgets are hand-written rather than Designer-placed, the same approach
 * MapManager's screen takes, because there is no Designer in this environment --
 * and because a wildcard buffer sized for the content is exactly what a
 * Designer-generated screen would not have given.
 *
 * ## Text
 *
 * Every line is a wildcard filled at runtime from the status snapshot. The
 * inherited text database is Chrono's (this app reuses its generated fonts and
 * text templates unchanged, since regenerating them needs the Designer), so the
 * TMP_* typography templates are what the TypedTexts refer to and no literal
 * string in the database is used. That is why the strings below are formatted
 * with snprintf into UnicodeChar buffers instead of being text-database entries.
 */
class MainView : public MainViewBase
{
public:
    MainView();
    virtual ~MainView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    virtual void handleTickEvent() override {}

    /// Take in a new snapshot from the service and repaint.
    void onStatusChanged(const Model::Status& status);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    /// Longest line any state formats, plus room.
    ///
    /// Every line is written to fit its box on one line at its font size --
    /// nothing here relies on wide-text wrapping, which was tried first and
    /// silently clipped mid-word instead (a 52-character hint rendered as
    /// "Unplug USB, pres"). Two short lines the code chooses beat one long line
    /// the layout engine breaks where it likes, especially on a panel whose
    /// usable width shrinks with every row further from the middle.
    static constexpr uint16_t kLineBufSize = 40;

    // Horizontal insets, widening towards the vertical centre because that is
    // how a circle works. The round bezel's usable half-width at a row `dy`
    // from centre is sqrt(120^2 - dy^2), so a row near the bottom has ~84px to
    // play with where the middle has 120. Each inset below is chosen to sit
    // inside the chord for its own row, not for the framebuffer.
    static constexpr int16_t kInsetX = 30;  ///< Title: row ~51, chord ~98.
    static constexpr int16_t kLineW  = 240 - 2 * kInsetX;

    static constexpr int16_t kDetailX = 26;  ///< Rows ~140/160, chord >=112.
    static constexpr int16_t kDetailW = 240 - 2 * kDetailX;

    static constexpr int16_t kHint1X = 34;   ///< Row ~186, chord ~100.
    static constexpr int16_t kHint1W = 240 - 2 * kHint1X;

    static constexpr int16_t kHint2X = 40;   ///< Row ~206, chord ~84. The tightest row.
    static constexpr int16_t kHint2W = 240 - 2 * kHint2X;

    /// Progress bar geometry. Narrower than the text lines: it sits at the
    /// vertical middle where there is most room, but a full-width bar reads as
    /// an edge-to-edge stripe on a circle rather than as a gauge.
    static constexpr int16_t kBarX      = 45;
    static constexpr int16_t kBarW      = 240 - 2 * kBarX;
    static constexpr int16_t kBarY      = 118;
    static constexpr int16_t kBarH      = 8;
    static constexpr int16_t kBarBorder = 1;

    /// Above this, an ETA extrapolated from the first slices of a pass is noise
    /// rather than an estimate, and is shown as "--". Generous: a 4 MB dump at
    /// the rate this storage sustains is a few minutes, so anything past an hour
    /// is a rate that has not settled yet.
    static constexpr uint32_t kImplausibleEtaSec = 60u * 60u;

    void refresh();

    /// Fills one text area's wildcard from a printf-style format. Every line on
    /// the screen goes through here, so truncation and invalidation are handled
    /// in one place rather than six.
    void setLine(touchgfx::TextAreaWithOneWildcard& area,
                 touchgfx::Unicode::UnicodeChar* buffer, const char* format, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 4, 5)))
#endif
        ;

    /// Draws the bar to `fraction` of its width, or hides it entirely.
    void setBar(bool visible, uint64_t done, uint64_t total);

    // -- Widgets --------------------------------------------------------------

    /// What app this is. Constant, and small: it is the least important thing on
    /// the screen once a dump is running, but it is what identifies the app to
    /// someone who has just picked the watch up.
    touchgfx::TextAreaWithOneWildcard mTitle;
    touchgfx::Unicode::UnicodeChar    mTitleBuf[kLineBufSize];

    /// The one thing to read from across a room: READY, 07/32, DONE, ERROR.
    ///
    /// Single words and short counts only. At Poppins SemiBold 35 the inset
    /// holds about eight characters, and "DONE 32/32" does not fit -- it
    /// rendered as "ONE 32/3", clipped at both edges. The count lives on the
    /// detail line below instead, which loses nothing: "DONE" over
    /// "32 / 32 chunks" is no less unmistakable than one long line would be.
    touchgfx::TextAreaWithOneWildcard mHeadline;
    touchgfx::Unicode::UnicodeChar    mHeadlineBuf[kLineBufSize];

    /// Bar track and fill. Two boxes rather than a canvas widget: a canvas
    /// needs a buffer and a painter to draw a rectangle that two boxes draw for
    /// free, and this one has no rounded ends to justify it.
    touchgfx::Box mBarTrack;
    touchgfx::Box mBarFill;

    /// Two lines of detail under the bar: how far, how fast, how much longer.
    touchgfx::TextAreaWithOneWildcard mDetail;
    touchgfx::Unicode::UnicodeChar    mDetailBuf[kLineBufSize];

    touchgfx::TextAreaWithOneWildcard mDetail2;
    touchgfx::Unicode::UnicodeChar    mDetail2Buf[kLineBufSize];

    /// The actionable part: what to do next, over two deliberately short lines
    /// rather than one wrapped one. Never both empty -- every state has
    /// something the user should do or know, including "wait".
    touchgfx::TextAreaWithOneWildcard mHint;
    touchgfx::Unicode::UnicodeChar    mHintBuf[kLineBufSize];

    touchgfx::TextAreaWithOneWildcard mHint2;
    touchgfx::Unicode::UnicodeChar    mHint2Buf[kLineBufSize];

    /// Shown only in Idle, next to the R1 button it names.
    touchgfx::Image mStartIcon;
};

#endif // MAINVIEW_HPP
