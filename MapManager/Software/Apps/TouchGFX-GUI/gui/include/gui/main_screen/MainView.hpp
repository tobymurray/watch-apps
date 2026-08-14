#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>
#include <gui/containers/PackListItem.hpp>

#include <touchgfx/containers/scrollers/DrawableList.hpp>
#include <touchgfx/containers/scrollers/ScrollWheel.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

/**
 * @class MainView
 * @brief The single screen: a scrolling list of packs and their verdicts,
 *        under one summary line.
 *
 * The inherited widget tree (MainViewBase) was Designer-generated for a
 * stopwatch face -- its wildcard buffers are hardcoded far too small for this
 * content (12 and 4 UnicodeChars) and there's no Designer available in this
 * environment to resize them. Rather than fight that, the inherited widgets
 * are hidden in setupScreen() and this class adds its own directly (same
 * hand-written-widget pattern as AthensRun's TrackFaceMap, in the SDK this
 * app's verification logic was first built for) -- fully supported, just not
 * the Designer path.
 *
 * The list is a ScrollWheel rather than the inherited ScrollList because this
 * screen has a genuine selection: one row is focused, and it is the row whose
 * detail the summary line describes. A wheel snaps to a selected item and
 * tracks its index itself, which is exactly the bookkeeping a list would have
 * left to be done by hand.
 */
class MainView : public MainViewBase
{
public:
    MainView();
    virtual ~MainView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    virtual void handleTickEvent() override {}

    /// The inherited stopwatch list is hidden and never populated.
    virtual void lapListUpdateItem(LapListItem &item, int16_t itemIndex) override
    {
        (void)item;
        (void)itemIndex;
    }

    /**
     * @brief Take in a new snapshot from the service.
     */
    void onProgressChanged(const Model::Progress &progress);

    /**
     * @brief Take in a new roster from the service.
     */
    void onRosterChanged(const Model::Roster &roster);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    static constexpr uint16_t kLineBufSize = 40;

    /// How many rows the list shows at once. Five is what the round panel
    /// allows at PackListItem::kHeight -- see the note there -- and it gives
    /// the shape asked for: one focused, two above, two below.
    static constexpr int16_t kVisibleRows = 5;

    /// Drawables bound to the list: one more than can be shown, so a spare is
    /// always available for the row scrolling into view.
    static constexpr int16_t kRowDrawables = kVisibleRows + 1;

    /// Frames one row of scrolling takes. Short: this is driven by discrete
    /// button presses, so the animation is feedback that the press landed,
    /// not a gesture being followed.
    static constexpr int16_t kScrollSteps = 6;

    /// Above this, an ETA extrapolated from the first few bytes of a pass is
    /// noise rather than an estimate, and is shown as "--" instead. Well
    /// clear of a real worst case: the largest pack this has been run against
    /// (~200MB) verifies in about a minute.
    static constexpr uint32_t kImplausibleEtaSec = 100u * 60u;

    void refreshSummary();
    void refreshList();

    /// Move the focused row, clamped to the ends. No wrap-around: on a list
    /// this short, jumping from the last row to the first reads as a glitch
    /// rather than as a feature.
    void moveFocus(int16_t delta);

    void packListUpdateItem(PackListItem &item, int16_t itemIndex);
    void updateRowCallbackHandler(touchgfx::DrawableListItemsInterface *items,
                                  int16_t containerIndex, int16_t itemIndex);

    touchgfx::ScrollWheel                       mPackList;
    touchgfx::DrawableListItems<PackListItem, kRowDrawables> mRowDrawables_;
    touchgfx::Callback<MainView, touchgfx::DrawableListItemsInterface *, int16_t, int16_t>
        mUpdateRowCallback;

    /// One line above the list: what the whole app is doing, or the detail of
    /// the focused pack when there is any to give.
    touchgfx::TextAreaWithOneWildcard mSummary;
    touchgfx::Unicode::UnicodeChar    mSummaryBuf[kLineBufSize];

    /// Shown instead of the list when there is nothing to list.
    touchgfx::TextAreaWithOneWildcard mEmpty;
    touchgfx::Unicode::UnicodeChar    mEmptyBuf[kLineBufSize];

    int16_t mFocus = 0; ///< Index of the focused row.
};

#endif // MAINVIEW_HPP
