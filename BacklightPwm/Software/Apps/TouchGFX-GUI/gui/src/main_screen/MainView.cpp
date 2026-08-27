#include <gui/main_screen/MainView.hpp>

#include <images/BitmapDatabase.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include "SDK/GUI/Button.hpp"

#include <cstdarg>
#include <cstdio>

namespace {

const touchgfx::colortype kWhite = touchgfx::Color::getColorFromRGB(255, 255, 255);
const touchgfx::colortype kBlack = touchgfx::Color::getColorFromRGB(0, 0, 0);
const touchgfx::colortype kGrey  = touchgfx::Color::getColorFromRGB(128, 128, 128);

/// Mid grey on white. A lighter one vanishes against the measuring field.
const touchgfx::colortype kDimOnWhite = touchgfx::Color::getColorFromRGB(90, 90, 90);

} // namespace

MainView::MainView()
{
}

void MainView::setupScreen()
{
    MainViewBase::setupScreen();

    // Added first, so it sits behind the text. See the class comment: this is
    // the photometric target, not decoration.
    mField.setPosition(0, 0, 240, 240);
    mField.setColor(kBlack);
    add(mField);

    mTitle.setPosition(kInsetX, 40, kLineW, 22);
    mTitle.setTypedText(touchgfx::TypedText(T_TMP_ITALIC_18));
    mTitle.setColor(kGrey);
    mTitleBuf[0] = 0;
    mTitle.setWildcard(mTitleBuf);
    add(mTitle);

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
        return;
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
    mBarTrack.setColor(dim);
    mBarFill.setColor(text);

    mField.invalidate();
}

void MainView::onStatusChanged(const Model::Status& status)
{
    (void)status;
    refresh();
}

void MainView::handleTickEvent()
{
    // Nothing here repaints. Every line on this screen changes only when a
    // snapshot arrives, and a repaint is a REQUEST_DISPLAY_UPDATE to the same
    // kernel this app is contesting a pin with. BacklightProbe had to move a
    // counter because it was timing an event it could not see; this app is
    // driving the light itself and has nothing to time.
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
            filled = static_cast<int16_t>((done * static_cast<uint32_t>(inner)) / total);
        }
        mBarFill.setWidth(filled);
    }

    mBarTrack.invalidate();
    mBarFill.invalidate();
}

void MainView::refresh()
{
    const Model::Status& status = presenter->status();

    if (!status.everReceived) {
        setMeasuringField(false);
        setLine(mTitle, mTitleBuf, "BACKLIGHT PWM");
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

    // White field only while actually driving. On the refused path the screen
    // stays dark, so a run that touched nothing cannot be mistaken at a glance
    // for one that did.
    const bool running = (status.state == CustomMessage::PwmState::Running) && status.driving;
    setMeasuringField(running);

    const bool canStart = (status.state == CustomMessage::PwmState::Idle);
    mStartIcon.setVisible(canStart);
    mStartIcon.invalidate();

    switch (status.state) {
        case CustomMessage::PwmState::Idle:
            setLine(mTitle, mTitleBuf, "BACKLIGHT PWM");
            setLine(mHeadline, mHeadlineBuf, "READY");
            setBar(false, 0, 0);
            setLine(mDetail, mDetailBuf, "%u rungs, about 40 s",
                    static_cast<unsigned>(status.rungCount));
            // Says what it is about to do to the hardware, on the screen, before
            // the button is pressed. This app writes a pin the kernel owns and
            // that should not be a surprise to anyone holding the watch.
            setLine(mDetail2, mDetail2Buf, "drives PF3 directly");
            setLine(mHint, mHintBuf, "Unplug USB, then play");
            setLine(mHint2, mHint2Buf, "R1 play  R2 leave");
            break;

        case CustomMessage::PwmState::Running: {
            setLine(mTitle, mTitleBuf, "%02u/%02u  %lu MHz",
                    static_cast<unsigned>(status.rungIndex + 1),
                    static_cast<unsigned>(status.rungCount),
                    static_cast<unsigned long>(status.cyclesPerUs));

            // The requested duty, big. This is the number to photograph next to
            // the light.
            setLine(mHeadline, mHeadlineBuf, "%u%%", static_cast<unsigned>(status.requestedDuty));

            setBar(true, status.rungIndex, status.rungCount);

            // Achieved next to requested, never instead of it. The gap is the
            // measure of what a busy-wait PWM sharing a thread with a message
            // loop actually delivers.
            setLine(mDetail, mDetailBuf, "got %u%%  %lu periods",
                    static_cast<unsigned>(status.achievedDuty),
                    static_cast<unsigned long>(status.periods));
            setLine(mDetail2, mDetail2Buf, "%s", status.label);

            setLine(mHint, mHintBuf, "meter or photograph");
            setLine(mHint2, mHint2Buf, "R1 stops, gives pin back");
            break;
        }

        case CustomMessage::PwmState::Done:
            setLine(mTitle, mTitleBuf, "BACKLIGHT PWM");
            setLine(mHeadline, mHeadlineBuf, "DONE");
            setBar(true, 1, 1);
            setLine(mDetail, mDetailBuf, "%lu periods, %lu edges",
                    static_cast<unsigned long>(status.periods),
                    static_cast<unsigned long>(status.edges));
            setLine(mDetail2, mDetail2Buf, "pin handed back");
            setLine(mHint, mHintBuf, "kernel owns PF3 again");
            setLine(mHint2, mHint2Buf, "relaunch to run again");
            break;

        case CustomMessage::PwmState::Calibrating:
            // Brief, but it has to exist: this state is a tenth of a second of
            // blocking, and without it the screen sat on READY looking as though
            // the button had not been seen.
            setLine(mTitle, mTitleBuf, "BACKLIGHT PWM");
            setLine(mHeadline, mHeadlineBuf, "CLOCK");
            setBar(false, 0, 0);
            setLine(mDetail, mDetailBuf, "measuring core clock");
            setLine(mDetail2, mDetail2Buf, " ");
            setLine(mHint, mHintBuf, "one moment");
            setLine(mHint2, mHint2Buf, " ");
            break;

        case CustomMessage::PwmState::Refused:
            // Never looks like a run. This is the state a host or simulator
            // build sits in permanently, and the state a device build reaches
            // when the cycle counter will not start.
            setLine(mTitle, mTitleBuf, "BACKLIGHT PWM");
            setLine(mHeadline, mHeadlineBuf, "NO DRIVE");
            setBar(false, 0, 0);
            setLine(mDetail, mDetailBuf, "nothing was driven");
            setLine(mDetail2, mDetail2Buf, status.cyclesPerUs == 0 ? "no cycle counter"
                                                                  : "no registers here");
            setLine(mHint, mHintBuf, "this build cannot");
            setLine(mHint2, mHint2Buf, "measure anything");
            break;
    }
}

void MainView::handleKeyEvent(uint8_t key)
{
    namespace Btn = SDK::GUI::Button;

    switch (key) {
        case Btn::R1:
            // Play when idle, stop when running. Giving the pin back is the one
            // control that matters mid-run, so it gets the button that is
            // already under the user's thumb rather than a second one they would
            // have to find while the light is doing something unexpected.
            if (presenter->status().state == CustomMessage::PwmState::Idle) {
                presenter->start();
            } else if (presenter->status().state == CustomMessage::PwmState::Running) {
                presenter->stop();
            }
            break;

        case Btn::R2:
            // Leaves the screen. The service keeps the pin and keeps climbing.
            presenter->exit();
            break;

        default:
            break;
    }
}
