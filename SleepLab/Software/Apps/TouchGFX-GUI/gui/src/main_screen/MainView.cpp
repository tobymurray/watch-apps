#include <gui/main_screen/MainView.hpp>

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include "SDK/GUI/Button.hpp"

#include "Engine/NightSummary.hpp"
#include "Engine/RestfulnessBand.hpp"

#include <cstdio>
#include <ctime>

namespace {

/// Local minutes past midnight as "HH:MM", or "--:--".
void formatTime(char *out, size_t n, int16_t localMin)
{
    if (localMin < 0) {
        std::snprintf(out, n, "--:--");
        return;
    }
    std::snprintf(out, n, "%02d:%02d", localMin / 60, localMin % 60);
}

/// Minutes as "7h24", or "--" when the value is absent.
///
/// Absent, not zero. A night that failed the worn gate has no total sleep
/// time, and "0h00" would read as a measurement of a terrible night rather
/// than as the absence of a claim.
void formatDuration(char *out, size_t n, int32_t minutes)
{
    if (minutes == Engine::kAbsent || minutes < 0) {
        std::snprintf(out, n, "--");
        return;
    }
    std::snprintf(out, n, "%ldh%02ld", static_cast<long>(minutes / 60),
                  static_cast<long>(minutes % 60));
}

/// The interruption bits as one short phrase, or nullptr if the night was
/// clean. The most consequential cause wins: charging means the app was
/// terminated outright, which subsumes everything else.
const char *interruptionText(uint16_t bits)
{
    namespace I = Engine::Interruption;
    if (bits == 0)                { return nullptr; }
    if (bits & I::kCharging)      { return "INTERRUPTED - was charging"; }
    if (bits & I::kResumed)       { return "INTERRUPTED - app restarted"; }
    if (bits & I::kClockJump)     { return "INTERRUPTED - clock changed"; }
    if (bits & I::kDataGap)       { return "INTERRUPTED - sensor gap"; }
    if (bits & I::kTruncated)     { return "INTERRUPTED - too long, cut"; }
    return "INTERRUPTED";
}

/// The worn verdict as the reason a night has no numbers.
const char *wornText(uint8_t worn)
{
    switch (static_cast<Engine::WornVerdict>(worn)) {
        case Engine::WornVerdict::NotWorn:   return "NOT WORN - no sleep data";
        case Engine::WornVerdict::Uncertain: return "UNCONFIRMED - no sleep data";
        default:                             return nullptr;
    }
}

/// Days since the epoch as "Mon 12 Aug".
void formatDay(char *out, size_t n, int32_t days)
{
    static const char *kWd[] = { "Thu","Fri","Sat","Sun","Mon","Tue","Wed" };
    static const char *kMon[] = { "Jan","Feb","Mar","Apr","May","Jun",
                                  "Jul","Aug","Sep","Oct","Nov","Dec" };
    if (days <= 0) {
        std::snprintf(out, n, "unknown");
        return;
    }
    // gmtime rather than localtime: the value is already a whole local day,
    // computed service-side from the night's own start. Re-applying a timezone
    // here would shift half the nights by one day.
    const std::time_t t = static_cast<std::time_t>(days) * 86400;
    std::tm g {};
#if defined(_WIN32) || defined(_WIN64)
    if (gmtime_s(&g, &t) != 0) { std::snprintf(out, n, "unknown"); return; }
#else
    if (gmtime_r(&t, &g) == nullptr) { std::snprintf(out, n, "unknown"); return; }
#endif
    std::snprintf(out, n, "%s %d %s", kWd[days % 7], g.tm_mday,
                  kMon[g.tm_mon % 12]);
}

} // namespace

void MainView::setupScreen()
{
    MainViewBase::setupScreen();

    // Hidden rather than repurposed -- see the class doc comment on why
    // relabelling Designer's packed string table is the riskier option.
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

    for (int i = 0; i < kMaxLines; i++) {
        // 26 px inset each side: a 240x240 round panel's usable chord is
        // narrower than its bounding box everywhere except the middle, and the
        // top and bottom lines are the ones that clip.
        mLine[i].setPosition(26, static_cast<int16_t>(kFirstLineY + i * kLineHeight),
                             188, kLineHeight);
        mLine[i].setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16_L));
        mLine[i].setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
        mLine[i].setWideTextAction(touchgfx::WIDE_TEXT_CHARWRAP_DOUBLE_ELLIPSIS);
        mBuf[i][0] = 0;
        mLine[i].setWildcard(mBuf[i]);
        add(mLine[i]);
    }

    // Centred: 100 buckets at 2 px is 200 px on a 240 px panel.
    mStrip.setXY(static_cast<int16_t>((240 - SleepStrip::kWidth) / 2), 158);
    add(mStrip);

    // The caption is not decoration. It travels with the picture, because a
    // four-level band drawn across a night looks exactly like a hypnogram and
    // this device cannot produce one.
    mCaption.setPosition(20, 188, 200, kLineHeight);
    mCaption.setTypedText(touchgfx::TypedText(T_TMP_MEDIUM_16));
    mCaption.setColor(touchgfx::Color::getColorFromRGB(170, 170, 170));
    mCaption.setWideTextAction(touchgfx::WIDE_TEXT_CHARWRAP_DOUBLE_ELLIPSIS);
    touchgfx::Unicode::strncpy(mCaptionBuf, Engine::RestfulnessBand::kCaption,
                               kLineBufSize - 1);
    mCaptionBuf[kLineBufSize - 1] = 0;
    mCaption.setWildcard(mCaptionBuf);
    add(mCaption);

    refresh();
}

