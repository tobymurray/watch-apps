#include <gui/main_screen/MainView.hpp>

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include "SDK/GUI/Button.hpp"

#include <cstdio>
#include <cstring>

MainView::MainView()
    : mUpdateRowCallback(this, &MainView::updateRowCallbackHandler)
{
}

void MainView::setupScreen()
{
    MainViewBase::setupScreen();

    // Hide the inherited stopwatch-face widgets rather than repurpose them --
    // their wildcard buffers are hardcoded far too small for this content
    // (see the class doc comment), and hiding avoids touching Designer's
    // generated packed string table (title's "CHRONO" text) to change it,
    // which risks corrupting the offsets of every string packed after it.
    line.setVisible(false);
    scrollIndicator.setVisible(false);
    buttons.setVisible(false);
    title.setVisible(false);
    timeMainText.setVisible(false);
    timeFracText.setVisible(false);
    actionL2Icon.setVisible(false);
    actionR2Icon.setVisible(false);
    actionPlayR1Icon.setVisible(false);
    actionPauseR1Icon.setVisible(false);
    lapList.setVisible(false);

    // Summary sits above the list, clear of the round bezel's narrowest chord
    // near the top (the same lesson AthensRun's status label learned the hard
    // way: a 240x240 round panel's usable width shrinks well before its edges).
    mSummary.setPosition(40, 26, 160, 22);
    mSummary.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16));
    mSummary.setWideTextAction(touchgfx::WIDE_TEXT_CHARWRAP_DOUBLE_ELLIPSIS);
    mSummary.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    mSummaryBuf[0] = 0;
    mSummary.setWildcard(mSummaryBuf);
    add(mSummary);

    // The list: five rows of PackListItem::kHeight, centred on the panel so
    // that its first and last rows are equally far from the bezel and both
    // still have the width for a full row.
    mPackList.setPosition((240 - PackListItem::kWidth) / 2, 50, PackListItem::kWidth,
                          kVisibleRows * PackListItem::kHeight);
    mPackList.setHorizontal(false);
    mPackList.setCircular(false);
    mPackList.setEasingEquation(touchgfx::EasingEquations::linearEaseOut);
    mPackList.setAnimationSteps(kScrollSteps);
    mPackList.setDrawableSize(PackListItem::kHeight, 0);

    // Put the selected row in the middle: two rows above it, two below.
    mPackList.setSelectedItemOffset(2 * PackListItem::kHeight);

    // Buttons drive this, not fingers. Leaving swipe/drag enabled would let a
    // stray touch move the selection somewhere the summary line then
    // describes, with no key press to explain why it changed.
    mPackList.setSwipeAcceleration(0);
    mPackList.setDragAcceleration(0);

    mPackList.setDrawables(mRowDrawables_, mUpdateRowCallback);
    mPackList.setNumberOfItems(0);
    add(mPackList);

    // Occupies the list's space when there is nothing to list.
    mEmpty.setPosition(30, 108, 180, 44);
    mEmpty.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16));
    mEmpty.setWideTextAction(touchgfx::WIDE_TEXT_WORDWRAP_ELLIPSIS);
    mEmpty.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    mEmptyBuf[0] = 0;
    mEmpty.setWildcard(mEmptyBuf);
    add(mEmpty);

    refreshList();
    refreshSummary();
}

void MainView::tearDownScreen()
{
    MainViewBase::tearDownScreen();
}

void MainView::onProgressChanged(const Model::Progress &)
{
    // Only the summary depends on byte progress; the list rows carry verdicts,
    // which arrive on the roster message instead. Repainting the list on every
    // progress tick would redraw five rows a second to no effect.
    refreshSummary();
}

void MainView::onRosterChanged(const Model::Roster &)
{
    refreshList();
    refreshSummary();
}

void MainView::refreshList()
{
    const Model::Roster &roster = presenter->roster();
    const int16_t count = static_cast<int16_t>(roster.count);

    // Keep the focus on a row that still exists: packs can be dropped while
    // the screen is open.
    if (mFocus >= count) {
        mFocus = count > 0 ? static_cast<int16_t>(count - 1) : 0;
    }

    mPackList.setNumberOfItems(count);
    mPackList.animateToItem(mFocus, 0);
    mPackList.setVisible(count > 0);

    // itemChanged(), not invalidate(). A plain invalidate redraws the
    // drawables exactly as they already are: the list only re-runs its update
    // callback when items are re-bound, which setNumberOfItems does only when
    // the count actually changes. Verdicts change far more often than the
    // number of packs does, so without this the rows keep the text they were
    // first given and sit reading "wait" long after everything has finished.
    for (int16_t i = 0; i < count; ++i) {
        mPackList.itemChanged(i);
    }
    mPackList.invalidate();

    // An empty list has two quite different causes and they must not look
    // alike: nothing has been heard from the service yet, or it has spoken and
    // there genuinely are no packs. Telling someone to go and copy a pack in
    // when the answer simply has not arrived would send them off to fix
    // something that was never broken.
    if (count == 0) {
        const char *why = roster.everReceived ? "drop a pack in SharedData/maps"
                                              : "starting...";
        touchgfx::Unicode::strncpy(mEmptyBuf, why, kLineBufSize - 1);
        mEmptyBuf[kLineBufSize - 1] = 0;
        mEmpty.setWildcard(mEmptyBuf);
    }
    mEmpty.setVisible(count == 0);
    mEmpty.invalidate();
}

