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
#include <memory>

#include "WallClock.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"

// Generated from Resources/*.png by the SDK's png2abgr2222.py. Included here
// and nowhere else: the arrays are `static const` in the header, so every
// translation unit that included it would carry its own copy of 504 bytes.
#include "Icons.h"

namespace
{

/// The name the glance registers under. Matches APP_USER_NAME, so the carousel
/// and the app list agree about what this is called.
constexpr char kGlanceName[] = "Sun";

/// The renderer builds its lines against its own constant so its tests need no
/// SDK header. This is where the two are held to each other.
static_assert(Sun::kLineBytes == GLANCE_TEXT_SIZE,
              "Render.hpp's line budget must be the SDK's text buffer size");

/// Beside input.json, in the app's own folder, readable over USB. See
/// Service::noteGeometry().
constexpr char kGeometryPath[] = "glance.txt";

/// The renderer picks a size in pixels; the SDK names its fonts. One switch,
/// here, rather than a size the renderer cannot express.
GlanceFont_t rowFontFor(int16_t px)
{
    switch (px) {
        case 30: return GlanceFont_t::GLANCE_FONT_POPPINS_SEMIBOLD_30;
        case 25: return GlanceFont_t::GLANCE_FONT_POPPINS_SEMIBOLD_25;
        case 20: return GlanceFont_t::GLANCE_FONT_POPPINS_SEMIBOLD_20;
        default: return GlanceFont_t::GLANCE_FONT_POPPINS_SEMIBOLD_18;
    }
}

GlancePoint_t pointOf(const Sun::Box &box)
{
    return GlancePoint_t { static_cast<uint16_t>(box.x), static_cast<uint16_t>(box.y) };
}

GlanceSize_t sizeOf(const Sun::Box &box)
{
    return GlanceSize_t { static_cast<uint16_t>(box.w), static_cast<uint16_t>(box.h) };
}

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

    // Five controls gets the icons; three gets the words. Every SDK glance
    // example asks for three, so a kernel that offers only that is not a fault
    // to refuse -- it is a screen to draw differently. The rows can also turn
    // out too short to hold an icon, which bandsFor() decides.
    mLayout    = Sun::layoutFor(gc->width, gc->height,
                                gc->maxControls >= kControlsWanted,
                                ICON_SUNRISE_WIDTH, ICON_SUNRISE_HEIGHT);
    mWithIcons = mLayout.icons;

    LOG_INFO("glance %dx%d, %u controls -> %s font%d, icons %s\n",
             static_cast<int>(gc->width), static_cast<int>(gc->height),
             static_cast<unsigned>(gc->maxControls),
             mLayout.arrangement == Sun::Arrangement::SideBySide ? "side-by-side" : "stacked",
             static_cast<int>(mLayout.rowFontPx),
             mWithIcons ? "yes" : "no");

    // Written before the verdict below, on purpose: a panel too short to draw
    // in is exactly the case somebody needs the measurement for, and a glance
    // that declines silently tells them nothing.
    noteGeometry();

    if (!mLayout.fits) {
        LOG_WARNING("no arrangement of two times and a caption fits %dx%d\n",
                    static_cast<int>(gc->width), static_cast<int>(gc->height));
        return false;
    }

    return true;
}

