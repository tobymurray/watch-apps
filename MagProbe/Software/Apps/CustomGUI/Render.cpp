#include "Render.hpp"

#include "Fmt.hpp"
#include "Mag/Heading.hpp"

#include <cmath>

namespace Render {

namespace {

/// The display is round. Content starts below the top of the circle rather than
/// at the top of the framebuffer, because the first rows of a 240x240 square are
/// almost entirely masked: at y = 10 the visible width is under 90 px.
constexpr int32_t kTopY     = 40;
constexpr int32_t kHeaderY  = 26;
constexpr int32_t kLineStep = 9;

/// The left edge for one row of text, which follows the mask rather than being
/// a constant. See Canvas::safeLeft.
int32_t leftFor(const Canvas& c, int32_t y)
{
    const int32_t x = c.safeLeft(y, Canvas::textHeight(1));
    return (x < 0) ? 0 : x;
}

/// One line of "LABEL  value", with the label dim and the value bright, which
/// is legible at 5 px in a way that one uniform colour is not.
void row(Canvas& c, int32_t y, const char* label, const char* value, uint8_t valueColour)
{
    const int32_t after = c.text(leftFor(c, y), y, label, Canvas::kGrey);
    c.text(after + 5, y, value, valueColour);
}

void rowFixed(Canvas& c, int32_t y, const char* label, float v, uint8_t decimals,
              uint8_t colour = Canvas::kWhite)
{
    char buf[24];
    Fmt::fixed(buf, sizeof(buf), v, decimals);
    row(c, y, label, buf, colour);
}

void rowInt(Canvas& c, int32_t y, const char* label, int32_t v,
            uint8_t colour = Canvas::kWhite)
{
    char buf[16];
    Fmt::integer(buf, sizeof(buf), v);
    row(c, y, label, buf, colour);
}

/// Green once the answer is yes, red once it is a definite no, yellow while it
/// is still open. A probe that painted an unanswered question green would be
/// the worst thing on this screen.
uint8_t verdictColour(const View& v)
{
    switch (v.resolve) {
        case Mag::Resolve::NotAsked:   return Canvas::kYellow;
        case Mag::Resolve::NoProducer: return Canvas::kRed;
        case Mag::Resolve::Resolved:   break;
    }

    if (v.delivery == Mag::Delivery::Silent) {
        return Canvas::kRed;
    }
    if (v.delivery == Mag::Delivery::Stalled) {
        return Canvas::kYellow;
    }
    if (v.shape == Mag::FrameShape::Empty || v.shape == Mag::FrameShape::TooNarrow) {
        return Canvas::kRed;
    }
    if (v.units == Mag::Units::AllZero || v.units == Mag::Units::NonFinite) {
        return Canvas::kRed;
    }
    return Canvas::kGreen;
}

void header(Canvas& c, Screen screen)
{
    // Centred rather than left-aligned: the header is the narrowest row on the
    // screen, so it has the least room to spare on either side.
    char n[8];
    Fmt::integer(n, sizeof(n), static_cast<int32_t>(screen) + 1);

    const int32_t nameW  = Canvas::textWidth(name(screen), 1);
    const int32_t numW   = Canvas::textWidth(n, 1);
    const int32_t totalW = nameW + 6 + numW;
    const int32_t start  = (static_cast<int32_t>(c.width()) - totalW) / 2;

    const int32_t after = c.text(start, kHeaderY, name(screen), Canvas::kCyan);
    // Screen n of m, so it is obvious there are others behind the button.
    c.text(after + 6, kHeaderY, n, Canvas::kGrey);

    const int32_t ruleY    = kHeaderY + Canvas::textHeight(1) + 2;
    const int32_t ruleLeft = c.safeLeft(ruleY, 1);
    if (ruleLeft >= 0) {
        c.hLine(ruleLeft, ruleY, static_cast<int32_t>(c.width()) - 2 * ruleLeft, Canvas::kGrey);
    }
}

void drawVerdict(Canvas& c, const View& v)
{
    const Mag::Verdict verdict = Mag::verdict(v.resolve, v.delivery, v.shape);

    // The headline at the largest size that fits the row, because this one
    // string is the reason the app exists. Fitted rather than fixed at double
    // size: the display is round, so the width available on a row is not the
    // width of the framebuffer, and a headline that overflows is drawn off the
    // glass rather than wrapped.
    c.textCentredFitted(kTopY, verdict.headline, verdictColour(v), 2);
    c.textCentredFitted(kTopY + 18, verdict.reason, Canvas::kWhite, 1);

    int32_t y = kTopY + 32;
    row(c, y, "RESOLVE", Mag::name(v.resolve), Canvas::kWhite);
    y += kLineStep;
    row(c, y, "DELIVER", Mag::name(v.delivery), Canvas::kWhite);
    y += kLineStep;
    row(c, y, "FRAME", Mag::name(v.shape), Canvas::kWhite);
    y += kLineStep;
    row(c, y, "UNITS", Mag::name(v.units), Canvas::kWhite);
    y += kLineStep + 4;

    rowInt(c, y, "SAMPLES", static_cast<int32_t>(v.samples));
    y += kLineStep;
    rowInt(c, y, "AGE MS", static_cast<int32_t>(v.ageMs));
    y += kLineStep;

    // The rotation-invariance test, which is what turns a magnitude in a band
    // from a coincidence into evidence.
    if (v.spreadFraction >= 0.0f) {
        rowFixed(c, y, "SPREAD", v.spreadFraction * 100.0f, 1);
    } else {
        row(c, y, "SPREAD", "ROTATE ME", Canvas::kYellow);
    }
    y += kLineStep + 4;

    row(c, y, "ACCEL", Mag::name(v.accelResolve),
        v.accelResolve == Mag::Resolve::Resolved ? Canvas::kWhite : Canvas::kRed);
}

void drawFrame(Canvas& c, const View& v)
{
    int32_t y = kTopY;

    rowInt(c, y, "FIELDS", static_cast<int32_t>(v.fieldCount));
    y += kLineStep;
    rowInt(c, y, "STRIDE", static_cast<int32_t>(v.stride));
    y += kLineStep;
    rowInt(c, y, "BATCHES", static_cast<int32_t>(v.batches));
    y += kLineStep + 4;

    c.text(leftFor(c, y), y, "AS FLOAT", Canvas::kCyan);
    y += kLineStep;
    rowFixed(c, y, "X", v.raw.x, 2);
    y += kLineStep;
    rowFixed(c, y, "Y", v.raw.y, 2);
    y += kLineStep;
    rowFixed(c, y, "Z", v.raw.z, 2);
    y += kLineStep;
    rowFixed(c, y, "MAG", v.magnitude, 2);
    y += kLineStep + 4;

    // The same first field, not reinterpreted but re-read: the union does not
    // say which member the driver filled in.
    c.text(leftFor(c, y), y, "FIELD0 BITS", Canvas::kCyan);
    y += kLineStep;
    rowInt(c, y, "U32", static_cast<int32_t>(v.rawBits0));
    y += kLineStep;
    rowInt(c, y, "I32", v.rawInt0);
}

void drawCompass(Canvas& c, const View& v)
{
    const int32_t cx     = static_cast<int32_t>(c.width()) / 2;
    const int32_t cy     = 118;
    const int32_t radius = 56;

    // The dial: four cardinal ticks, so a needle has something to be read
    // against without a full rose costing pixels it cannot spare.
    for (int i = 0; i < 4; ++i) {
        const float    a  = static_cast<float>(i) * 90.0f * static_cast<float>(M_PI) / 180.0f;
        const int32_t  ox = static_cast<int32_t>(std::lround(std::sin(a) * radius));
        const int32_t  oy = static_cast<int32_t>(std::lround(-std::cos(a) * radius));
        c.fillRect(cx + ox - 1, cy + oy - 1, 3, 3, Canvas::kGrey);
    }
    c.text(cx - 2, cy - radius - 12, "N", Canvas::kGrey);

    if (!v.haveHeading) {
        const char* why = (v.accelResolve != Mag::Resolve::Resolved) ? "NO ACCEL"
                        : (v.delivery != Mag::Delivery::Delivering) ? "NO FIELD"
                                                                    : "NO HEADING";
        c.textCentredFitted(cy - 3, why, Canvas::kRed, 1);
        row(c, 196, "CAL", Mag::HardIron::name(v.calQuality), Canvas::kYellow);
        return;
    }

    // The needle points at magnetic north, which means it is drawn at minus the
    // heading: the heading is where the watch faces, and north is that many
    // degrees the other way around the dial.
    const float   rad = -v.headingDeg * static_cast<float>(M_PI) / 180.0f;
    const int32_t nx  = cx + static_cast<int32_t>(std::lround(std::sin(rad) * (radius - 6)));
    const int32_t ny  = cy - static_cast<int32_t>(std::lround(std::cos(rad) * (radius - 6)));
    const int32_t tx  = cx - static_cast<int32_t>(std::lround(std::sin(rad) * (radius - 26)));
    const int32_t ty  = cy + static_cast<int32_t>(std::lround(std::cos(rad) * (radius - 26)));

    const uint8_t needle = v.levelled ? Canvas::kRed : Canvas::kYellow;
    c.line(tx, ty, nx, ny, needle);
    c.line(tx + 1, ty, nx + 1, ny, needle);
    c.fillRect(cx - 2, cy - 2, 5, 5, Canvas::kWhite);

    char buf[16];
    Fmt::fixed(buf, sizeof(buf), v.headingDeg, 1);
    c.textCentredFitted(178, buf, Canvas::kWhite, 2);
    c.textCentredFitted(196, Mag::cardinal(v.headingDeg), Canvas::kCyan, 2);

    rowFixed(c, 214, "DIP", v.dipDeg, 1);

    if (!v.levelled) {
        c.text(leftFor(c, 224), 224, "MOVING", Canvas::kYellow);
    } else if (v.calQuality != Mag::HardIron::Quality::Usable) {
        c.text(leftFor(c, 224), 224, "UNCALIBRATED", Canvas::kYellow);
    }
}

void drawCalibration(Canvas& c, const View& v)
{
    int32_t y = kTopY;

    row(c, y, "STATE", v.calibrating ? "SWEEPING" : "IDLE",
        v.calibrating ? Canvas::kGreen : Canvas::kGrey);
    y += kLineStep;
    row(c, y, "QUALITY", Mag::HardIron::name(v.calQuality),
        v.calQuality == Mag::HardIron::Quality::Usable ? Canvas::kGreen : Canvas::kYellow);
    y += kLineStep;
    rowInt(c, y, "SAMPLES", static_cast<int32_t>(v.calSamples));
    y += kLineStep + 4;

    c.text(leftFor(c, y), y, "SPAN", Canvas::kCyan);
    y += kLineStep;
    rowFixed(c, y, "X", v.calSpans.x, 1);
    y += kLineStep;
    rowFixed(c, y, "Y", v.calSpans.y, 1);
    y += kLineStep;
    rowFixed(c, y, "Z", v.calSpans.z, 1);
    y += kLineStep + 4;

    c.text(leftFor(c, y), y, "OFFSET", Canvas::kCyan);
    y += kLineStep;
    rowFixed(c, y, "X", v.calOffsets.x, 1);
    y += kLineStep;
    rowFixed(c, y, "Y", v.calOffsets.y, 1);
    y += kLineStep;
    rowFixed(c, y, "Z", v.calOffsets.z, 1);
    y += kLineStep + 4;

    c.text(leftFor(c, y), y, v.calibrating ? "R1 STOP" : "R1 SWEEP", Canvas::kWhite);
}

} // namespace

const char* name(Screen screen)
{
    switch (screen) {
        case Screen::Verdict:     return "VERDICT";
        case Screen::Frame:       return "FRAME";
        case Screen::Compass:     return "COMPASS";
        case Screen::Calibration: return "CAL";
        case Screen::Count:       break;
    }
    return "VERDICT";
}

void render(Canvas& canvas, Screen screen, const View& view)
{
    if (!canvas.usable()) {
        return;
    }

    canvas.clear(Canvas::kBlack);
    header(canvas, screen);

    switch (screen) {
        case Screen::Verdict:     drawVerdict(canvas, view); break;
        case Screen::Frame:       drawFrame(canvas, view); break;
        case Screen::Compass:     drawCompass(canvas, view); break;
        case Screen::Calibration: drawCalibration(canvas, view); break;
        case Screen::Count:       break;
    }

    // A liveness marker positioned from the clock rather than from the frame
    // count, so a stalled loop freezes it and a slow one shows a visible jump
    // rather than a silently slower orbit. RustGuiPoc's reasoning, and its
    // period: one lap is a known 3.2 s.
    const int32_t markerY = static_cast<int32_t>(canvas.height()) - 34;
    const int32_t left    = canvas.safeLeft(markerY, 3);
    if (left >= 0) {
        const int32_t span = static_cast<int32_t>(canvas.width()) - 2 * left - 3;
        if (span > 0) {
            const int32_t pos = static_cast<int32_t>((view.uptimeMs % 3200u) *
                                                     static_cast<uint32_t>(span) / 3200u);
            canvas.fillRect(left + pos, markerY, 3, 3, Canvas::kGrey);
        }
    }
}

} // namespace Render