void MainView::refreshSummary()
{
    const Model::Progress &progress = presenter->progress();
    const Model::Roster   &roster   = presenter->roster();

    char text[kLineBufSize];

    if (!progress.everReceived) {
        std::snprintf(text, sizeof(text), "starting...");
    } else if (progress.packsTotal == 0) {
        std::snprintf(text, sizeof(text), "no packs found");
    } else {
        // While the focused row is the one being scanned, the summary carries
        // that scan's detail -- percent and ETA. Otherwise it reports the
        // whole job, which is what someone glancing at the screen wants.
        const bool focusIsScanning =
            progress.anyInProgress && mFocus < static_cast<int16_t>(roster.count)
            && roster.rows[mFocus].state
                   == static_cast<uint8_t>(CustomMessage::PackState::Scanning);

        if (focusIsScanning && progress.bytesTotal > 0 && progress.bytesDone > 0) {
            const unsigned percent =
                static_cast<unsigned>((progress.bytesDone * 100ull) / progress.bytesTotal);

            // ETA from the actually-observed rate for this pass, not a
            // hardcoded assumption -- throughput varies by device/storage, so
            // this only means anything once real bytes have actually moved.
            const uint64_t remainingBytes = progress.bytesTotal - progress.bytesDone;
            const uint64_t remainingMs = (remainingBytes * progress.elapsedMs) / progress.bytesDone;
            const uint32_t remainingSec = static_cast<uint32_t>(remainingMs / 1000);

            // The first sample or two of a pass are measured over a handful of
            // bytes and extrapolate to nonsense. Show that the estimate is not
            // ready yet rather than a confident "ETA 833m20s".
            if (remainingSec >= kImplausibleEtaSec) {
                std::snprintf(text, sizeof(text), "%u%%  ETA --", percent);
            } else {
                // Cast: uint32_t is long unsigned on this target, so %u would
                // be a format mismatch without it.
                std::snprintf(text, sizeof(text), "%u%%  ETA %um%02us", percent,
                              static_cast<unsigned>(remainingSec / 60),
                              static_cast<unsigned>(remainingSec % 60));
            }
        } else if (progress.packsVerified == progress.packsTotal) {
            std::snprintf(text, sizeof(text), "all %u verified", progress.packsTotal);
        } else {
            // Not everything passed and nothing is running: say how many are
            // outstanding rather than "all verified", which is exactly the
            // wrong thing to tell someone whose map is missing.
            std::snprintf(text, sizeof(text), "%u of %u verified", progress.packsVerified,
                          progress.packsTotal);
        }
    }

    touchgfx::Unicode::strncpy(mSummaryBuf, text, kLineBufSize - 1);
    mSummaryBuf[kLineBufSize - 1] = 0;
    mSummary.setWildcard(mSummaryBuf);
    mSummary.invalidate();
}

void MainView::packListUpdateItem(PackListItem &item, int16_t itemIndex)
{
    const Model::Roster &roster = presenter->roster();

    if (itemIndex < 0 || itemIndex >= static_cast<int16_t>(roster.count)) {
        item.clear();
        return;
    }

    // Drop the extension for display. Every tracked pack ends in the same nine
    // characters, so showing them spends a third of the column on a word that
    // distinguishes nothing -- and the column is exactly where two packs named
    // alike have to be told apart. The roster carries the real filename; only
    // the drawing is shortened.
    char shown[CustomMessage::kMaxRowNameLen];
    std::snprintf(shown, sizeof(shown), "%s", roster.rows[itemIndex].name);

    static const char kExt[] = ".rawtiles";
    const size_t shownLen = std::strlen(shown);
    const size_t extLen   = sizeof(kExt) - 1;
    if (shownLen > extLen && std::strcmp(shown + (shownLen - extLen), kExt) == 0) {
        shown[shownLen - extLen] = '\0';
    }

    item.setPack(shown, roster.rows[itemIndex].state, itemIndex == mFocus);
}

void MainView::updateRowCallbackHandler(touchgfx::DrawableListItemsInterface *items,
                                        int16_t containerIndex, int16_t itemIndex)
{
    if (items == &mRowDrawables_) {
        packListUpdateItem(mRowDrawables_[containerIndex], itemIndex);
    }
}

void MainView::moveFocus(int16_t delta)
{
    const int16_t count = static_cast<int16_t>(presenter->roster().count);
    if (count <= 0) {
        return;
    }

    int16_t next = static_cast<int16_t>(mFocus + delta);
    if (next < 0) {
        next = 0;
    } else if (next >= count) {
        next = static_cast<int16_t>(count - 1);
    }

    const int16_t previous = mFocus;
    if (next == previous) {
        return;
    }
    mFocus = next;

    mPackList.animateToItem(mFocus, kScrollSteps);

    // The two rows either side of the move changed appearance, not just
    // position: one brightens and the other dims. That is a content change, so
    // it needs the update callback re-run rather than a repaint of what the
    // drawables already hold.
    mPackList.itemChanged(previous);
    mPackList.itemChanged(mFocus);
    mPackList.invalidate();

    // The summary describes the focused pack when that pack is the one being
    // scanned, so moving the focus can change it.
    refreshSummary();
}

void MainView::handleKeyEvent(uint8_t key)
{
    namespace Btn = SDK::GUI::Button;

    switch (key) {
        case Btn::L1:
            moveFocus(-1);
            break;

        case Btn::L2:
            moveFocus(1);
            break;

        // Nothing else to command the service to do -- verification is
        // autonomous. The only other interaction this screen offers is
        // leaving it.
        case Btn::R2:
            presenter->exit();
            break;

        default:
            break;
    }
}