void MainView::tearDownScreen()
{
    MainViewBase::tearDownScreen();
}

void MainView::onReportChanged(const Model::Report &) { refresh(); }
void MainView::onHistoryChanged(const Model::History &) { refresh(); }

void MainView::setLine(int i, const char *text)
{
    if (i < 0 || i >= kMaxLines) {
        return;
    }
    touchgfx::Unicode::strncpy(mBuf[i], text != nullptr ? text : "",
                               kLineBufSize - 1);
    mBuf[i][kLineBufSize - 1] = 0;
    mLine[i].setWildcard(mBuf[i]);
    mLine[i].invalidate();
}

void MainView::refresh()
{
    if (mMode == Mode::Report) {
        drawReport();
    } else {
        drawHistory();
    }
}

void MainView::drawReport()
{
    const Model::Report &rep = presenter->report();
    const CustomMessage::SleepReportData &d = rep.data;

    mStrip.setVisible(true);
    mCaption.setVisible(true);

    char text[16][kLineBufSize];
    for (auto &t : text) { t[0] = '\0'; }
    int n = 0;

    if (!rep.everReceived) {
        std::snprintf(text[n++], kLineBufSize, "SLEEP LAB");
        std::snprintf(text[n++], kLineBufSize, "waiting for service...");
        mStrip.setVisible(false);
        mCaption.setVisible(false);
    } else {
        // The first line is the honesty line, and the order here is the
        // priority order: not-worn suppresses everything, an interruption
        // qualifies everything, and only a clean night gets a plain heading.
        const char *worn = wornText(d.worn);
        const char *intr = interruptionText(d.interruption);

        if (worn != nullptr) {
            std::snprintf(text[n++], kLineBufSize, "%s", worn);
        } else if (intr != nullptr) {
            std::snprintf(text[n++], kLineBufSize, "%s", intr);
        } else {
            switch (static_cast<CustomMessage::Phase>(d.phase)) {
                case CustomMessage::Phase::Recording:
                    std::snprintf(text[n++], kLineBufSize, "RECORDING");
                    break;
                case CustomMessage::Phase::Watching:
                    std::snprintf(text[n++], kLineBufSize, "waiting for you to settle");
                    break;
                case CustomMessage::Phase::Reported:
                    std::snprintf(text[n++], kLineBufSize, "LAST NIGHT");
                    break;
                default:
                    std::snprintf(text[n++], kLineBufSize, "SLEEP LAB - idle");
                    break;
            }
        }

        // An interruption still gets said even when not-worn took the first
        // line: they are different problems with different remedies.
        if (worn != nullptr && intr != nullptr) {
            std::snprintf(text[n++], kLineBufSize, "%s", intr);
        }

        if (d.hasSleep) {
            char a[8], b[8], est[12], still[12];
            formatTime(a, sizeof(a), d.asleepAtMin);
            formatTime(b, sizeof(b), d.wokeAtMin);
            std::snprintf(text[n++], kLineBufSize, "%s - %s", a, b);

            formatDuration(est,   sizeof(est),   d.totalSleepMin);
            formatDuration(still, sizeof(still), d.stillInBedMin);
            // The estimate and the measurement on one line, labelled. Showing
            // only the first is the overclaim this app exists to avoid.
            std::snprintf(text[n++], kLineBufSize, "est %s  still %s", est, still);

            std::snprintf(text[n++], kLineBufSize, "eff %ld%%  awake %ldx",
                          static_cast<long>(d.efficiencyPct),
                          static_cast<long>(d.awakenings));

            if (d.hrMinX10 != Engine::kAbsent) {
                if (d.hrDeltaAvailable) {
                    // Against the wearer's own nights, never an absolute
                    // judgement. Population norms describe distributions; a
                    // person is one draw from one.
                    std::snprintf(text[n++], kLineBufSize, "HR low %ld  (%+ld vs you)",
                                  static_cast<long>(d.hrMinX10 / 10),
                                  static_cast<long>(d.hrDeltaX10 / 10));
                } else {
                    std::snprintf(text[n++], kLineBufSize,
                                  "HR low %ld  (%u more nights)",
                                  static_cast<long>(d.hrMinX10 / 10),
                                  static_cast<unsigned>(d.nightsNeeded));
                }
            }

            if (!d.bandUsedHr) {
                // "movement and heart rate" and "movement" are not the same
                // method, and the screen must not imply the first.
                std::snprintf(text[n++], kLineBufSize, "band: movement only");
            }
        } else if (d.phase == static_cast<uint8_t>(CustomMessage::Phase::Recording)) {
            std::snprintf(text[n++], kLineBufSize, "%u min so far",
                          static_cast<unsigned>(d.epochs));
            std::snprintf(text[n++], kLineBufSize, "report in the morning");
        } else if (worn == nullptr) {
            std::snprintf(text[n++], kLineBufSize, "no night recorded yet");
        }
    }

    for (int i = 0; i < kMaxLines; i++) {
        setLine(i, i < n ? text[i] : "");
        // The strip occupies the lower third, so lines past the report's own
        // count must not draw over it.
        mLine[i].setVisible(i < kReportLines);
    }

    mStrip.setStrip(d.strip, d.stripUsed);
    invalidate();
}

