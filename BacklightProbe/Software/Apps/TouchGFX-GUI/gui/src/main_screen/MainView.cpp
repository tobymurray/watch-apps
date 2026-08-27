#include <gui/main_screen/MainView.hpp>

#include <images/BitmapDatabase.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include "SDK/GUI/Button.hpp"

#include <cstdarg>
#include <cstdio>

#include "ProbePlan.hpp"

namespace {

const touchgfx::colortype kWhite = touchgfx::Color::getColorFromRGB(255, 255, 255);
const touchgfx::colortype kBlack = touchgfx::Color::getColorFromRGB(0, 0, 0);
const touchgfx::colortype kGrey  = touchgfx::Color::getColorFromRGB(128, 128, 128);

/// Mid grey on white, for the same role grey-on-black plays in the dark scheme.
/// A lighter grey would vanish against the measuring field.
const touchgfx::colortype kDimOnWhite = touchgfx::Color::getColorFromRGB(90, 90, 90);

/// Short name for what the plan is doing, for the detail line.
const char* describeAction(uint8_t action)
{
    switch (static_cast<Probe::Action>(action)) {
        case Probe::Action::Note:         return "section";
        case Probe::Action::SetBacklight: return "request sent";
        case Probe::Action::Sweep:        return "sweeping registers";
        case Probe::Action::Hold:         return "hold still - meter now";
        case Probe::Action::Observe:      return "watch the light";
        case Probe::Action::ProbeIids:    return "probing interface IDs";
    }
    return "";
}

/// The message result, as the screen shows it. Short: this is the one thing on
/// screen that says whether the kernel is answering at all, and PENDING with a
/// non-zero send timeout is the interesting case.
const char* describeResult(uint8_t result)
{
    switch (result) {
        case 0:  return "PENDING";
        case 1:  return "SUCCESS";
        case 2:  return "FAIL";
        case 3:  return "TIMEOUT";
        default: return "?";
    }
}

} // namespace

MainView::MainView()
{
}

void MainView::setupScreen()
{
    MainViewBase::setupScreen();

    // Added first, so it is behind everything. Starts in the dark scheme: the
    // screen opens on Idle, which is not being measured.
    mField.setPosition(0, 0, 240, 240);
    mField.setColor(kBlack);
    add(mField);

    mTitle.setPosition(kInsetX, 40, kLineW, 22);
    mTitle.setTypedText(touchgfx::TypedText(T_TMP_ITALIC_18));
    mTitle.setColor(kGrey);
    mTitleBuf[0] = 0;
    mTitle.setWildcard(mTitleBuf);
    add(mTitle);

    // SemiBold 35. The counter is the widest thing that goes here: "12.34"
    // is five glyphs plus a point, which fits the inset at this size where a
    // larger face would not.
    mHeadline.setPosition(kInsetX, 68, kLineW, 46);
    mHeadline.setTypedText(touchgfx::TypedText(T_TMP_SEMIBOLD_35));
    mHeadline.setColor(kWhite);
    mHeadlineBuf[0] = 0;
    mHeadline.setWildcard(mHeadlineBuf);
    add(mHeadline);

    mBarTrack.setPosition(kBarX, kBarY, kBarW, kBarH);
    mBarTrack.setColor(kGrey);
    add(mBarTrack);

    mBarFill.setPosition(kBarX + kBarBorder, kBarY + kBarBorder, 0, kBarH - 2 * kBarBorder);
    mBarFill.setColor(kWhite);
    add(mBarFill);

    mDetail.setPosition(kDetailX, 132, kDetailW, 20);
    mDetail.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16));
    mDetail.setColor(kWhite);
    mDetailBuf[0] = 0;
    mDetail.setWildcard(mDetailBuf);
    add(mDetail);

    mDetail2.setPosition(kDetailX, 152, kDetailW, 20);
    mDetail2.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16));
    mDetail2.setColor(kWhite);
    mDetail2Buf[0] = 0;
    mDetail2.setWildcard(mDetail2Buf);
    add(mDetail2);

    mHint.setPosition(kHint1X, 176, kHint1W, 20);
    mHint.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16));
    mHint.setColor(kGrey);
    mHintBuf[0] = 0;
    mHint.setWildcard(mHintBuf);
    add(mHint);

    mHint2.setPosition(kHint2X, 196, kHint2W, 20);
    mHint2.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16));
    mHint2.setColor(kGrey);
    mHint2Buf[0] = 0;
    mHint2.setWildcard(mHint2Buf);
    add(mHint2);

    mStartIcon.setXY(190, 59);
    mStartIcon.setBitmap(touchgfx::Bitmap(BITMAP_ICON_PLAY_ID));
    mStartIcon.setVisible(false);
    add(mStartIcon);

    refresh();
}

