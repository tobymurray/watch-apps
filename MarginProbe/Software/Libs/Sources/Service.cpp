/**
 ******************************************************************************
 * @file    Service.cpp
 * @date    30-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The whole app: a rectangle, two labels, and a note about both.
 ******************************************************************************
 */

#include "Service.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#define LOG_MODULE_PRX      "Probe"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"

namespace
{

/// The name the glance registers under, and the folder it lives in. Set from
/// APP_NAME so the CMakeLists is the only place the number appears.
constexpr char kGlanceName[] = APP_NAME;

/// Beside the .uapp, in the app's own folder, readable over USB.
constexpr char kNotePath[] = "probe.txt";

/// Poppins at 18 for the identifier and 10 for the geometry. Deliberately not
/// larger: a big label would be the thing you look at, and the thing you look
/// at here is the border.
constexpr GlanceFont_t kLabelFont = GlanceFont_t::GLANCE_FONT_POPPINS_SEMIBOLD_18;
constexpr GlanceFont_t kSubFont   = GlanceFont_t::GLANCE_FONT_POPPINS_MEDIUM_10;

/// The 1.2 line-height ratio SunGlance measured on this watch, as integers.
int32_t lineHeightFor(int32_t fontPx)
{
    return (fontPx * 6) / 5;
}

GlancePoint_t pointOf(int32_t x, int32_t y)
{
    return GlancePoint_t { static_cast<uint16_t>(x), static_cast<uint16_t>(y) };
}

GlanceSize_t sizeOf(int32_t w, int32_t h)
{
    return GlanceSize_t { static_cast<uint16_t>(w), static_cast<uint16_t>(h) };
}

#ifdef PROBE_PROOF
/// The leftmost drawable column in row @p y, per the model four rounds of
/// probing arrived at: the band's own inscribed circle. The right edge is the
/// mirror, which is itself part of the claim -- every round measured the two
/// sides separately and they agreed.
int32_t edgeFor(int32_t y, int32_t w, int32_t h)
{
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    const float r  = w * 0.5f;
    const float dy = (static_cast<float>(y) + 0.5f) - cy;
    const float d2 = r * r - dy * dy;
    if (d2 <= 0.0f) {
        return static_cast<int32_t>(cx);
    }
    // -1.0f, not -0.5f. The -0.5f version tests whether the pixel's *centre*
    // falls inside the circle; this tests whether any part of it does. Round 5
    // shipped the centre rule and the panel refuted it: the mid-grey ring, drawn
    // one pixel outside and predicted to vanish, was visible on part of the arc.
    // Not all of it -- the two rules differ on 22 of the 60 rows, which is
    // exactly the "some pieces" that was reported. A pixel lights when the
    // circle clips it at all, not when it swallows its centre.
    return static_cast<int32_t>(ceilf(cx - sqrtf(d2) - 1.0f));
}
#endif

} // namespace

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
{
}