void Service::glanceCreate()
{
    const uint16_t width = mGlance.getWidth();

    // Order matters only for the kernel's own draw order; the rows are placed
    // by layout(), which runs before anything is sent.
    if (mWithIcons) {
        mIconFirst = mGlance.createImage();
        mIconFirst.init({ 0, 0 }, { ICON_SUNRISE_WIDTH, ICON_SUNRISE_HEIGHT },
                        ICON_SUNRISE_ABGR2222);
    }

    mFirst = mGlance.createText();
    mFirst.font(rowFontFor(mLayout.rowFontPx))
        .color(GlanceColor_t::GLANCE_COLOR_WHITE)
        .setText("--");

    if (mWithIcons) {
        mIconSecond = mGlance.createImage();
        mIconSecond.init({ 0, 0 }, { ICON_SUNSET_WIDTH, ICON_SUNSET_HEIGHT },
                         ICON_SUNSET_ABGR2222);
    }

    mSecond = mGlance.createText();
    mSecond.font(rowFontFor(mLayout.rowFontPx))
        .color(GlanceColor_t::GLANCE_COLOR_WHITE)
        .setText("");

    mSub = mGlance.createText();
    mSub.pos(pointOf(mLayout.sub), sizeOf(mLayout.sub))
        .font(GlanceFont_t::GLANCE_FONT_POPPINS_SEMIBOLD_18)
        .color(GlanceColor_t::GLANCE_COLOR_GRAY)
        .setText("")
        .alignment(GlanceAlignH_t::GLANCE_ALIGN_H_CENTER);

    // Every control is created visible, so that is what the tracking starts at.
    mFirstIconKind   = Sun::EventKind::Rise;
    mIconFirstShown  = true;
    mIconSecondShown = true;
    mSecondShown     = true;
    mLaidOut         = false;

    // Deliberately not marking the form valid here. The controls were just
    // built, so they *are* invalid, and the first tick is what should discover
    // that and send them.
    mHaveShown = false;
}

void Service::layout(bool rowsMode)
{
    if (mLaidOut && rowsMode == mRowsMode) {
        return;
    }

    const GlanceAlignH_t rowAlign = mLayout.textCentred
                                        ? GlanceAlignH_t::GLANCE_ALIGN_H_CENTER
                                        : GlanceAlignH_t::GLANCE_ALIGN_H_LEFT;

    if (rowsMode) {
        mFirst.pos(pointOf(mLayout.textFirst), sizeOf(mLayout.textFirst)).alignment(rowAlign);
        mSecond.pos(pointOf(mLayout.textSecond), sizeOf(mLayout.textSecond)).alignment(rowAlign);

        if (mWithIcons) {
            mIconFirst.pos(pointOf(mLayout.iconFirst));
            mIconSecond.pos(pointOf(mLayout.iconSecond));
        }
    } else {
        // One line instead of two times: it takes the whole row area, centred,
        // because it is a sentence rather than a number sitting beside a
        // picture.
        mFirst.pos(pointOf(mLayout.message), sizeOf(mLayout.message))
            .alignment(GlanceAlignH_t::GLANCE_ALIGN_H_CENTER);
    }

    mRowsMode = rowsMode;
    mLaidOut  = true;
}

void Service::noteGeometry()
{
    // The first version of this app hard-coded its bands and clipped a row on
    // the watch, and there was no way to find out what the panel actually was
    // without another build. This is that way: one file beside input.json,
    // readable over USB, saying what the kernel offered and what was made of
    // it. It is a measurement, and every layout decision after this one is
    // taken against it instead of against SleepLab's numbers.
    char text[256];
    const int len = snprintf(text, sizeof text,
                             "# what the kernel offered, and what was drawn from it\n"
                             "area %dx%d\n"
                             "%s font%d icons %s fits %s\n"
                             "first %d,%d %dx%d  second %d,%d %dx%d\n"
                             "sub %d,%d %dx%d\n",
                             static_cast<int>(mGlance.getWidth()),
                             static_cast<int>(mGlance.getHeight()),
                             mLayout.arrangement == Sun::Arrangement::SideBySide
                                 ? "side-by-side" : "stacked",
                             static_cast<int>(mLayout.rowFontPx),
                             mLayout.icons ? "yes" : "no",
                             mLayout.fits ? "yes" : "no",
                             static_cast<int>(mLayout.textFirst.x), static_cast<int>(mLayout.textFirst.y),
                             static_cast<int>(mLayout.textFirst.w), static_cast<int>(mLayout.textFirst.h),
                             static_cast<int>(mLayout.textSecond.x), static_cast<int>(mLayout.textSecond.y),
                             static_cast<int>(mLayout.textSecond.w), static_cast<int>(mLayout.textSecond.h),
                             static_cast<int>(mLayout.sub.x), static_cast<int>(mLayout.sub.y),
                             static_cast<int>(mLayout.sub.w), static_cast<int>(mLayout.sub.h));

    if (len <= 0 || static_cast<size_t>(len) >= sizeof text) {
        return;
    }

    // Rewritten only when it changes. The glance service starts every time the
    // card is scrolled to, and a file rewritten on every viewing would be a
    // write cycle spent saying what it already said.
    SDK::Interface::IFileSystem::ObjectInfo info {};
    if (mKernel.fs.objectInfo(kGeometryPath, info) && info.size == static_cast<size_t>(len)) {
        std::unique_ptr<SDK::Interface::IFile> existing = mKernel.fs.file(kGeometryPath);
        char   current[sizeof text] = { 0 };
        size_t read                 = 0;
        if (existing && existing->open()
            && existing->read(current, static_cast<size_t>(len), read)) {
            existing->close();
            if (read == static_cast<size_t>(len)
                && std::memcmp(current, text, static_cast<size_t>(len)) == 0) {
                return;
            }
        } else if (existing) {
            existing->close();
        }
    }

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kGeometryPath);
    if (!file || !file->open(true, true)) {
        // Not being able to write a note about the screen is no reason not to
        // draw the screen.
        LOG_WARNING("could not write %s\n", kGeometryPath);
        return;
    }

    size_t written = 0;
    file->write(text, static_cast<size_t>(len), written);
    file->close();
}

