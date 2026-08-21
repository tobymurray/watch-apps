#include <gui/main_screen/MainView.hpp>

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include "SDK/GUI/Button.hpp"

#include <cstdio>

#include "Catalogue/Catalogue.hpp"
#include "Stats/StreamStats.hpp"

namespace
{

using SensorLab::Catalogue::kTypeCount;
using SensorLab::Catalogue::kTypes;

const char *phaseName(uint8_t phase)
{
    switch (static_cast<CustomMessage::Phase>(phase)) {
        case CustomMessage::Phase::Starting:  return "start";
        case CustomMessage::Phase::Existence: return "sweep";
        case CustomMessage::Phase::Liveness:  return "live";
        case CustomMessage::Phase::Soak:      return "SOAK";
        case CustomMessage::Phase::Idle:      return "idle";
        case CustomMessage::Phase::Truncated: return "CUT";
    }
    return "?";
}

/// One character for the measured cadence. `s` streaming, `e` event, `-` not yet
/// classifiable -- which is the honest answer for a sensor that has spoken once,
/// and is what `TOUCH_DETECT` correctly reports.
char cadenceChar(uint8_t cadence)
{
    switch (static_cast<SensorLab::Stats::Cadence>(cadence)) {
        case SensorLab::Stats::Cadence::Streaming: return 's';
        case SensorLab::Stats::Cadence::Event:     return 'e';
        case SensorLab::Stats::Cadence::Unknown:   return '-';
    }
    return '?';
}

} // namespace

