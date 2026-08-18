#include <gui/main_screen/MainView.hpp>

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include "SDK/GUI/Button.hpp"

#include <cstdio>

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
        // 30px inset each side: a 240x240 round panel's usable chord is
        // narrower than its bounding box everywhere except the middle, and the
        // top and bottom lines here are the ones that would clip.
        mLine[i].setPosition(30, static_cast<int16_t>(kFirstLineY + i * kLineHeight),
                             180, kLineHeight);
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

void MainView::formatSensors(char *out, size_t outSize, uint16_t subscribed) const
{
    namespace Sub = CustomMessage::Sub;

    // Order matches the log's column order, so the block reads the same way
    // the CSV does.
    static const struct { uint16_t bit; char letter; } kMap[] = {
        { Sub::kAccel,       'A' },
        { Sub::kTouch,       'T' },
        { Sub::kMotion,      'M' },
        { Sub::kActivity,    'R' },
        { Sub::kHr,          'H' },
        { Sub::kHrEx,        'X' },
        { Sub::kBeat,        'B' },
        { Sub::kPpg,         'P' },
        { Sub::kSpo2,        'O' },
        { Sub::kSteps,       'S' },
        { Sub::kBattLevel,   'L' },
        { Sub::kBattCharge,  'C' },
        { Sub::kBattMetrics, 'E' },
    };

    size_t n = 0;
    for (const auto &e : kMap) {
        if (n + 1 >= outSize) {
            break;
        }
        const bool on = (subscribed & e.bit) != 0;
        out[n++] = on ? e.letter : static_cast<char>(e.letter - 'A' + 'a');
    }
    out[n < outSize ? n : outSize - 1] = '\0';
}

void MainView::refresh()
{
    const Model::Status &s = presenter->status();

    char text[kLines][kLineBufSize];
    for (int i = 0; i < kLines; i++) {
        text[i][0] = '\0';
    }

    if (!s.everReceived) {
        // Nothing heard yet is not the same as nothing arriving, and only one
        // of them means the run is broken. Say which this is.
        std::snprintf(text[0], kLineBufSize, "SLEEP PROBE");
        std::snprintf(text[1], kLineBufSize, "waiting for service...");
    } else {
        static const char *kHrMode[] = { "cont", "off", "duty" };
        const char *hr = (s.hrMode < 3) ? kHrMode[s.hrMode] : "?";

        const uint32_t mins = s.runningMs / 60000u;
        std::snprintf(text[0], kLineBufSize, "run %luh%02lum  hr:%s",
                      static_cast<unsigned long>(mins / 60u),
                      static_cast<unsigned long>(mins % 60u), hr);

        char sensors[24];
        formatSensors(sensors, sizeof(sensors), s.subscribed);
        std::snprintf(text[1], kLineBufSize, "%s", sensors);

        // Failures first when there are any: a run writing nothing to storage
        // is the one state where everything else on this screen is irrelevant.
        if (s.rowFailures > 0) {
            std::snprintf(text[2], kLineBufSize, "rows %lu  FAILED %lu",
                          static_cast<unsigned long>(s.rowsWritten),
                          static_cast<unsigned long>(s.rowFailures));
        } else {
            std::snprintf(text[2], kLineBufSize, "rows %lu  %luk",
                          static_cast<unsigned long>(s.rowsWritten),
                          static_cast<unsigned long>(s.bytesWritten / 1024u));
        }

        if (s.rowsWritten == 0) {
            std::snprintf(text[3], kLineBufSize, "first row within 1 min");
        } else {
            std::snprintf(text[3], kLineBufSize, "last acc %ld  hr %ld",
                          static_cast<long>(s.lastAccN),
                          static_cast<long>(s.lastHrN));
        }

        if (s.lastTouchN > 0) {
            std::snprintf(text[4], kLineBufSize, "worn %ld/%ld  beat %lu spo2 %lu",
                          static_cast<long>(s.lastTouchWorn),
                          static_cast<long>(s.lastTouchN),
                          static_cast<unsigned long>(s.totalBeatN),
                          static_cast<unsigned long>(s.totalSpo2N));
        } else {
            std::snprintf(text[4], kLineBufSize, "beat %lu  spo2 %lu",
                          static_cast<unsigned long>(s.totalBeatN),
                          static_cast<unsigned long>(s.totalSpo2N));
        }

        // The charger is not a detail here: plugging in terminates every app
        // on the device, so a probe night on the cable records nothing at all.
        if (s.usb == 1 || s.charging == 1) {
            std::snprintf(text[5], kLineBufSize, "UNPLUG USB TO RECORD");
        } else if (s.battPctX10 >= 0) {
            std::snprintf(text[5], kLineBufSize, "batt %ld.%ld%%",
                          static_cast<long>(s.battPctX10 / 10),
                          static_cast<long>(s.battPctX10 % 10));
        }
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

    // Read-only by design. There is nothing to command: the probe subscribes
    // what its config asked for and records until the app is removed, and a
    // button that could stop it would only add a way to lose a night.
    if (key == Btn::R2) {
        presenter->exit();
    }
}