void MainView::drawHistory()
{
    const Model::History &h = presenter->history();

    mStrip.setVisible(false);
    mCaption.setVisible(false);

    char text[kMaxLines][kLineBufSize];
    for (auto &t : text) { t[0] = '\0'; }

    std::snprintf(text[0], kLineBufSize, "HISTORY  %u nights",
                  static_cast<unsigned>(h.count));

    if (h.count == 0) {
        // "Nothing heard yet" and "you have not recorded a night" need
        // different things said about them, and telling someone to go and
        // sleep in a watch they are already wearing would be the wrong one.
        std::snprintf(text[1], kLineBufSize,
                      h.everReceived ? "wear it overnight to start"
                                     : "starting...");
    } else {
        if (mFocus >= h.count) {
            mFocus = static_cast<int16_t>(h.count - 1);
        }

        // Window the list around the focus, clamped to the ends.
        int16_t top = static_cast<int16_t>(mFocus - kHistoryRows / 2);
        if (top < 0) {
            top = 0;
        }
        if (top + kHistoryRows > h.count) {
            top = static_cast<int16_t>(h.count - kHistoryRows);
        }
        if (top < 0) {
            top = 0;
        }

        for (int i = 0; i < kHistoryRows && (top + i) < h.count; i++) {
            const Model::HistoryRow &r = h.rows[top + i];
            char day[16];
            formatDay(day, sizeof(day), r.startUtcDays);

            const bool focused = (top + i) == mFocus;
            if (r.worn != static_cast<uint8_t>(Engine::WornVerdict::Worn)) {
                // A night with no numbers shows as one. Blanks in the columns
                // would read as zeroes.
                std::snprintf(text[1 + i], kLineBufSize, "%c%s  not worn",
                              focused ? '>' : ' ', day);
            } else {
                char est[12];
                formatDuration(est, sizeof(est), r.totalSleepMin);
                std::snprintf(text[1 + i], kLineBufSize, "%c%s %s %d%%%s",
                              focused ? '>' : ' ', day, est,
                              static_cast<int>(r.efficiencyPct),
                              r.interrupted ? " !" : "");
            }
        }
    }

    for (int i = 0; i < kMaxLines; i++) {
        setLine(i, text[i]);
        mLine[i].setVisible(true);
    }
    invalidate();
}

void MainView::moveFocus(int16_t delta)
{
    const Model::History &h = presenter->history();
    if (h.count == 0) {
        return;
    }
    int16_t next = static_cast<int16_t>(mFocus + delta);
    if (next < 0) {
        next = 0;
    }
    if (next >= h.count) {
        next = static_cast<int16_t>(h.count - 1);
    }
    if (next == mFocus) {
        return;
    }
    mFocus = next;
    drawHistory();
}

void MainView::handleKeyEvent(uint8_t key)
{
    namespace Btn = SDK::GUI::Button;

    switch (key) {
        case Btn::L1:
            if (mMode == Mode::History) {
                moveFocus(-1);
            }
            break;

        case Btn::L2:
            if (mMode == Mode::History) {
                moveFocus(1);
            }
            break;

        case Btn::R1:
            mMode  = (mMode == Mode::Report) ? Mode::History : Mode::Report;
            mFocus = 0;
            refresh();
            break;

        case Btn::R2:
            // Leaves the screen. The service keeps recording -- there is
            // deliberately nothing here that can stop it, because a button
            // that could would only add a way to lose a night.
            presenter->exit();
            break;

        default:
            break;
    }
}
