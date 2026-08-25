/**
 ******************************************************************************
 * @file    Service.cpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The glance half: can a service on the carousel see a button at all?
 ******************************************************************************
 */

#include "Service.hpp"

#include <cstdio>
#include <cstring>

#define LOG_MODULE_PRX      "BtnSvc"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/MessageTypes.hpp"

namespace
{

constexpr char kGlanceName[] = "ButtonProbe";
constexpr char kLogPath[]    = "button-glance.txt";

/// Present in the app's folder = hand the screen to the GUI on this viewing.
/// Absent = sample pins from the glance. See Service.hpp.
constexpr char kGuiMarker[] = "gui.on";

// Two centred lines, sized from the reported area. The constants come from
// SunGlance, which measured this panel: a font-20 line wants about 24 pixels
// and a font-18 line about 21.
constexpr int16_t kTopHeight    = 24;
constexpr int16_t kBottomHeight = 21;
constexpr int16_t kGap          = 4;
constexpr int16_t kNeededHeight = kTopHeight + kGap + kBottomHeight;

} // namespace

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mLog(kernel, kLogPath)
    , mSampler(kernel, mLog)
{
}

void Service::run()
{
    LOG_INFO("started\n");
    mLog.line("--- button probe glance ---");
    if (!Probe::Sampler::available()) {
        mLog.line("this build cannot read registers; nothing to measure");
    }

    while (true) {
        // Polled first, and on every turn of the loop including the timeouts,
        // because the timeouts are the majority of them. This is the whole
        // proposition being tested: that a glance service can sample fast
        // enough to catch a press without a tick to hang it on.
        if (mActive && !mGuiMode) {
            if (mSampler.poll() > 0) {
                refresh();
                push();
            }
        }

        if (mActive && mGuiMode && !mRequested
            && (mKernel.sys.getTimeMs() - mStartMs) >= kLaunchDwellMs) {
            requestGui();
            refresh();
            push();
        }

        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, Probe::Sampler::kPollMs)) {
            // Timed out with nothing to handle: go round and sample again.
            // Refreshed here as well so the card notices the end of the
            // calibration window, which is not signalled by any message.
            if (mActive) {
                refresh();
                push();
            }
            continue;
        }

        switch (msg->getType()) {

            case SDK::MessageType::EVENT_GLANCE_START:
                mHaveShown = false;
                mRequested = false;
                mLaunched  = false;
                mStartMs   = mKernel.sys.getTimeMs();
                mGuiMode   = mKernel.fs.exist(kGuiMarker);
                mLog.line("glance-start, %s mode",
                          mGuiMode ? "gui (gui.on present)" : "sampling");

                if (!glanceConfig()) {
                    mLog.line("declined: glance area unusable");
                    mKernel.comm.releaseMessage(msg);
                    mKernel.sys.exit(0);
                    return;
                }
                glanceCreate();
                mActive = true;
                refresh();
                break;

            case SDK::MessageType::EVENT_GLANCE_TICK:
                // Nothing hangs off the tick here on purpose -- it arrives about
                // once a second, and everything this app does needs to happen
                // far more often than that.
                break;

            case SDK::MessageType::EVENT_GLANCE_STOP:
                mActive = false;
                mLog.line("glance-stop after %u edges", static_cast<unsigned>(mSampler.seen()));
                LOG_INFO("stopped\n");

                // A granted launch means a GUI of this app is coming up, and
                // exiting now could take it with it -- the lesson RunGuiProbe
                // was written to learn.
                if (mLaunched) {
                    mLog.line("staying alive: a launch was granted");
                    break;
                }

                mKernel.comm.releaseMessage(msg);
                return;

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                if (mLaunched) {
                    mLog.line("the gui has gone; leaving");
                    mKernel.comm.releaseMessage(msg);
                    return;
                }
                break;

            case SDK::MessageType::COMMAND_APP_STOP:
                mLog.line("--- app-stop ---");
                mKernel.comm.releaseMessage(msg);
                return;

            case SDK::MessageType::EVENT_BUTTON:
                // Not expected to arrive: EVENT_BUTTON is documented as going to
                // a GUI, and the carousel owns the buttons while a card is up.
                // Logged because if it ever *did* arrive, none of this app would
                // be necessary and that would be the most important line in the
                // file.
                mLog.line("EVENT_BUTTON reached the glance service");
                break;

            default:
                break;
        }

        mKernel.comm.releaseMessage(msg);

        if (mActive) {
            push();
        }
    }
}