void MainView::tearDownScreen()
{
    MainViewBase::tearDownScreen();
}

void MainView::setMeasuringField(bool measuring)
{
    if (measuring == mMeasuring) {
        return; // Repainting everything for no change would be its own confound.
    }
    mMeasuring = measuring;

    const touchgfx::colortype field = measuring ? kWhite : kBlack;
    const touchgfx::colortype text  = measuring ? kBlack : kWhite;
    const touchgfx::colortype dim   = measuring ? kDimOnWhite : kGrey;

    mField.setColor(field);
    mHeadline.setColor(text);
    mDetail.setColor(text);
    mDetail2.setColor(text);
    mTitle.setColor(dim);
    mHint.setColor(dim);
    mHint2.setColor(dim);

    // The bar inverts with the scheme too, or the fill disappears into the
    // white field.
    mBarTrack.setColor(dim);
    mBarFill.setColor(text);

    mField.invalidate();
}

void MainView::onStatusChanged(const Model::Status& status)
{
    mQuiet = status.quiet;
    refresh();
}

void MainView::handleTickEvent()
{
    // The counter, and nothing else. Every other line changes only when a
    // snapshot arrives.
    if (mQuiet) {
        return; // See the class comment: a repaint is a message to the kernel.
    }

    const Model::Status& status = presenter->status();
    if (!status.everReceived || status.state != CustomMessage::ProbeState::Running) {
        return;
    }

    const uint32_t elapsedMs = presenter->liveStepElapsedMs();
    const uint32_t centis    = elapsedMs / 10u;
    if (centis == mLastCounterCentis) {
        return; // Nothing a person could see has changed.
    }
    mLastCounterCentis = centis;

    setLine(mHeadline, mHeadlineBuf, "%lu.%02lu", static_cast<unsigned long>(centis / 100u),
            static_cast<unsigned long>(centis % 100u));
}

void MainView::setLine(touchgfx::TextAreaWithOneWildcard& area,
                       touchgfx::Unicode::UnicodeChar* buffer, const char* format, ...)
{
    char text[kLineBufSize];

    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    touchgfx::Unicode::strncpy(buffer, text, kLineBufSize - 1);
    buffer[kLineBufSize - 1] = 0;
    area.setWildcard(buffer);
    area.invalidate();
}

void MainView::setBar(bool visible, uint32_t done, uint32_t total)
{
    mBarTrack.setVisible(visible);
    mBarFill.setVisible(visible);

    if (visible) {
        const int16_t inner = static_cast<int16_t>(kBarW - 2 * kBarBorder);
        int16_t filled = 0;
        if (total > 0) {
            filled = static_cast<int16_t>((static_cast<uint32_t>(done) * static_cast<uint32_t>(inner))
                                          / total);
        }
        mBarFill.setWidth(filled);
    }

    mBarTrack.invalidate();
    mBarFill.invalidate();
}

