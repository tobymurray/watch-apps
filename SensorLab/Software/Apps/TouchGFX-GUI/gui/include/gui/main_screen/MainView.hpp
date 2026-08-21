#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>

#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

/**
 * @class MainView
 * @brief The roster: all thirty-seven sensor types, scrollable, at a glance.
 *
 * ---------------------------------------------------------------------------
 * Why this screen matters more than it looks
 *
 * The Sleep Probe's post-mortem is right that its unique value was *a screen you
 * read before bed rather than a log you read after*. Two minutes of hardware
 * time and a thirteen-character block on a 240x240 panel settled four ledger
 * rows -- including `TOUCH_DETECT` being an event sensor (row S12), which would
 * otherwise have suppressed every night that app ever recorded.
 *
 * This is that screen for all thirty-seven types. It supersedes the probe's log
 * and keeps its screen, which is exactly what the post-mortem asked for.
 *
 * ---------------------------------------------------------------------------
 * The distinction the whole screen is built around
 *
 * **Resolved-driver versus emitting-driver, visible without reading.** Absent,
 * silent and stuck are three different findings with three different causes, and
 * the probe's case-encoding -- upper case resolved, lower case asked-for and
 * refused -- is what made two of the ledger's most consequential rows visible in
 * two minutes. Here each row carries an explicit marker rather than case, because
 * a roster row has space for one and a thirteen-character block did not:
 *
 *     .  the run did not ask for this type
 *     -  asked for, and RequestDefault resolved nothing   (no producer)
 *     o  resolved a driver, and nothing has arrived        (silent)
 *     *  resolved and delivering
 *     !  delivering a frame that does not match its parser
 *     ?  no burst has arrived for this row yet
 *
 * The last one is not padding. A row that has not arrived is drawn as unknown
 * rather than as a zeroed measurement, because zeroed memory reads as "resolves
 * nothing, delivers nothing" -- which is a finding, and inventing one would be
 * the worst thing this screen could do. SleepLab's ledger row A11 is what
 * happens when a widget draws a claim out of zeroed memory.
 *
 * ---------------------------------------------------------------------------
 * Completeness is on the screen, always
 *
 * The header line carries the run phase and the overall completeness fraction,
 * on every frame, in both the roster and the summary view. **No screen shows
 * findings without showing how much is missing.** A roster that looked
 * authoritative after a three-minute existence sweep would be the single most
 * misleading thing this app could produce.
 *
 * ---------------------------------------------------------------------------
 * On the inherited widget tree
 *
 * `MainViewBase` was Designer-generated for a stopwatch face and reached here
 * through the Sleep Probe's fork of it. Its wildcard buffers are hardcoded far
 * too small for this content and there is no Designer in this environment to
 * resize them, so the inherited widgets are hidden in `setupScreen()` and this
 * class adds its own -- a supported pattern, just not the Designer path. Editing
 * Designer's packed string table by hand to relabel them would risk the offsets
 * of every string packed after it.
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

    /// A fresh snapshot arrived from the service.
    void onStateChanged(const Model::State &state);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    /// Two views, one screen. The roster is the instrument; the summary is what
    /// you read before starting a twelve-hour run.
    enum class Page : uint8_t { Roster = 0, Summary };

    /// Widest line is a roster row, which is deliberately terse for exactly
    /// this reason.
    static constexpr uint16_t kLineBufSize = 40;

    /// Nine lines is what fits between a 240x240 round panel's chords at this
    /// size without either end clipping. One is the header, so eight sensor
    /// rows are visible at a time out of thirty-seven.
    static constexpr int     kLines       = 9;
    static constexpr int     kRosterRows  = kLines - 1;
    static constexpr int16_t kLineHeight  = 20;
    static constexpr int16_t kFirstLineY  = 34;

    void refresh();
    void drawRoster(char text[kLines][kLineBufSize]);
    void drawSummary(char text[kLines][kLineBufSize]);

    /// The one-character state marker for a roster row. See the class comment;
    /// this is the function the whole screen exists to compute.
    char markerFor(size_t typeIdx) const;

    /// A short name for a sensor type, trimmed to fit. The generated table's
    /// enumerator names run to twenty-four characters and the row has room for
    /// about twelve, so the trim is towards the *end* -- `ACTIVITY_TIME_DAI`
    /// still distinguishes it from `ACTIVITY_TIME`, whereas trimming the front
    /// would not.
    static void shortName(char *out, size_t outSize, size_t typeIdx);

    Page     mPage      = Page::Roster;
    /// First roster row on screen. Scrolled with L1/L2.
    uint8_t  mScroll    = 0;

    touchgfx::TextAreaWithOneWildcard mLine[kLines];
    touchgfx::Unicode::UnicodeChar    mBuf[kLines][kLineBufSize];
};

#endif // MAINVIEW_HPP
