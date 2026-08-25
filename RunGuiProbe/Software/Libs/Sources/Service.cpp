/**
 ******************************************************************************
 * @file    Service.cpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The glance half: waits on its own card, then asks for a screen.
 ******************************************************************************
 */

#include "Service.hpp"

#include <cstring>

#define LOG_MODULE_PRX      "Probe"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/MessageTypes.hpp"

namespace
{

/// Matches APP_USER_NAME, so the carousel and the app list agree about what
/// this is called.
constexpr char kGlanceName[] = "RunGuiProbe";

/// Beside the app's own files, readable over USB. The GUI half writes
/// probe-gui.txt; see ProbeLog.hpp for why they are separate.
constexpr char kLogPath[] = "probe.txt";

/// The renderer builds its lines against its own constant so its tests need no
/// SDK header. This is where the two are held to each other.
static_assert(Probe::kLineBytes == GLANCE_TEXT_SIZE,
              "ProbePlan's line budget must be the SDK's text buffer size");

// -- Layout -------------------------------------------------------------------
//
// Two centred lines, sized from the area the kernel reports rather than placed
// at fixed pixels. The numbers come from SunGlance, which measured this panel on
// hardware: a font-20 line wants about 24 pixels and a font-18 line about 21,
// and centred text can span the full reported width because what the carousel's
// scroll indicator passes over is its margin rather than a glyph.
//
// There is no font-fitting ladder here on purpose. This app is not a layout
// experiment; if the panel is too short for two lines it says so in the log and
// declines, which is a better answer than a cut-off one.
constexpr int16_t kTopHeight    = 24;
constexpr int16_t kBottomHeight = 21;
constexpr int16_t kGap          = 4;
constexpr int16_t kNeededHeight = kTopHeight + kGap + kBottomHeight;

} // namespace

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mLog(kernel, kLogPath)
{
}

const char *Service::label(uint32_t type)
{
    switch (type) {
        case SDK::MessageType::EVENT_GLANCE_START:        return "glance-start";
        case SDK::MessageType::EVENT_GLANCE_TICK:         return "glance-tick";
        case SDK::MessageType::EVENT_GLANCE_STOP:         return "glance-stop";
        case SDK::MessageType::COMMAND_APP_STOP:          return "app-stop";
        case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN: return "gui-running";
        case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:return "gui-stopped";
        case SDK::MessageType::EVENT_BUTTON:              return "button";
        default:                                          return nullptr;
    }
}

