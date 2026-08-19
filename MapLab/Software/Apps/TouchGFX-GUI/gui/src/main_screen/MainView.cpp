#include <gui/main_screen/MainView.hpp>

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include "SDK/GUI/Button.hpp"

#include "Cards.hpp"

#include <cstdio>

using namespace MapLab;

void MainView::setupScreen()
{
    MainViewBase::setupScreen();

    // Hidden rather than relabelled: the inherited widgets come from a
    // Designer-generated stopwatch face, and editing its packed string table
    // by hand risks the offsets of every string packed after it.
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

    mCanvasView.setPosition(0, 0, 240, 240);
    mCanvasView.setKernel(&presenter->kernel());
    mCanvasView.setVisible(false);
    add(mCanvasView);

    for (int i = 0; i < kLines; i++) {
        // 24 px inset: a round panel's usable chord is narrower than its
        // bounding box, and the first and last lines are the ones that clip.
        mLine[i].setPosition(24, static_cast<int16_t>(kFirstLineY + i * kLineHeight),
                             192, kLineHeight);
        mLine[i].setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16_L));
        mLine[i].setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
        mLine[i].setWideTextAction(touchgfx::WIDE_TEXT_CHARWRAP_DOUBLE_ELLIPSIS);
        mBuf[i][0] = 0;
        mLine[i].setWildcard(mBuf[i]);
        add(mLine[i]);
    }

    refresh();
}

void MainView::tearDownScreen()
{
    MainViewBase::tearDownScreen();
}

void MainView::onStatusChanged(const Model::Status &)
{
    refresh();
}

void MainView::handleTickEvent()
{
    int            index   = -1;
    uint32_t       repeats = 0;
    const uint8_t *source  = nullptr;
    int16_t        w       = 0;
    int16_t        h       = 0;
    bool           mosaic  = false;

    if (!mBlitRunning) {
        if (presenter->blitPending(index, repeats, source, w, h, mosaic)) {
            // The canvas widget has to be visible for TouchGFX to draw it, and
            // the text lines have to be out of the way: a blit measured with
            // six text areas repainting over it would be measuring the screen,
            // not the blit.
            for (int i = 0; i < kLines; ++i) {
                mLine[i].setVisible(false);
            }
            mCanvasView.setVisible(true);
            mCanvasView.startBlitBench(source, w, h, repeats, mosaic);
            mBlitRunning = true;
            mBlitIndex   = index;
            invalidate();
        }
        return;
    }

    uint32_t iterations   = 0;
    uint32_t elapsedMs    = 0;
    int32_t  bytesPerBlit = 0;
    if (mCanvasView.takeResult(iterations, elapsedMs, bytesPerBlit)) {
        mBlitRunning = false;
        presenter->blitComplete(mBlitIndex, iterations, elapsedMs, bytesPerBlit);
        mBlitIndex = -1;
        refresh();
    }
}

void MainView::refreshMenu(char text[kLines][kLineBufSize], const Model::Status &s)
{
    static const char *kItems[] = { "run benches", "cards", "watchdog stair", "exit" };

    std::snprintf(text[0], kLineBufSize, "MAP LAB");
    for (int i = 0; i < 4; ++i) {
        std::snprintf(text[1 + i], kLineBufSize, "%s%s",
                      (s.menuIndex == i) ? "> " : "  ", kItems[i]);
    }
    if (s.coldTouchMs >= 0) {
        std::snprintf(text[5], kLineBufSize, "cold fs touch %ld ms",
                      static_cast<long>(s.coldTouchMs));
    } else {
        std::snprintf(text[5], kLineBufSize, "cold fs touch: not captured");
    }
}

