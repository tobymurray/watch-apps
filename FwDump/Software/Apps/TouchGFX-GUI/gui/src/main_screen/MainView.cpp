#include <gui/main_screen/MainView.hpp>

#include <images/BitmapDatabase.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include "SDK/GUI/Button.hpp"

#include <cstdarg>
#include <cstdio>

namespace {

/// Bytes as a short decimal with one place: "1.4" for 1.4 MB. Written out
/// rather than using %f because the SDK builds without floating-point
/// formatting in printf, and a "%.1f" that silently prints nothing is worse
/// than integer arithmetic that is obviously integer arithmetic.
void formatMegabytes(uint64_t bytes, unsigned& whole, unsigned& tenths)
{
    const uint64_t tenthsTotal = (bytes * 10ull) / (1024ull * 1024ull);
    whole  = static_cast<unsigned>(tenthsTotal / 10ull);
    tenths = static_cast<unsigned>(tenthsTotal % 10ull);
}

/// A short reason for each failure. Phrased as what happened rather than as an
/// error code -- the code is in the log, and the person reading this screen
/// needs to know whether to go and free some space or to give up.
///
/// Every string here must fit the detail line: about 22 characters at Poppins
/// Medium 16 in the 188px box. The longest below is 20.
const char* describeError(CustomMessage::DumpError error)
{
    switch (error) {
        case CustomMessage::DumpError::None:             return "no error";
        case CustomMessage::DumpError::BadRegion:        return "bad region config";
        case CustomMessage::DumpError::OpenFailed:       return "cannot open file";
        case CustomMessage::DumpError::ShortWrite:       return "write failed: full?";
        case CustomMessage::DumpError::VerifyFailed:     return "chunk will not verify";
        case CustomMessage::DumpError::ManifestFailed:   return "manifest failed";
        case CustomMessage::DumpError::ManifestOverflow: return "manifest too long";
    }
    return "unknown error";
}

} // namespace

MainView::MainView()
{
}

void MainView::setupScreen()
{
    MainViewBase::setupScreen();

    const touchgfx::colortype white = touchgfx::Color::getColorFromRGB(255, 255, 255);
    const touchgfx::colortype grey  = touchgfx::Color::getColorFromRGB(128, 128, 128);

    // Grouped about the vertical middle, where the round panel is widest. The
    // title is the highest thing on the screen at y=42, which is already well
    // clear of the chord that clipped AthensRun's top-anchored label.
    mTitle.setPosition(kInsetX, 40, kLineW, 22);
    mTitle.setTypedText(touchgfx::TypedText(T_TMP_ITALIC_18));
    mTitle.setColor(grey);
    mTitleBuf[0] = 0;
    mTitle.setWildcard(mTitleBuf);
    add(mTitle);

    // The headline. SemiBold 35 rather than 60: even single words like
    // "CHECKING" need the inset's full width at 35, and 60 would not fit any of
    // them. See the member's comment for why nothing longer goes here.
    mHeadline.setPosition(kInsetX, 68, kLineW, 46);
    mHeadline.setTypedText(touchgfx::TypedText(T_TMP_SEMIBOLD_35));
    mHeadline.setColor(white);
    mHeadlineBuf[0] = 0;
    mHeadline.setWildcard(mHeadlineBuf);
    add(mHeadline);

    // Track first, then fill on top of it: the fill's width is what changes, so
    // drawing it over a static track avoids having to repaint a background
    // behind a shrinking rectangle.
    mBarTrack.setPosition(kBarX, kBarY, kBarW, kBarH);
    mBarTrack.setColor(grey);
    add(mBarTrack);

    mBarFill.setPosition(kBarX + kBarBorder, kBarY + kBarBorder, 0,
                         kBarH - 2 * kBarBorder);
    mBarFill.setColor(white);
    add(mBarFill);

    mDetail.setPosition(kDetailX, 132, kDetailW, 20);
    mDetail.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16));
    mDetail.setColor(white);
    mDetailBuf[0] = 0;
    mDetail.setWildcard(mDetailBuf);
    add(mDetail);

    mDetail2.setPosition(kDetailX, 152, kDetailW, 20);
    mDetail2.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16));
    mDetail2.setColor(white);
    mDetail2Buf[0] = 0;
    mDetail2.setWildcard(mDetail2Buf);
    add(mDetail2);

    // Two short hint lines, each sized for its own row's chord, rather than one
    // long line with WIDE_TEXT_WORDWRAP_ELLIPSIS. That was the first attempt and
    // it did not wrap -- it clipped mid-word with no ellipsis, turning "Unplug
    // USB, press play. Do not reconnect until DONE." into "Unplug USB, pres".
    // Choosing the break here means every line is known to fit.
    mHint.setPosition(kHint1X, 176, kHint1W, 20);
    mHint.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16));
    mHint.setColor(grey);
    mHintBuf[0] = 0;
    mHint.setWildcard(mHintBuf);
    add(mHint);

    // The tightest row on the screen: at y~206 the round panel leaves about
    // 84px either side of centre, so this box is the narrowest of the lot.
    mHint2.setPosition(kHint2X, 196, kHint2W, 20);
    mHint2.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16));
    mHint2.setColor(grey);
    mHint2Buf[0] = 0;
    mHint2.setWildcard(mHint2Buf);
    add(mHint2);

    // Beside the R1 button, which is where Chrono puts its own play affordance
    // -- a position already proven to land inside the bezel on this panel.
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