void Service::requestGui()
{
    mRequested = true;

    auto req = SDK::make_msg<SDK::Message::RequestAppRunGui>(mKernel);
    if (!req || !req.send(2000)) {
        mLog.line("run-gui: could not send");
        return;
    }

    if (req->getResult() == SDK::MessageResult::SUCCESS) {
        mLaunched = true;
        mLog.line("run-gui: SUCCESS");
    } else {
        mLog.line("run-gui: refused, result=%u",
                  static_cast<unsigned>(req->getResult()));
    }
}

bool Service::glanceConfig()
{
    auto gc = SDK::make_msg<SDK::Message::RequestGlanceConfig>(mKernel);
    if (!gc || !gc.send(100) || !gc.ok()) {
        mLog.line("glance config request failed");
        return false;
    }

    mLog.line("glance area %dx%d, %u controls",
              static_cast<int>(gc->width), static_cast<int>(gc->height),
              static_cast<unsigned>(gc->maxControls));

    if (gc->maxControls < kControlsNeeded || gc->height < kNeededHeight || gc->width <= 0) {
        return false;
    }

    mGlance.setWidth(gc->width);
    mGlance.setHeight(gc->height);
    return true;
}

void Service::glanceCreate()
{
    if (mCreated) {
        mHaveShown = false;
        return;
    }

    const uint16_t width = mGlance.getWidth();
    const int16_t  top   = static_cast<int16_t>((mGlance.getHeight() - kNeededHeight) / 2);

    mTop = mGlance.createText();
    mTop.pos({ 0, static_cast<uint16_t>(top) },
             { width, static_cast<uint16_t>(kTopHeight) })
        .font(GlanceFont_t::GLANCE_FONT_POPPINS_SEMIBOLD_20)
        .color(GlanceColor_t::GLANCE_COLOR_WHITE)
        .setText("")
        .alignment(GlanceAlignH_t::GLANCE_ALIGN_H_CENTER);

    mBottom = mGlance.createText();
    mBottom.pos({ 0, static_cast<uint16_t>(top + kTopHeight + kGap) },
                { width, static_cast<uint16_t>(kBottomHeight) })
        .font(GlanceFont_t::GLANCE_FONT_POPPINS_SEMIBOLD_18)
        .color(GlanceColor_t::GLANCE_COLOR_GRAY)
        .setText("")
        .alignment(GlanceAlignH_t::GLANCE_ALIGN_H_CENTER);

    mHaveShown = false;
    mCreated   = true;
}

void Service::refresh()
{
    char top[GLANCE_TEXT_SIZE];
    char bottom[GLANCE_TEXT_SIZE];

    if (!Probe::Sampler::available()) {
        std::snprintf(top, sizeof top, "no registers");
        std::snprintf(bottom, sizeof bottom, "host build");
    } else if (mGuiMode) {
        // "%s" rather than the chosen string as the format: a non-literal
        // format is a warning at best and a hole at worst.
        std::snprintf(top, sizeof top, "%s", mLaunched ? "opening gui" : "gui.on set");
        std::snprintf(bottom, sizeof bottom, "%s",
                      mRequested ? (mLaunched ? "screen incoming" : "kernel refused")
                                 : "hold for the gui");
    } else if (mSampler.calibrating()) {
        std::snprintf(top, sizeof top, "calibrating");
        std::snprintf(bottom, sizeof bottom, "hands off");
    } else {
        std::snprintf(top, sizeof top, "edges: %u", static_cast<unsigned>(mSampler.seen()));
        const char *last = mSampler.last();
        std::snprintf(bottom, sizeof bottom, "%s", (last[0] != '\0') ? last : "press a button");
    }

    const bool fresh = !mHaveShown;

    // Only the line that changed, because setText() invalidates whether or not
    // the string differs -- and this loop goes round more than a hundred times a
    // second.
    if (fresh || std::strcmp(top, mShownTop) != 0) {
        mTop.setText(top);
        std::snprintf(mShownTop, sizeof mShownTop, "%s", top);
    }
    if (fresh || std::strcmp(bottom, mShownBottom) != 0) {
        mBottom.setText(bottom);
        std::snprintf(mShownBottom, sizeof mShownBottom, "%s", bottom);
    }

    mHaveShown = true;
}

void Service::push()
{
    if (!mActive || !mGlance.isInvalid()) {
        return;
    }

    if (auto upd = SDK::make_msg<SDK::Message::RequestGlanceUpdate>(mKernel)) {
        upd->name           = kGlanceName;
        upd->controls       = mGlance.data();
        upd->controlsNumber = static_cast<uint32_t>(mGlance.size());
        upd.send(100);
    }

    mGlance.setValid();
}
