#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include "gui/main_screen/MainViewLayout.hpp"
#include <gui/main_screen/MainPresenter.hpp>
#include <gui/containers/SleepStrip.hpp>

#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

/**
 * @class MainView
 * @brief The report, and the history, on one screen.
 *
 * Two modes rather than two TouchGFX screens. A second screen means a second
 * Presenter and View pair registered in the Designer-generated
 * `FrontendApplication`, and every widget on this app's screens is hand-built
 * precisely to avoid editing generated artefacts -- the packed string table in
 * particular, where changing one string shifts the offsets of every string
 * after it. `R1` toggles; the rest of the layout is redrawn.
 *
 * The inherited widget tree (MainViewBase) was Designer-generated for a
 * stopwatch face and reached here through MapManager's fork of it. Its
 * wildcard buffers are hardcoded far too small for this content, so the
 * inherited widgets are hidden in `setupScreen()` and this class adds its own.
 * A supported pattern, just not the Designer path.
 *
 * ---------------------------------------------------------------------------
 * What this screen must never do
 *
 * - **Show a sleep number for a night that failed the worn gate.** The service
 *   sends `hasSleep = false` and every metric absent; this draws the reason
 *   instead. A watch on a nightstand scores a flawless night and it must not
 *   be shown as one.
 * - **Bury an interruption.** If the night was interrupted, that is the *first
 *   line*, not a badge in a corner. A night with a hole in it that looks like a
 *   whole night is the second-worst thing this app could produce.
 * - **Call the strip a hypnogram.** The caption below it says what it is, and
 *   it is not optional decoration.
 * - **Do wall-clock arithmetic.** The service holds both clocks and sends
 *   times of day already resolved. A GUI that computed its own would be the
 *   second opinion in a system whose whole discipline is having one.
 */
class MainView : public MainViewBase
{
public:
    MainView() = default;
    virtual ~MainView() {}

    virtual void setupScreen() override;
    virtual void tearDownScreen() override;
    virtual void handleTickEvent() override {}

    /// The inherited stopwatch list is hidden and never populated.
    virtual void lapListUpdateItem(LapListItem &item, int16_t itemIndex) override
    {
        (void)item;
        (void)itemIndex;
    }

    void onReportChanged(const Model::Report &r);
    void onHistoryChanged(const Model::History &h);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    enum class Mode : uint8_t { Report = 0, History = 1 };

    static constexpr uint16_t kLineBufSize = 44;
    /// Lines on the report. Six is what fits between the round panel's chords
    /// once the strip has its band.
    static constexpr int      kReportLines = 6;
    /// Rows the history shows at once.
    static constexpr int      kHistoryRows = 5;
    static constexpr int      kMaxLines    = MainViewLayout::kMaxLines;

    // Layout lives in MainViewLayout.hpp, which has no TouchGFX dependency so
    // the host tests can assert every rectangle fits the round glass.
    static constexpr int16_t kLineHeight = MainViewLayout::kLineHeight;
    static constexpr int16_t kFirstLineY = MainViewLayout::kFirstLineY;
    static constexpr int16_t kStripY     = MainViewLayout::kStripY;
    static constexpr int16_t kCaptionY   = MainViewLayout::kCaptionY;

    void refresh();
    void drawReport();
    void drawHistory();

    /// Set line @p i, or blank it. Bounds-checked, because every caller
    /// computes its index and one of them will get it wrong.
    void setLine(int i, const char *text);

    /// Move the history focus, clamped. No wrap: on a short list, jumping from
    /// the last row to the first reads as a glitch rather than as a feature.
    void moveFocus(int16_t delta);

    Mode mMode = Mode::Report;

    touchgfx::TextAreaWithOneWildcard mLine[kMaxLines];
    touchgfx::Unicode::UnicodeChar    mBuf[kMaxLines][kLineBufSize];

    SleepStrip                        mStrip;
    touchgfx::TextAreaWithOneWildcard mCaption;
    touchgfx::Unicode::UnicodeChar    mCaptionBuf[kLineBufSize];

    int16_t mFocus = 0;
};

#endif // MAINVIEW_HPP