void Service::showControl(SDK::Glance::Control &control, bool shown, bool &state)
{
    if (state == shown) {
        return;
    }

    control.setVisible(shown);
    // setVisible() writes the flag and returns. Without this the form stays
    // clean and the control keeps being drawn as it was.
    control.invalidate();
    state = shown;
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
    view.first       = clockAt(next.whenUtc);
    view.second      = clockAt(next.secondUtc);
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

    apply(Sun::render(compose(now), mWithIcons));
}

void Service::apply(const Sun::Lines &lines)
{
    const bool fresh = !mHaveShown;

    layout(lines.rows);

    // The first row's control does double duty: it is a time when there are
    // rows and the whole message when there are not, so the string it should
    // hold comes from one place or the other.
    const char *headline = lines.rows ? lines.first : lines.message;
    const char *wasHeadline = mShown.rows ? mShown.first : mShown.message;

    if (fresh || std::strcmp(headline, wasHeadline) != 0) {
        mFirst.setText(headline);
    }
    if (fresh || std::strcmp(lines.second, mShown.second) != 0) {
        mSecond.setText(lines.second);
    }
    if (fresh || std::strcmp(lines.sub, mShown.sub) != 0) {
        mSub.setText(lines.sub);
    }
    if (fresh || lines.caution != mShown.caution) {
        // Amber for a caption that is a caveat, grey for one that is a
        // convenience. Colour is set only when it changes for the same reason
        // the text is: it invalidates the control either way.
        mSub.color(lines.caution ? GlanceColor_t::GLANCE_COLOR_YELLOW_DARK
                                 : GlanceColor_t::GLANCE_COLOR_GRAY);
    }

    // A second row with nothing in it is hidden rather than left empty: an
    // empty text control still occupies its band, and on the last day before
    // the midnight sun there is genuinely no second event.
    showControl(mSecond, lines.rows && lines.second[0] != '\0', mSecondShown);

    if (mWithIcons) {
        if (fresh || lines.firstKind != mFirstIconKind) {
            const bool rising = (lines.firstKind == Sun::EventKind::Rise);
            mIconFirst.setImage(rising ? ICON_SUNRISE_ABGR2222 : ICON_SUNSET_ABGR2222);
            mIconSecond.setImage(rising ? ICON_SUNSET_ABGR2222 : ICON_SUNRISE_ABGR2222);
            mFirstIconKind = lines.firstKind;
        }

        // The icons follow their rows: on a polar day, or a screen saying why
        // there is no time, there is no event for a picture to label.
        showControl(mIconFirst, lines.rows, mIconFirstShown);
        showControl(mIconSecond, lines.rows && lines.second[0] != '\0', mIconSecondShown);
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
