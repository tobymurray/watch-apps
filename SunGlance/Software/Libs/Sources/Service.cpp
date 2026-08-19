/**
 ******************************************************************************
 * @file    Service.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The glance: the only part of this app that touches the SDK.
 ******************************************************************************
 */

#include "Service.hpp"

#include <cstring>
#include <ctime>

#include "WallClock.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"

namespace
{

/// The name the glance registers under. Matches APP_USER_NAME, so the carousel
/// and the app list agree about what this is called.
constexpr char kGlanceName[] = "Sun";

/// The renderer builds its lines against its own constant so its tests need no
/// SDK header. This is where the two are held to each other.
static_assert(Sun::kLineBytes == GLANCE_TEXT_SIZE,
              "Render.hpp's line budget must be the SDK's text buffer size");

} // namespace

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mHome(kernel)
{
}

void Service::run()
{
    LOG_INFO("started\n");

    while (true) {
        SDK::MessageBase *msg = nullptr;

        if (!mKernel.comm.getMessage(msg)) {
            continue;
        }

        switch (msg->getType()) {

            case SDK::MessageType::EVENT_GLANCE_START:
                if (!glanceConfig()) {
                    LOG_WARNING("glance area is too small; declining\n");
                    mKernel.comm.releaseMessage(msg);
                    mKernel.sys.exit(0);
                    // exit() does not return on a watch. The return is for
                    // every other harness, where it does: falling through here
                    // would build controls into a form with no dimensions.
                    return;
                }
                glanceCreate();
                // Once per viewing is the right cadence for a file: the
                // carousel restarts this service every time the glance is
                // scrolled to, so this is fresher than any polling interval
                // would be, and it costs one stat rather than sixty a second.
                mHome.refresh();
                mScheduleDay = INT64_MIN;
                mActive      = true;
                update();
                break;

            case SDK::MessageType::EVENT_GLANCE_TICK:
                update();
                push();
                break;

            case SDK::MessageType::COMMAND_APP_STOP:
            case SDK::MessageType::EVENT_GLANCE_STOP:
                LOG_INFO("stopped\n");
                mActive = false;
                // Released here because this is the last message handled.
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
        return false;
    }

    if (gc->maxControls < kControlsNeeded) {
        LOG_WARNING("glance offers %u controls, need %u\n",
                    static_cast<unsigned>(gc->maxControls),
                    static_cast<unsigned>(kControlsNeeded));
        return false;
    }

    mGlance.setWidth(gc->width);
    mGlance.setHeight(gc->height);
    return true;
}

void Service::glanceCreate()
{
    const uint16_t width = mGlance.getWidth();

    mTitle = mGlance.createText();
    mTitle.pos({ 0, kTitleY }, { width, kTitleH })
        .font(GlanceFont_t::GLANCE_FONT_POPPINS_SEMIBOLD_20)
        .color(GlanceColor_t::GLANCE_COLOR_TEAL)
        .setText("sun")
        .alignment(GlanceAlignH_t::GLANCE_ALIGN_H_CENTER);

    mValue = mGlance.createText();
    mValue.pos({ 0, kValueY }, { width, kValueH })
        .font(GlanceFont_t::GLANCE_FONT_POPPINS_SEMIBOLD_30)
        .color(GlanceColor_t::GLANCE_COLOR_WHITE)
        .setText("--")
        .alignment(GlanceAlignH_t::GLANCE_ALIGN_H_CENTER);

    mSub = mGlance.createText();
    mSub.pos({ 0, kSubY }, { width, kSubH })
        .font(GlanceFont_t::GLANCE_FONT_POPPINS_SEMIBOLD_18)
        .color(GlanceColor_t::GLANCE_COLOR_GRAY)
        .setText("")
        .alignment(GlanceAlignH_t::GLANCE_ALIGN_H_CENTER);

    // Deliberately not marking the form valid here. The controls were just
    // built, so they *are* invalid, and the first tick is what should discover
    // that and send them.
    mHaveShown = false;
}

Sun::Clock Service::clockAt(int64_t utc)
{
    Sun::Clock clock;
    if (utc < 0) {
        return clock;
    }

    std::tm     local {};
    std::time_t t = static_cast<std::time_t>(utc);
    if (localtime_r(&t, &local) == nullptr) {
        return clock;
    }

    clock.hour   = local.tm_hour;
    clock.minute = local.tm_min;
    clock.valid  = true;
    return clock;
}

Sun::View Service::compose(int64_t nowUtc)
{
    Sun::View view;

    if (nowUtc <= 0) {
        // The one wall-clock read in the app, and its failure has its own
        // screen: every time on this glance is derived from it, so a clock that
        // is not set makes all of them meaningless rather than approximate.
        view.trouble = Sun::Trouble::NoClock;
        return view;
    }

    std::tm     local {};
    std::time_t t = static_cast<std::time_t>(nowUtc);
    if (localtime_r(&t, &local) == nullptr) {
        view.trouble = Sun::Trouble::NoClock;
        return view;
    }

    switch (mHome.status()) {
        case Sun::HomeConfig::Status::Absent:
            view.trouble = Sun::Trouble::NoPosition;
            return view;
        case Sun::HomeConfig::Status::Rejected:
            view.trouble = Sun::Trouble::BadConfig;
            return view;
        case Sun::HomeConfig::Status::Ok:
            break;
    }

    const Sun::Fix &fix = mHome.fix();

    const int year  = local.tm_year + 1900;
    const int month = local.tm_mon + 1;
    const int day   = local.tm_mday;

    const int32_t offset = Sun::utcOffsetFromCivil(year, month, day,
                                                   local.tm_hour, local.tm_min, local.tm_sec,
                                                   nowUtc);
    const int64_t localDay = Sun::daysFromCivil(year, month, day);

    if (localDay != mScheduleDay) {
        mToday = Sun::forLocalDay(year, month, day, offset, fix.position());

        int tYear = 0, tMonth = 0, tDay = 0;
        Sun::civilFromDays(localDay + 1, tYear, tMonth, tDay);
        mTomorrow = Sun::forLocalDay(tYear, tMonth, tDay, offset, fix.position());

        mScheduleDay = localDay;
        LOG_INFO("schedule for %04d-%02d-%02d\n", year, month, day);
    }

    const Sun::Next next = Sun::nextEvent(nowUtc, mToday, mTomorrow);

    view.kind        = next.kind;
    view.nextDay     = next.nextDay;
    view.when        = clockAt(next.whenUtc);
    view.other       = clockAt(next.otherUtc);
    view.secondsAway = (next.whenUtc >= 0) ? next.whenUtc - nowUtc : -1;
    view.zoneSuspect = !Sun::zoneAgreesWithLongitude(fix.lonDeg, offset);

    return view;
}

void Service::update()
{
    if (!mActive) {
        return;
    }

    const int64_t now = Sun::wallClockUtc();
    if (now == mLastSecond && mHaveShown) {
        return;
    }
    mLastSecond = now;

    apply(Sun::render(compose(now)));
}

void Service::apply(const Sun::Lines &lines)
{
    const bool first = !mHaveShown;

    if (first || std::strcmp(lines.title, mShown.title) != 0) {
        mTitle.setText(lines.title);
    }
    if (first || std::strcmp(lines.value, mShown.value) != 0) {
        mValue.setText(lines.value);
    }
    if (first || std::strcmp(lines.sub, mShown.sub) != 0) {
        mSub.setText(lines.sub);
    }
    if (first || lines.caution != mShown.caution) {
        // Amber for a caption that is a caveat, grey for one that is a
        // convenience. Colour is set only when it changes for the same reason
        // the text is: it invalidates the control either way.
        mSub.color(lines.caution ? GlanceColor_t::GLANCE_COLOR_YELLOW_DARK
                                 : GlanceColor_t::GLANCE_COLOR_GRAY);
    }

    mShown     = lines;
    mHaveShown = true;
}

void Service::push()
{
    if (!mGlance.isInvalid()) {
        return;
    }

    if (auto upd = SDK::make_msg<SDK::Message::RequestGlanceUpdate>(mKernel)) {
        upd->name           = kGlanceName;
        upd->controls       = mGlance.data();
        upd->controlsNumber = static_cast<uint32_t>(mGlance.size());
        upd.send(100);
    }

    // After the send, and only here. Marking the form valid at the end of
    // building it is what made SleepLab's glance send nothing: every tick found
    // a form that was already clean.
    mGlance.setValid();
}