void Service::run()
{
    LOG_INFO("started\n");
    mLog.line("--- service started ---");

    while (true) {
        SDK::MessageBase *msg = nullptr;

        if (!mKernel.comm.getMessage(msg)) {
            continue;
        }

        const uint32_t type = msg->getType();

        // Ticks are logged as a count at the end of the viewing rather than one
        // line each: at 60 Hz a single glance would otherwise fill the file.
        // Everything else is logged as it arrives, including types this app was
        // not expecting -- an unexpected message after the request is exactly
        // what would explain a surprising result.
        if (type != SDK::MessageType::EVENT_GLANCE_TICK) {
            const char *name = label(type);
            if (name != nullptr) {
                mLog.line("msg %s", name);
            } else {
                mLog.line("msg 0x%08X", static_cast<unsigned>(type));
            }
        }

        switch (type) {

            case SDK::MessageType::EVENT_GLANCE_START:
                // Each viewing is a fresh run of the experiment, including the
                // one after a launch was granted: the carousel restarts a
                // glance service every time the card is scrolled to, and a
                // result that only reproduces once is not much of a result.
                mPhase     = Probe::Phase::Waiting;
                mHaveStart = false;
                mTicks     = 0;
                mHaveShown = false;
                mLaunched  = false;

                if (!glanceConfig()) {
                    mLog.line("declined: glance area unusable");
                    LOG_WARNING("glance area unusable; declining\n");
                    mKernel.comm.releaseMessage(msg);
                    mKernel.sys.exit(0);
                    // exit() does not return on a watch. The return is for
                    // every other harness, where it does.
                    return;
                }
                glanceCreate();
                mActive = true;
                apply(Probe::linesFor(mPhase, 0));
                break;

            case SDK::MessageType::EVENT_GLANCE_TICK: {
                const auto *tick = static_cast<SDK::Message::EventGlanceTick *>(msg);
                ++mTicks;

                if (!mHaveStart) {
                    mStartMs   = tick->timestamp;
                    mHaveStart = true;
                    mLog.line("first tick at t=%u ms", static_cast<unsigned>(mStartMs));
                }

                const uint32_t elapsed = Probe::elapsedSince(mStartMs, tick->timestamp);

                if (Probe::shouldFire(elapsed, mPhase != Probe::Phase::Waiting)) {
                    fire();
                }

                apply(Probe::linesFor(mPhase, elapsed));
                push();
            } break;

            case SDK::MessageType::EVENT_GLANCE_STOP:
                mActive = false;
                mLog.line("viewing over after %u ticks, phase %s",
                          static_cast<unsigned>(mTicks), Probe::nameOf(mPhase));

                // The one place this app deliberately differs from every other
                // glance: after a granted launch, staying alive is part of the
                // experiment. See Service.hpp.
                if (mLaunched) {
                    mLog.line("staying alive: a launch was granted");
                    break;
                }

                LOG_INFO("stopped\n");
                mKernel.comm.releaseMessage(msg);
                return;

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                // The end of the interesting part: a GUI existed and has now
                // gone away. Staying past this would leave a glance service
                // parked on a queue with nothing left to observe, waiting for a
                // stop the kernel may never send.
                if (mLaunched) {
                    mLog.line("the gui has gone; leaving");
                    mKernel.comm.releaseMessage(msg);
                    return;
                }
                break;

            case SDK::MessageType::COMMAND_APP_STOP:
                mLog.line("--- service stopping ---");
                LOG_INFO("stopping\n");
                mKernel.comm.releaseMessage(msg);
                return;

            default:
                break;
        }

        mKernel.comm.releaseMessage(msg);
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

    if (gc->maxControls < kControlsNeeded) {
        return false;
    }
    if (gc->height < kNeededHeight || gc->width <= 0) {
        return false;
    }

    mGlance.setWidth(gc->width);
    mGlance.setHeight(gc->height);
    return true;
}

void Service::glanceCreate()
{
    if (mCreated) {
        // Second viewing in one process: the controls are still there and still
        // where they were put. Only the tracking of what they hold is cleared,
        // so the first tick re-sends both lines.
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

    // Deliberately not marking the form valid here. The controls were just
    // built, so they *are* invalid, and the first tick is what should discover
    // that and send them. Marking it valid at the end of building it is what
    // made SleepLab's glance send nothing for weeks.
    mHaveShown = false;
    mCreated   = true;
}

void Service::fire()
{
    // The experiment, in five lines.
    auto req = SDK::make_msg<SDK::Message::RequestAppRunGui>(mKernel);

    if (!req) {
        mPhase = Probe::Phase::NotSent;
        mLog.line("run-gui: could not allocate the message");
        return;
    }

    // A generous timeout: the kernel has to load a GUI ELF to answer this, and
    // a TIMEOUT here would be indistinguishable from a refusal at 100 ms.
    if (!req.send(2000)) {
        mPhase = Probe::Phase::NotSent;
        mLog.line("run-gui: send failed");
        return;
    }

    const auto result = req->getResult();
    if (result == SDK::MessageResult::SUCCESS) {
        mPhase    = Probe::Phase::Launched;
        mLaunched = true;
        mLog.line("run-gui: SUCCESS -- kernel accepted the request");
    } else {
        mPhase = Probe::Phase::Refused;
        mLog.line("run-gui: refused, result=%u (1=success 2=fail 3=timeout)",
                  static_cast<unsigned>(result));
    }

    LOG_INFO("run-gui -> %s\n", Probe::nameOf(mPhase));
}

void Service::apply(const Probe::Lines &lines)
{
    const bool fresh = !mHaveShown;

    if (fresh || std::strcmp(lines.top, mShown.top) != 0) {
        mTop.setText(lines.top);
    }
    if (fresh || std::strcmp(lines.bottom, mShown.bottom) != 0) {
        mBottom.setText(lines.bottom);
    }

    mShown     = lines;
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

    // After the send, and only here.
    mGlance.setValid();
}