void Service::run()
{
    LOG_INFO("%s started, inset %d\n", kGlanceName, static_cast<int>(kInset));

    while (true) {
        SDK::MessageBase *msg = nullptr;

        if (!mKernel.comm.getMessage(msg)) {
            continue;
        }

        switch (msg->getType()) {

            case SDK::MessageType::EVENT_GLANCE_START:
                if (!glanceConfig()) {
                    // The only way to get here is the kernel refusing to say
                    // how big the glance is, which is not a measurement -- it
                    // is a broken request. Nothing useful can be drawn.
                    LOG_WARNING("no glance config; nothing to probe\n");
                    mKernel.comm.releaseMessage(msg);
                    mKernel.sys.exit(0);
                    return;
                }
                glanceCreate();
                note();
                break;

            case SDK::MessageType::EVENT_GLANCE_TICK:
                // Nothing on this screen changes, so there is nothing to
                // recompute -- but the form still has to be sent once, and the
                // first tick after START is what does it.
                push();
                break;

            case SDK::MessageType::COMMAND_APP_STOP:
            case SDK::MessageType::EVENT_GLANCE_STOP:
                LOG_INFO("stopped\n");
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

    mGlance.setWidth(gc->width);
    mGlance.setHeight(gc->height);
    mMaxControls = gc->maxControls;

    LOG_INFO("area %dx%d, %u controls, probing inset %d\n",
             static_cast<int>(gc->width), static_cast<int>(gc->height),
             static_cast<unsigned>(gc->maxControls), static_cast<int>(kInset));

    if (gc->maxControls < kControlsNeeded) {
        // Reported rather than refused. A kernel that will not grant three
        // controls is itself a finding, and the note() below is where it is
        // recorded; the rectangle simply will not appear.
        LOG_WARNING("only %u controls offered, need %u\n",
                    static_cast<unsigned>(gc->maxControls),
                    static_cast<unsigned>(kControlsNeeded));
    }

    return true;
}

void Service::glanceCreate()
{
    const int32_t w = static_cast<int32_t>(mGlance.getWidth());
    const int32_t h = static_cast<int32_t>(mGlance.getHeight());

    // Clamped so an inset larger than half the panel degenerates to a line
    // rather than wrapping through zero into an enormous unsigned size. At
    // 240x60 the largest inset in the series is 20, so this never fires on this
    // watch -- it fires on the next one, which is the point of measuring.
    mRectX = kInset;
    mRectY = kInset;
    mRectW = (w - 2 * kInset > 1) ? (w - 2 * kInset) : 1;
    mRectH = (h - 2 * kInset > 1) ? (h - 2 * kInset) : 1;

#ifdef PROBE_PROOF
    // Outside first, then the boundary, then inside -- so each covers the last
    // everywhere except the single pixel where they disagree, and what survives
    // is the prediction. See the header for what each outcome would mean.
    struct Pass { int32_t delta; uint8_t colour; int32_t trim; };
    const Pass passes[] = {
        { -1, GlanceColor_t::GLANCE_COLOR_GRAY,      0 },  // expected to vanish
        {  0, GlanceColor_t::GLANCE_COLOR_WHITE,     0 },  // expected: unbroken ring
        { +1, GlanceColor_t::GLANCE_COLOR_GRAY_DARK, 1 },  // the field it sits on
    };

    // Two spare for the label and the sub-line. A form built past what the
    // kernel granted would be a silent truncation, and this app of all of them
    // must not quietly draw something other than what it claims.
    const size_t budget = (mMaxControls > 2) ? (mMaxControls - 2) : 0;

    for (const Pass &pass : passes) {
        int32_t y = pass.trim;
        while (y < h - pass.trim && mGlance.size() < budget) {
            const int32_t xl = edgeFor(y, w, h) + pass.delta;

            int32_t yEnd = y + 1;
            while (yEnd < h - pass.trim && edgeFor(yEnd, w, h) + pass.delta == xl) {
                yEnd++;
            }

            // Clamped, not skipped. Across the middle rows the outside pass asks
            // for x = -1, which is off the band; clamping puts it under the
            // boundary pass, which then paints over it. Nothing false is shown.
            const int32_t a = (xl < 0) ? 0 : xl;
            const int32_t b = (w - 1 - xl > w - 1) ? (w - 1) : (w - 1 - xl);
            if (b >= a) {
                SDK::Glance::ControlRectangle band = mGlance.createRect();
                band.init(pointOf(a, y), sizeOf(b - a + 1, yEnd - y),
                          pass.colour, pass.colour, true);
            }
            y = yEnd;
        }
    }
#elif defined(PROBE_ARC)
    // The whole band, mid grey. Everything this probe measures is a departure
    // from this level: black where the glass cut it away, white where the
    // carousel painted its scroll arc on top.
    SDK::Glance::ControlRectangle fill = mGlance.createRect();
    fill.init(pointOf(0, 0), sizeOf(w, h),
              GlanceColor_t::GLANCE_COLOR_GRAY,
              GlanceColor_t::GLANCE_COLOR_GRAY, true);

    // Everything from here down is BLACK on that grey. Black is the one value
    // the arc cannot wash out and cannot be mistaken for, so the ruler stays
    // readable exactly where the thing being measured is brightest.
    for (size_t i = 0; i < kTickCount; i++) {
        const int32_t c = kTicks[i];
        if (c >= w / 2) {
            continue;
        }
        SDK::Glance::ControlRectangle left = mGlance.createRect();
        left.init(pointOf(c, 0), sizeOf(1, h),
                  GlanceColor_t::GLANCE_COLOR_BLACK,
                  GlanceColor_t::GLANCE_COLOR_BLACK, true);

        SDK::Glance::ControlRectangle right = mGlance.createRect();
        right.init(pointOf(w - 1 - c, 0), sizeOf(1, h),
                   GlanceColor_t::GLANCE_COLOR_BLACK,
                   GlanceColor_t::GLANCE_COLOR_BLACK, true);
    }

    SDK::Glance::ControlRectangle datum = mGlance.createRect();
    datum.init(pointOf(0, h / 2), sizeOf(w, 1),
               GlanceColor_t::GLANCE_COLOR_BLACK,
               GlanceColor_t::GLANCE_COLOR_BLACK, true);
#elif defined(PROBE_RULER)
    // Six adjacent 1px columns from each edge, one colour per display pixel.
    // Adjacent and not spaced: they are meant to read as one striped block whose
    // outer end is the answer, and a gap between them would be one more thing to
    // mistake for a missing stripe.
    for (size_t i = 0; i < kRulerCount; i++) {
        const int32_t c = static_cast<int32_t>(i);
        if (c >= w / 2) {
            continue;
        }
        SDK::Glance::ControlRectangle left = mGlance.createRect();
        left.init(pointOf(c, 0), sizeOf(1, h), kRuler[i], kRuler[i], true);

        SDK::Glance::ControlRectangle right = mGlance.createRect();
        right.init(pointOf(w - 1 - c, 0), sizeOf(1, h), kRuler[i], kRuler[i], true);
    }

    // Grey, so it cannot be mistaken for one of the six.
    SDK::Glance::ControlRectangle datum = mGlance.createRect();
    datum.init(pointOf(0, h / 2), sizeOf(w, 1),
               GlanceColor_t::GLANCE_COLOR_GRAY,
               GlanceColor_t::GLANCE_COLOR_GRAY, true);
#elif defined(PROBE_STAIR)
    // A full-height bar standing in each sampled column, on both edges. Each is
    // clipped to the chord at its own x, so the set of them traces the boundary
    // and the reader counts bars rather than measuring one.
    for (size_t i = 0; i < kColCount; i++) {
        const int32_t c = kCols[i];
        if (c >= w / 2) {
            continue;
        }
        SDK::Glance::ControlRectangle left = mGlance.createRect();
        left.init(pointOf(c, 0), sizeOf(1, h),
                  GlanceColor_t::GLANCE_COLOR_WHITE,
                  GlanceColor_t::GLANCE_COLOR_WHITE, true);

        SDK::Glance::ControlRectangle right = mGlance.createRect();
        right.init(pointOf(w - 1 - c, 0), sizeOf(1, h),
                   GlanceColor_t::GLANCE_COLOR_WHITE,
                   GlanceColor_t::GLANCE_COLOR_WHITE, true);
    }

    // A datum across the middle. It is the one line whose full width is known to
    // be drawable -- the box series showed a horizontal edge surviving even at
    // inset 0 -- so it doubles as a check that the band really is 240 wide and
    // as the centre the bars should be symmetric about.
    SDK::Glance::ControlRectangle datum = mGlance.createRect();
    datum.init(pointOf(0, h / 2), sizeOf(w, 1),
               GlanceColor_t::GLANCE_COLOR_GRAY,
               GlanceColor_t::GLANCE_COLOR_GRAY, true);
#else
    // Unfilled, so what lands on the panel is an outline and every one of its
    // four sides can be inspected separately. A filled rectangle would say only
    // "something was drawn"; the whole question here is *which edge* survives.
    SDK::Glance::ControlRectangle border = mGlance.createRect();
    border.init(pointOf(mRectX, mRectY), sizeOf(mRectW, mRectH),
                 GlanceColor_t::GLANCE_COLOR_WHITE,
                 GlanceColor_t::GLANCE_COLOR_BLACK,
                 false);
#endif

    // The two labels are centred across the *full* reported width, not inside
    // the border. At the wide end of the series the border would not hold them,
    // and a label that moves with the inset would be one more thing to hold in
    // your head while comparing two cards. Fixed position, moving border.
    const int32_t labelH = lineHeightFor(18);
    const int32_t subH   = lineHeightFor(10);
    const int32_t stack  = labelH + subH;
    const int32_t top    = (h - stack > 0) ? (h - stack) / 2 : 0;

#if defined(PROBE_ARC) || defined(PROBE_PROOF)
    // Black. In the arc style because black is the one value the scroll arc
    // cannot wash out; in the proof style because white is the ring and nothing
    // else on the panel may be white.
    constexpr uint8_t kInk = GlanceColor_t::GLANCE_COLOR_BLACK;
    constexpr uint8_t kInkDim = GlanceColor_t::GLANCE_COLOR_BLACK;
#else
    constexpr uint8_t kInk = GlanceColor_t::GLANCE_COLOR_WHITE;
    constexpr uint8_t kInkDim = GlanceColor_t::GLANCE_COLOR_GRAY;
#endif

    SDK::Glance::ControlText label = mGlance.createText();
    label.init(pointOf(0, top), sizeOf(w, labelH), kGlanceName, kLabelFont,
                kInk, GlanceAlignH_t::GLANCE_ALIGN_H_CENTER);

    // The geometry line makes a photograph self-describing: the card says what
    // the kernel claimed the area was and what this build asked for, so a
    // picture of it is evidence on its own rather than something that needs the
    // build it came from remembered alongside it.
    SDK::Glance::ControlText sub = mGlance.createText();
    sub.init(pointOf(0, top + labelH), sizeOf(w, subH), "", kSubFont,
              kInkDim, GlanceAlignH_t::GLANCE_ALIGN_H_CENTER);
    // Digits and separators only. The box series rendered this same line at this
    // same size and every letter came back as '?' -- "240x60  inset 0  c32" read
    // as "240?60????????0???32" -- so Poppins Medium 10 carries no letter glyphs
    // here. The numbers were perfectly legible, so the fix is to stop sending
    // words rather than to grow the font: width-height-inset-maxControls.
    sub.print("%d-%d-%d-%u",
              static_cast<int>(w), static_cast<int>(h),
              static_cast<int>(kInset), static_cast<unsigned>(mMaxControls));
}

void Service::note()
{
    char text[256];
    const int len = snprintf(text, sizeof text,
                             "# what the kernel offered, and what was asked of it\n"
                             "name %s\n"
                             "inset %d\n"
                             "area %dx%d\n"
                             "maxControls %u\n"
                             "rect %d,%d %dx%d\n",
                             kGlanceName,
                             static_cast<int>(kInset),
                             static_cast<int>(mGlance.getWidth()),
                             static_cast<int>(mGlance.getHeight()),
                             static_cast<unsigned>(mMaxControls),
                             static_cast<int>(mRectX), static_cast<int>(mRectY),
                             static_cast<int>(mRectW), static_cast<int>(mRectH));

    if (len <= 0 || static_cast<size_t>(len) >= sizeof text) {
        return;
    }

    // Rewritten only when it changes, for the reason SunGlance gives: the
    // carousel restarts this service every time the card is scrolled to, and a
    // file rewritten on every viewing is a write cycle spent saying what it
    // already said.
    SDK::Interface::IFileSystem::ObjectInfo info {};
    if (mKernel.fs.objectInfo(kNotePath, info) && info.size == static_cast<size_t>(len)) {
        std::unique_ptr<SDK::Interface::IFile> existing = mKernel.fs.file(kNotePath);
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

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kNotePath);
    if (!file || !file->open(true, true)) {
        LOG_WARNING("could not write %s\n", kNotePath);
        return;
    }

    size_t written = 0;
    file->write(text, static_cast<size_t>(len), written);
    file->close();
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

    mGlance.setValid();
}