void MainView::onStatusChanged(const Model::Status&)
{
    refresh();
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

void MainView::setBar(bool visible, uint64_t done, uint64_t total)
{
    mBarTrack.setVisible(visible);
    mBarFill.setVisible(visible);

    if (visible) {
        const int16_t inner = static_cast<int16_t>(kBarW - 2 * kBarBorder);
        int16_t filled = 0;
        if (total > 0) {
            filled = static_cast<int16_t>((done * static_cast<uint64_t>(inner)) / total);
        }
        mBarFill.setWidth(filled);
    }

    mBarTrack.invalidate();
    mBarFill.invalidate();
}

void MainView::refresh()
{
    const Model::Status& status = presenter->status();

    setLine(mTitle, mTitleBuf, "FIRMWARE DUMP");

    // Nothing heard from the service yet. Distinct from Idle on purpose: telling
    // someone to press a button that nothing is listening for is worse than
    // telling them to wait a moment.
    if (!status.everReceived) {
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

    const unsigned total = status.chunksTotal;
    unsigned mbWhole = 0, mbTenths = 0;
    formatMegabytes(status.bytesTotal, mbWhole, mbTenths);

    // Only Idle offers the start button, so the icon tracks that exactly rather
    // than being left up as decoration in states where a press does nothing.
    const bool canStart = status.state == CustomMessage::DumpState::Idle
                          || status.state == CustomMessage::DumpState::Error;
    mStartIcon.setVisible(canStart);
    mStartIcon.invalidate();

    switch (status.state) {
        case CustomMessage::DumpState::Idle: {
            const bool resuming = status.scanComplete && status.chunksPresent > 0
                                  && status.chunksPresent < total;
            const bool complete = status.scanComplete && total > 0
                                  && status.chunksPresent == total;

            if (complete) {
                // Every chunk is already there from a previous run. Say so, and
                // say that starting re-verifies rather than redoes -- otherwise
                // this looks like the app forgot it had finished.
                setLine(mHeadline, mHeadlineBuf, "READY");
                setLine(mDetail, mDetailBuf, "all %u chunks on disk", total);
                setLine(mDetail2, mDetail2Buf, "play re-verifies them");
            } else if (resuming) {
                setLine(mHeadline, mHeadlineBuf, "RESUME");
                setLine(mDetail, mDetailBuf, "%u of %u already done",
                        static_cast<unsigned>(status.chunksPresent), total);
                setLine(mDetail2, mDetail2Buf, "play finishes the rest");
            } else {
                setLine(mHeadline, mHeadlineBuf, "READY");
                setLine(mDetail, mDetailBuf, "%u.%u MB, %u chunks", mbWhole, mbTenths, total);
                setLine(mDetail2, mDetail2Buf, "a few minutes");
            }
            setBar(false, 0, 0);
            setLine(mHint, mHintBuf, "Unplug USB, then play");
            setLine(mHint2, mHint2Buf, "no USB until DONE");
            break;
        }

        case CustomMessage::DumpState::Checking:
            // Shows a count so a resume scan cannot be mistaken for a stall.
            setLine(mHeadline, mHeadlineBuf, "CHECKING");
            setBar(false, 0, 0);
            setLine(mDetail, mDetailBuf, "%u of %u found",
                    static_cast<unsigned>(status.chunksPresent), total);
            setLine(mDetail2, mDetail2Buf, "what is already here");
            setLine(mHint, mHintBuf, "one moment");
            setLine(mHint2, mHint2Buf, " ");
            break;

        case CustomMessage::DumpState::Dumping: {
            unsigned doneWhole = 0, doneTenths = 0;
            formatMegabytes(status.bytesDone, doneWhole, doneTenths);

            // The headline is the chunk count, not a percentage: it is the number
            // that matches the files on disk and the manifest, so it is the one
            // worth reading off the screen.
            setLine(mHeadline, mHeadlineBuf, "%02u/%02u",
                    static_cast<unsigned>(status.chunksDone), total);
            setBar(true, status.bytesDone, status.bytesTotal);
            setLine(mDetail, mDetailBuf, "%u.%u / %u.%u MB", doneWhole, doneTenths, mbWhole,
                    mbTenths);

            if (status.etaSec == 0 || status.etaSec >= kImplausibleEtaSec) {
                setLine(mDetail2, mDetail2Buf, "%lu KB/s  ETA --",
                        static_cast<unsigned long>(status.kbPerSec));
            } else {
                setLine(mDetail2, mDetail2Buf, "%lu KB/s  ETA %lum%02lus",
                        static_cast<unsigned long>(status.kbPerSec),
                        static_cast<unsigned long>(status.etaSec / 60),
                        static_cast<unsigned long>(status.etaSec % 60));
            }

            // A stall already happened and the dump carried on. Worth saying,
            // because the cause is almost certainly a cable that is still in --
            // and if it is, this progress is about to stop again.
            if (status.stalledMs > 0) {
                setLine(mHint, mHintBuf, "paused earlier");
                setLine(mHint2, mHint2Buf, "USB in? keep it out");
            } else {
                setLine(mHint, mHintBuf, "keep USB out");
                if (status.chunksVerified > 0) {
                    setLine(mHint2, mHint2Buf, "%u re-verified",
                            static_cast<unsigned>(status.chunksVerified));
                } else {
                    setLine(mHint2, mHint2Buf, "until it says DONE");
                }
            }
            break;
        }

        case CustomMessage::DumpState::Done:
            // Unmistakably terminal, and carries the one number the host will
            // print back: whole_image_crc32. Being able to eyeball-match that
            // against reassemble_dump.py's output is the point of putting a
            // hex word on a watch face at all.
            setLine(mHeadline, mHeadlineBuf, "DONE");
            setBar(true, 1, 1);
            setLine(mDetail, mDetailBuf, "%u / %u chunks",
                    static_cast<unsigned>(status.chunksDone), total);
            setLine(mDetail2, mDetail2Buf, "CRC32 %08lX",
                    static_cast<unsigned long>(status.wholeCrc));
            setLine(mHint, mHintBuf, "plug in USB, copy");
            setLine(mHint2, mHint2Buf, "Apps/FwDump/");
            break;

        case CustomMessage::DumpState::Error:
            setLine(mHeadline, mHeadlineBuf, "ERROR");
            setBar(false, 0, 0);
            setLine(mDetail, mDetailBuf, "%s", describeError(status.error));
            setLine(mDetail2, mDetail2Buf, "at chunk %u of %u",
                    static_cast<unsigned>(status.errorChunk), total);
            // Even here there is something to do: chunks that completed are on
            // disk and a restart resumes from them, so pressing play is a real
            // option rather than a forlorn hope.
            setLine(mHint, mHintBuf, "%u chunks kept",
                    static_cast<unsigned>(status.chunksDone));
            setLine(mHint2, mHint2Buf, "play to retry");
            break;
    }
}

void MainView::handleKeyEvent(uint8_t key)
{
    namespace Btn = SDK::GUI::Button;

    switch (key) {
        case Btn::R1:
            // The one command. A press in any other state is dropped here rather
            // than sent and ignored by the service, so the button does nothing
            // visible when it should do nothing -- and the service's log stays
            // free of starts it had to refuse.
            if (presenter->status().state == CustomMessage::DumpState::Idle
                    || presenter->status().state == CustomMessage::DumpState::Error) {
                presenter->start();
            }
            break;

        case Btn::R2:
            // Leaves the screen. The service keeps dumping -- see Model::exitApp.
            presenter->exit();
            break;

        default:
            break;
    }
}
