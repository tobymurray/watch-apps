#include <gui/main_screen/MainView.hpp>

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include "SDK/GUI/Button.hpp"

#include <cstdio>

MainView::MainView()
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
    // Known cosmetic follow-up: a real Designer pass could give this screen
    // its own proper layout instead.
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

    // Two centred lines, stacked around the panel's vertical middle -- clear
    // of the round bezel's narrowest chord near the top/bottom (the same
    // lesson AthensRun's status label learned the hard way: a 240x240 round
    // panel's usable width shrinks well before its edges).
    mLine1.setPosition(30, 96, 180, 24);
    mLine1.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16));
    mLine1.setWideTextAction(touchgfx::WIDE_TEXT_WORDWRAP_ELLIPSIS);
    mLine1.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    mLine1Buf[0] = 0;
    mLine1.setWildcard(mLine1Buf);
    add(mLine1);

    mLine2.setPosition(30, 124, 180, 24);
    mLine2.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16));
    mLine2.setWideTextAction(touchgfx::WIDE_TEXT_WORDWRAP_ELLIPSIS);
    mLine2.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    mLine2Buf[0] = 0;
    mLine2.setWildcard(mLine2Buf);
    add(mLine2);

    refresh(presenter->progress());
}

void MainView::tearDownScreen()
{
    MainViewBase::tearDownScreen();
}

void MainView::onProgressChanged(const Model::Progress &progress)
{
    refresh(progress);
}

void MainView::refresh(const Model::Progress &progress)
{
    char text1[kLineBufSize];
    char text2[kLineBufSize];

    if (!progress.everReceived) {
        std::snprintf(text1, sizeof(text1), "starting...");
        text2[0] = '\0';
    } else if (progress.packsTotal == 0) {
        std::snprintf(text1, sizeof(text1), "no packs found");
        std::snprintf(text2, sizeof(text2), "drop one in SharedData/maps");
    } else if (!progress.anyInProgress) {
        // Nothing is scanning. That is only "all verified" if every pack
        // actually passed -- saying so while one of them failed its CRC is
        // exactly the wrong thing to tell someone whose map is missing.
        if (progress.packsVerified == progress.packsTotal) {
            std::snprintf(text1, sizeof(text1), "all packs verified");
            std::snprintf(text2, sizeof(text2), "%u of %u", progress.packsVerified,
                          progress.packsTotal);
        } else {
            std::snprintf(text1, sizeof(text1), "%u of %u verified",
                          progress.packsVerified, progress.packsTotal);
            std::snprintf(text2, sizeof(text2), "%u failed CRC",
                          static_cast<unsigned>(progress.packsTotal - progress.packsVerified));
        }
    } else if (progress.bytesTotal == 0 || progress.bytesDone == 0) {
        // Explicit precision, not a bare %s: a pack name can be up to
        // kMaxPackNameLen and this line holds far less, so the clip is
        // intended -- say so rather than leave snprintf to do it silently.
        std::snprintf(text1, sizeof(text1), "%.*s", kNameFieldChars, progress.packName);
        std::snprintf(text2, sizeof(text2), "starting...");
    } else {
        const unsigned percent = static_cast<unsigned>(
            (progress.bytesDone * 100ull) / progress.bytesTotal);
        // Room reserved for the trailing " 100%".
        std::snprintf(text1, sizeof(text1), "%.*s %u%%", kNameFieldChars - 5,
                      progress.packName, percent);

        // ETA from the actually-observed rate for this pass, not a
        // hardcoded assumption -- throughput varies by device/storage, so
        // this only means anything once real bytes have actually moved.
        // (bytesDone is non-zero here: the branch above catches the zero.)
        const uint64_t remainingBytes = progress.bytesTotal - progress.bytesDone;
        const uint64_t remainingMs    = (remainingBytes * progress.elapsedMs) / progress.bytesDone;
        const uint32_t remainingSec   = static_cast<uint32_t>(remainingMs / 1000);

        // The first sample or two of a pass are measured over a handful of
        // bytes and extrapolate to nonsense. Show that the estimate is not
        // ready yet rather than a confident "ETA 833m20s".
        if (remainingSec >= kImplausibleEtaSec) {
            std::snprintf(text2, sizeof(text2), "ETA --   %u/%u",
                          progress.packsVerified, progress.packsTotal);
        } else {
            // Cast: uint32_t is long unsigned on this target, so %u would be
            // a format mismatch without it.
            std::snprintf(text2, sizeof(text2), "ETA %um%02us  %u/%u",
                          static_cast<unsigned>(remainingSec / 60),
                          static_cast<unsigned>(remainingSec % 60),
                          progress.packsVerified, progress.packsTotal);
        }
    }

    touchgfx::Unicode::strncpy(mLine1Buf, text1, kLineBufSize - 1);
    mLine1Buf[kLineBufSize - 1] = 0;
    mLine1.setWildcard(mLine1Buf);
    mLine1.invalidate();

    touchgfx::Unicode::strncpy(mLine2Buf, text2, kLineBufSize - 1);
    mLine2Buf[kLineBufSize - 1] = 0;
    mLine2.setWildcard(mLine2Buf);
    mLine2.invalidate();
}

void MainView::handleKeyEvent(uint8_t key)
{
    namespace Btn = SDK::GUI::Button;

    // Nothing to command the service to do -- verification is autonomous.
    // The only interaction this screen offers is leaving it.
    if (key == Btn::R2) {
        presenter->exit();
    }
}