void MainView::refresh()
{
    const Model::Status& status = presenter->status();

    // Nothing heard from the service yet. Deliberately distinct from Idle:
    // offering a button that nothing is listening for is worse than saying wait.
    if (!status.everReceived) {
        setMeasuringField(false);
        setLine(mTitle, mTitleBuf, "BACKLIGHT PROBE");
        setLine(mHeadline, mHeadlineBuf, "...");
        setBar(false, 0, 0);
        setLine(mDetail, mDetailBuf, " ");
        setLine(mDetail2, mDetail2Buf, " ");
        setLine(mHint, mHintBuf, "starting up");
        setLine(mHint2, mHint2Buf, " ");
        mStartIcon.setVisible(false);
        mStartIcon.invalidate();
        return;
    }

    // White field while the plan runs, so every brightness photograph is taken
    // against the same maximum-reflectance target. Dark otherwise.
    const bool running = (status.state == CustomMessage::ProbeState::Running);
    setMeasuringField(running);

    const bool canStart = (status.state == CustomMessage::ProbeState::Idle);
    mStartIcon.setVisible(canStart);
    mStartIcon.invalidate();

    switch (status.state) {
        case CustomMessage::ProbeState::Idle:
            setLine(mTitle, mTitleBuf, "BACKLIGHT PROBE");
            setLine(mHeadline, mHeadlineBuf, "READY");
            setBar(false, 0, 0);
            if (status.registersAvailable) {
                setLine(mDetail, mDetailBuf, "%u steps, about 4 min",
                        static_cast<unsigned>(status.stepCount));
                setLine(mDetail2, mDetail2Buf, "%u need the camera",
                        static_cast<unsigned>(status.observeSteps));
            } else {
                // Said plainly. On a build with no registers the run measures
                // nothing, and a screen that looked identical to a real run
                // would be the worst thing this app could do.
                setLine(mDetail, mDetailBuf, "NO REGISTERS ON THIS");
                setLine(mDetail2, mDetail2Buf, "BUILD - measures nothing");
            }
            setLine(mHint, mHintBuf, "Unplug USB, then play");
            setLine(mHint2, mHint2Buf, "film the screen");
            break;

        case CustomMessage::ProbeState::Running: {
            setLine(mTitle, mTitleBuf, "%02u/%02u  %s", static_cast<unsigned>(status.stepIndex + 1),
                    static_cast<unsigned>(status.stepCount), describeResult(status.lastResult));

            if (status.quiet) {
                // A held step. The headline names what is being held rather than
                // counting, because nothing here should move: this is the frame
                // being photographed.
                setLine(mHeadline, mHeadlineBuf, "HOLD");
                mLastCounterCentis = 0xFFFFFFFFu; // Force a repaint on the next OBSERVE.
            } else {
                // The counter takes over from here; handleTickEvent keeps it
                // moving between snapshots.
                const uint32_t centis = presenter->liveStepElapsedMs() / 10u;
                mLastCounterCentis    = centis;
                setLine(mHeadline, mHeadlineBuf, "%lu.%02lu",
                        static_cast<unsigned long>(centis / 100u),
                        static_cast<unsigned long>(centis % 100u));
            }

            setBar(true, status.stepIndex, status.stepCount);
            setLine(mDetail, mDetailBuf, "%s", status.label);
            setLine(mDetail2, mDetail2Buf, "%s", describeAction(status.action));

            if (status.quiet) {
                setLine(mHint, mHintBuf, "meter or photograph");
                setLine(mHint2, mHint2Buf, "screen is holding still");
            } else {
                setLine(mHint, mHintBuf, "keep filming");
                setLine(mHint2, mHint2Buf, "note when it dims");
            }
            break;
        }

        case CustomMessage::ProbeState::Done:
            setLine(mTitle, mTitleBuf, "BACKLIGHT PROBE");
            setLine(mHeadline, mHeadlineBuf, "DONE");
            setBar(true, 1, 1);
            setLine(mDetail, mDetailBuf, "%u steps run",
                    static_cast<unsigned>(status.stepCount));
            // Whether the record is whole is the one thing worth knowing before
            // plugging in, because a truncated results file is a run to repeat.
            setLine(mDetail2, mDetail2Buf, status.logIntact ? "record intact"
                                                            : "RECORD TRUNCATED");
            setLine(mHint, mHintBuf, "plug in USB, copy");
            setLine(mHint2, mHint2Buf, "Apps/BacklightProbe/");
            break;
    }
}

void MainView::handleKeyEvent(uint8_t key)
{
    namespace Btn = SDK::GUI::Button;

    switch (key) {
        case Btn::R1:
            // Only Idle. A press mid-run is dropped here rather than sent and
            // refused, so the button does nothing visible when it should do
            // nothing, and a press during a measurement cannot become a
            // display update in the middle of a held frame.
            if (presenter->status().state == CustomMessage::ProbeState::Idle) {
                presenter->start();
            }
            break;

        case Btn::R2:
            // Leaves the screen. The service keeps running the plan; see
            // Model::exitApp. Suite 1 deliberately spends time dark, so this is
            // not an abort.
            presenter->exit();
            break;

        default:
            break;
    }
}