void MainView::setupScreen()
{
    MainViewBase::setupScreen();

    // Hide the inherited stopwatch-face widgets rather than repurpose them --
    // see the class doc comment for why relabelling them is the riskier option.
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

    for (int i = 0; i < kLines; i++) {
        // 22 px inset each side: a 240x240 round panel's usable chord is
        // narrower than its bounding box everywhere except the middle, and the
        // top and bottom lines are the ones that would clip. Narrower than the
        // Sleep Probe's 30 px because a roster row needs the width and the
        // extra two lines are nearer the middle of the glass.
        mLine[i].setPosition(22, static_cast<int16_t>(kFirstLineY + i * kLineHeight),
                             196, kLineHeight);
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

void MainView::onStateChanged(const Model::State &)
{
    refresh();
}

char MainView::markerFor(size_t typeIdx) const
{
    const Model::State &st = presenter->state();

    // Nothing has arrived for this row. Drawn as unknown rather than as a zeroed
    // measurement: zeroed memory reads as "resolves nothing, delivers nothing",
    // which is a finding, and inventing one is the worst thing this screen could
    // do.
    if (!st.rowsSeen[typeIdx]) {
        return '?';
    }

    const CustomMessage::RosterRow &r = st.rows[typeIdx];
    using Row = CustomMessage::RosterRow;

    if ((r.flags & Row::kResolved) == 0) {
        // No producer. Distinct from "not asked for", and both are distinct from
        // "asked, resolved, silent".
        return (r.flags & Row::kAsked) ? '-' : '.';
    }
    if ((r.flags & Row::kEverDelivered) == 0) {
        return (r.flags & Row::kAsked) ? 'o' : '.';
    }
    if (r.flags & Row::kFrameDiffers) {
        // Delivering, but not the frame the shipped parser expects. The loudest
        // marker, because for 28 of 29 parsers that silently invalidates every
        // sample.
        return '!';
    }
    return '*';
}

void MainView::shortName(char *out, size_t outSize, size_t typeIdx)
{
    if (typeIdx >= kTypeCount) {
        std::snprintf(out, outSize, "?");
        return;
    }
    // Trimmed towards the end, so `ACTIVITY_TIME_DAI` is still distinguishable
    // from `ACTIVITY_TIME`.
    std::snprintf(out, outSize, "%s", kTypes[typeIdx].name);
    out[outSize - 1] = '\0';
}

void MainView::drawRoster(char text[kLines][kLineBufSize])
{
    const Model::State &st = presenter->state();
    const auto         &s  = st.status;

    // The header. Phase and completeness, on every frame: no screen shows
    // findings without showing how much is missing.
    const unsigned complete =
        (s.claimsApplicable > 0)
            ? static_cast<unsigned>((static_cast<uint32_t>(s.claimsAnswered) * 100u)
                                    / s.claimsApplicable)
            : 0u;
    std::snprintf(text[0], kLineBufSize, "%s %u/%u %u%% r%lu",
                  phaseName(s.phase),
                  static_cast<unsigned>(s.typesResolved),
                  static_cast<unsigned>(kTypeCount),
                  complete,
                  static_cast<unsigned long>(s.runId));

    for (int i = 0; i < kRosterRows; i++) {
        const size_t t = static_cast<size_t>(mScroll) + static_cast<size_t>(i);
        if (t >= kTypeCount) {
            text[i + 1][0] = '\0';
            continue;
        }

        char name[13];
        shortName(name, sizeof(name), t);

        const CustomMessage::RosterRow &r = st.rows[t];
        const char marker = markerFor(t);

        // A rate to one decimal, and never without its longest gap: a sensor
        // delivering its nominal average in two bursts an hour apart is not
        // delivering at that rate.
        if (st.rowsSeen[t] && (r.flags & CustomMessage::RosterRow::kEverDelivered)) {
            std::snprintf(text[i + 1], kLineBufSize,
                          "%c%-12s %lu.%lu%c g%lu",
                          marker, name,
                          static_cast<unsigned long>(r.samplesPerMinX10 / 10u),
                          static_cast<unsigned long>(r.samplesPerMinX10 % 10u),
                          cadenceChar(r.cadence),
                          static_cast<unsigned long>(r.longestGapS));
        } else {
            std::snprintf(text[i + 1], kLineBufSize, "%c%-12s %02x %u%%",
                          marker, name,
                          static_cast<unsigned>(kTypes[t].value & 0xFFu),
                          static_cast<unsigned>(r.completePct));
        }
    }
}

void MainView::drawSummary(char text[kLines][kLineBufSize])
{
    const auto &s = presenter->state().status;

    const unsigned complete =
        (s.claimsApplicable > 0)
            ? static_cast<unsigned>((static_cast<uint32_t>(s.claimsAnswered) * 100u)
                                    / s.claimsApplicable)
            : 0u;

    std::snprintf(text[0], kLineBufSize, "SENSORLAB  %s", phaseName(s.phase));

    // The primary key, first. A profile whose firmware version is unknown cannot
    // be diffed, and a reader has to see that before anything else.
    if (s.firmware[0] != '\0') {
        std::snprintf(text[1], kLineBufSize, "fw %s%s", s.firmware,
                      s.haveSystemInfo ? "" : " (told)");
    } else {
        std::snprintf(text[1], kLineBufSize, "fw UNKNOWN - no diff");
    }

    std::snprintf(text[2], kLineBufSize, "%u/%u claims  %u%%",
                  static_cast<unsigned>(s.claimsAnswered),
                  static_cast<unsigned>(s.claimsApplicable), complete);
    std::snprintf(text[3], kLineBufSize, "%u conf  %u refuted",
                  static_cast<unsigned>(s.claimsConfirmed),
                  static_cast<unsigned>(s.claimsRefuted));
    std::snprintf(text[4], kLineBufSize, "%u/%u resolved  %u live",
                  static_cast<unsigned>(s.typesResolved),
                  static_cast<unsigned>(kTypeCount),
                  static_cast<unsigned>(s.typesDelivering));

    const unsigned long mins = s.runningMs / 60000u;
    std::snprintf(text[5], kLineBufSize, "run %lu  %luh%02lum",
                  static_cast<unsigned long>(s.runId),
                  mins / 60u, mins % 60u);

    // Failures first when there are any: a run that cannot write is one whose
    // numbers are missing, not one whose numbers are shorter.
    if (s.rowFailures > 0) {
        std::snprintf(text[6], kLineBufSize, "rows %lu  FAILED %lu",
                      static_cast<unsigned long>(s.rowsWritten),
                      static_cast<unsigned long>(s.rowFailures));
    } else {
        std::snprintf(text[6], kLineBufSize, "rows %lu  %luk",
                      static_cast<unsigned long>(s.rowsWritten),
                      static_cast<unsigned long>(s.bytesWritten / 1024u));
    }

    std::snprintf(text[7], kLineBufSize, "%lu samples",
                  static_cast<unsigned long>(s.samplesSeen));

    // The charger is not a detail. Plugging in terminates every running app, so
    // a soak on the cable records nothing at all.
    if (s.usb == 1 || s.charging == 1) {
        std::snprintf(text[8], kLineBufSize, "UNPLUG USB TO RUN");
    } else if (s.phase == static_cast<uint8_t>(CustomMessage::Phase::Soak)) {
        std::snprintf(text[8], kLineBufSize, "R1 stop   R2 exit");
    } else {
        std::snprintf(text[8], kLineBufSize, "R1 soak   R2 exit");
    }
}

void MainView::refresh()
{
    char text[kLines][kLineBufSize];
    for (int i = 0; i < kLines; i++) {
        text[i][0] = '\0';
    }

    if (!presenter->state().status.everReceived) {
        // Nothing heard yet is not the same as nothing arriving, and only one of
        // them means the instrument is broken. Say which this is.
        std::snprintf(text[0], kLineBufSize, "SENSORLAB");
        std::snprintf(text[1], kLineBufSize, "waiting for service...");
    } else if (mPage == Page::Roster) {
        drawRoster(text);
    } else {
        drawSummary(text);
    }

    for (int i = 0; i < kLines; i++) {
        touchgfx::Unicode::strncpy(mBuf[i], text[i], kLineBufSize - 1);
        mBuf[i][kLineBufSize - 1] = 0;
        mLine[i].setWildcard(mBuf[i]);
        mLine[i].invalidate();
    }
}

void MainView::handleKeyEvent(uint8_t key)
{
    namespace Btn = SDK::GUI::Button;

    switch (key) {
        case Btn::L1:
            // Scroll up, or back to the roster from the summary. One button
            // doing two things because there are four of them in total and one
            // has to be the exit.
            if (mPage == Page::Summary) {
                mPage = Page::Roster;
            } else if (mScroll > 0) {
                mScroll--;
            } else {
                mPage = Page::Summary;
            }
            refresh();
            break;

        case Btn::L2:
            if (mPage == Page::Summary) {
                mPage = Page::Roster;
                mScroll = 0;
            } else if (static_cast<size_t>(mScroll) + kRosterRows < kTypeCount) {
                mScroll++;
            }
            refresh();
            break;

        case Btn::R1:
            // Start a soak, or stop the one that is running. **The only
            // destructive-ish action on this screen**, and it is deliberately
            // the same button both ways: an instrument with a separate "stop"
            // has a way to leave a run open by accident.
            if (presenter->state().status.phase
                == static_cast<uint8_t>(CustomMessage::Phase::Soak)) {
                presenter->send(CustomMessage::Command::StopRun);
            } else {
                presenter->send(CustomMessage::Command::StartSoak);
            }
            break;

        case Btn::R2:
            // Leave the screen. An open soak keeps recording -- an instrument
            // that stopped measuring when the screen closed would only ever
            // measure watched sensors.
            presenter->exit();
            break;

        default:
            break;
    }
}