void MainView::refreshRunning(char text[kLines][kLineBufSize], const Model::Status &s)
{
    if (s.complete) {
        std::snprintf(text[0], kLineBufSize, "DONE  %d benches", s.benchTotal);
    } else if (s.benchIndex < 0) {
        std::snprintf(text[0], kLineBufSize, "writing fixture...");
    } else {
        std::snprintf(text[0], kLineBufSize, "bench %d/%d", s.benchIndex + 1, s.benchTotal);
    }

    std::snprintf(text[1], kLineBufSize, "%s %s", s.lastId, s.lastName);

    if (!s.lastValid) {
        // "Too fast to time" and "measured as zero" are different findings and
        // must not print the same.
        std::snprintf(text[2], kLineBufSize, "unmeasured");
    } else if (s.lastUsPerOp >= 10000u) {
        std::snprintf(text[2], kLineBufSize, "%lu.%lu ms",
                      static_cast<unsigned long>(s.lastUsPerOp / 1000u),
                      static_cast<unsigned long>((s.lastUsPerOp % 1000u) / 100u));
    } else {
        std::snprintf(text[2], kLineBufSize, "%lu us",
                      static_cast<unsigned long>(s.lastUsPerOp));
    }

    if (s.lastNote[0] != '\0') {
        std::snprintf(text[3], kLineBufSize, "%s", s.lastNote);
    }

    if (s.logFailures > 0) {
        // A run whose rows are not reaching storage is worth interrupting for:
        // everything else on this screen is about to be lost.
        std::snprintf(text[4], kLineBufSize, "LOG FAILED %lu",
                      static_cast<unsigned long>(s.logFailures));
    } else {
        std::snprintf(text[4], kLineBufSize, "%lu rows  run %lu",
                      static_cast<unsigned long>(s.logRows),
                      static_cast<unsigned long>(s.runIndex));
    }
    std::snprintf(text[5], kLineBufSize, "R2 back");
}

void MainView::refreshCards(char text[kLines][kLineBufSize], const Model::Status &s)
{
    const Card c = static_cast<Card>(s.card);
    // Two lines only, at the bottom, so the card keeps the panel. The
    // question is on screen because the person holding the watch is the
    // instrument, and an instrument needs to know what it is being asked.
    std::snprintf(text[4], kLineBufSize, "%d/%d %s", s.card + 1,
                  static_cast<int>(Card::Count), cardName(c));
    std::snprintf(text[5], kLineBufSize, "%s", cardQuestion(c));
}

void MainView::refreshStair(char text[kLines][kLineBufSize], const Model::Status &s)
{
    std::snprintf(text[0], kLineBufSize, "WATCHDOG STAIR");
    std::snprintf(text[1], kLineBufSize, "step %d/%d", s.stairStep + 1,
                  static_cast<int>(MapLab::BenchSuite::kStairSteps));
    std::snprintf(text[2], kLineBufSize, "block %lu ms",
                  static_cast<unsigned long>(s.stairMs));
    // Said plainly, because it is true: a late step is expected to take the
    // device down, and the log is what survives it.
    std::snprintf(text[3], kLineBufSize, "may restart the watch");
    std::snprintf(text[4], kLineBufSize, "%s", s.lastNote[0] ? s.lastNote : "R1 to block");
    std::snprintf(text[5], kLineBufSize, "L1/L2 step  R2 back");
}

void MainView::refresh()
{
    const Model::Status &s = presenter->status();

    char text[kLines][kLineBufSize];
    for (int i = 0; i < kLines; i++) {
        text[i][0] = '\0';
    }

    switch (s.mode) {
        case Model::Mode::Menu:    refreshMenu(text, s);    break;
        case Model::Mode::Running: refreshRunning(text, s); break;
        case Model::Mode::Cards:   refreshCards(text, s);   break;
        case Model::Mode::Stair:   refreshStair(text, s);   break;
    }

    const bool showCanvas = (s.mode == Model::Mode::Cards);
    if (showCanvas) {
        mCanvasView.setSource(presenter->canvasPixels(), 240, 240);
    }
    mCanvasView.setVisible(showCanvas);

    for (int i = 0; i < kLines; i++) {
        touchgfx::Unicode::strncpy(mBuf[i], text[i], kLineBufSize - 1);
        mBuf[i][kLineBufSize - 1] = 0;
        mLine[i].setWildcard(mBuf[i]);
        mLine[i].setVisible(!mBlitRunning);
        mLine[i].invalidate();
    }
    invalidate();
}

void MainView::handleKeyEvent(uint8_t key)
{
    namespace Btn = SDK::GUI::Button;

    // The same four buttons everywhere, meaning the same four things: move,
    // move, do it, leave. A lab app whose buttons changed meaning per mode
    // would cost a measurement every time somebody pressed the wrong one.
    switch (key) {
        case Btn::L1: presenter->up();     break;
        case Btn::L2: presenter->down();   break;
        case Btn::R1: presenter->select(); break;
        case Btn::R2: presenter->back();   break;
        default: break;
    }
}
