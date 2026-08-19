#include "Cards.hpp"

#include "Palette.hpp"
#include "SceneRender.hpp"

namespace MapLab
{
namespace
{

/// The panel is round: a 240 px square's corners are not visible, and content
/// pushed into them cannot be judged. Cards keep their content inside this
/// inset, which is the same 30 px the text screens use.
constexpr int16_t kInset = 30;

void drawScene(Canvas& canvas, const uint8_t* buf, uint32_t bytes,
               Pt* scratch, int scratchCap, int16_t tilePx, int16_t ox, int16_t oy)
{
    canvas.clear(code(Slot::Paper));
    SceneReader r;
    if (!r.open(buf, bytes)) {
        // A card that cannot draw its subject says so in the only vocabulary
        // it has: a diagonal cross, which no map ever produces.
        canvas.thickLine(0, 0, 239, 239, 2, code(Slot::RoadMajor));
        canvas.thickLine(239, 0, 0, 239, 2, code(Slot::RoadMajor));
        return;
    }
    renderScene(r, canvas, scratch, scratchCap, ox, oy, tilePx);
}

/// A short GPS trace across the middle of the card, in the one code the
/// cartography reserves for it. Fixed rather than generated: this is a
/// legibility question, and a trace that moved between cards would make two
/// variants incomparable.
void drawTrace(Canvas& canvas)
{
    const Pt trace[] = {
        {  10, 190 }, {  46, 168 }, {  72, 172 }, {  98, 140 },
        { 120, 112 }, { 138,  74 }, { 168,  62 }, { 196,  70 }, { 228,  44 },
    };
    canvas.polyline(trace, static_cast<int>(sizeof(trace) / sizeof(trace[0])), 3,
                    code(Slot::Trace));
}

void cardPalette64(Canvas& canvas)
{
    canvas.clear(code(Slot::Paper));
    // 8 x 8 of 26 px cells with a 2 px paper gutter, filling the panel. The
    // code's own bits order the grid: row = blue+green, column = red+green, so
    // a neighbour differs by one quantum in one channel and the eye is being
    // asked whether one quantum looks like one step.
    constexpr int16_t cell = 26;
    constexpr int16_t x0   = (240 - 8 * cell) / 2;
    for (int i = 0; i < 64; ++i) {
        const uint8_t c = static_cast<uint8_t>(0xC0 | i);
        const int16_t cx = static_cast<int16_t>(x0 + (i % 8) * cell);
        const int16_t cy = static_cast<int16_t>(x0 + (i / 8) * cell);
        canvas.fillRect(cx, cy, cell - 2, cell - 2, c);
        // Notch the codes the cartography actually spends, so they can be
        // found on the panel without counting squares.
        for (const auto& s : kSlots) {
            if (s.code == c) {
                canvas.fillRect(cx, cy, 5, 5, code(Slot::Trace));
                break;
            }
        }
    }
}

void cardSlotRoles(Canvas& canvas)
{
    canvas.clear(code(Slot::Paper));
    // One band per slot, each 16 px, with 2 px of paper between: the question
    // is contrast against paper, so paper has to be adjacent to every band.
    const int n = static_cast<int>(Slot::Count);
    const int16_t bandH = 15;
    const int16_t top   = static_cast<int16_t>((240 - n * (bandH + 2)) / 2);
    for (int i = 0; i < n; ++i) {
        const int16_t y = static_cast<int16_t>(top + i * (bandH + 2));
        // Bands are narrower at the top and bottom of the panel: a round
        // display clips a full-width rect there, and a band that is partly
        // missing cannot be compared with one that is not.
        const int16_t inset = static_cast<int16_t>((i < 2 || i >= n - 2) ? 58 : kInset);
        canvas.fillRect(inset, y, static_cast<int16_t>(240 - 2 * inset), bandH,
                        kSlots[i].code);
    }
}

void cardLineWeights(Canvas& canvas)
{
    canvas.clear(code(Slot::Paper));
    // Left half: no casing. Right half: the spec's paper halo. Same weights,
    // same inks, so the halo's contribution is the only difference.
    const uint8_t inks[] = { code(Slot::RoadMajor), code(Slot::RoadMinor), code(Slot::Path) };
    int16_t y = 40;
    for (uint8_t ink : inks) {
        for (int16_t w = 1; w <= 4; ++w) {
            canvas.thickLine(34, y, 108, y, w, ink);
            // Cased: halo first, ink over it, at the spec's ratio (ink + 3).
            canvas.thickLine(132, y, 206, y, static_cast<int16_t>(w + 3), kHalo);
            canvas.thickLine(132, y, 206, y, w, ink);
            y = static_cast<int16_t>(y + 13);
        }
        y = static_cast<int16_t>(y + 4);
    }
    // Diagonals, where aliasing is worst and a 1 px line either survives or
    // does not.
    canvas.thickLine(34, 210, 100, 150, 1, code(Slot::Contour));
    canvas.thickLine(70, 210, 136, 150, 2, code(Slot::RoadMinor));
    canvas.thickLine(106, 210, 172, 150, 4, code(Slot::RoadMajor));
}

void cardDashes(Canvas& canvas)
{
    canvas.clear(code(Slot::Paper));
    // Four cycles at the spec's 2 px path weight, plus the same cycles at 3 px
    // in case the answer is that a dashed 2 px line is simply too thin here.
    const int16_t cycles[4][2] = { { 2, 2 }, { 3, 3 }, { 4, 4 }, { 3, 6 } };
    int16_t y = 60;
    for (auto& c : cycles) {
        canvas.dashedLine(34, y, 206, y, 2, c[0], c[1], code(Slot::Path));
        y = static_cast<int16_t>(y + 18);
    }
    y = static_cast<int16_t>(y + 10);
    for (auto& c : cycles) {
        canvas.dashedLine(34, y, 206, y, 3, c[0], c[1], code(Slot::Path));
        y = static_cast<int16_t>(y + 18);
    }
}

void cardTextBed(Canvas& canvas)
{
    // Four bands the view writes the same string over, so the halo can be
    // judged against every fill it will meet.
    canvas.clear(code(Slot::Paper));
    const uint8_t fills[] = { code(Slot::Paper), code(Slot::Landuse),
                              code(Slot::WoodLight), code(Slot::Wood) };
    int16_t y = 46;
    for (uint8_t f : fills) {
        canvas.fillRect(20, y, 200, 36, f);
        y = static_cast<int16_t>(y + 38);
    }
}

} // namespace

const char* cardName(Card c)
{
    switch (c) {
        case Card::Palette64:       return "64 codes";
        case Card::SlotRoles:       return "slots";
        case Card::LineWeights:     return "weights";
        case Card::Dashes:          return "dashes";
        case Card::TextBed:         return "text";
        case Card::SceneNative:     return "scene 1x";
        case Card::SceneOverzoom:   return "scene 2x";
        case Card::SceneCoarse:     return "scene .5x";
        case Card::VariantNight:    return "night";
        case Card::VariantContrast: return "contrast";
        case Card::VariantTrail:    return "trail";
        case Card::TraceOverMap:    return "trace";
        default:                    return "?";
    }
}

const char* cardQuestion(Card c)
{
    switch (c) {
        case Card::Palette64:       return "one quantum = one step?";
        case Card::SlotRoles:       return "which bands vanish?";
        case Card::LineWeights:     return "thinnest road you can follow";
        case Card::Dashes:          return "which reads as a trail";
        case Card::TextBed:         return "does the halo save it";
        case Card::SceneNative:     return "is this a map";
        case Card::SceneOverzoom:   return "overzoom: still honest?";
        case Card::SceneCoarse:     return "too much at half scale?";
        case Card::VariantNight:    return "usable in the dark";
        case Card::VariantContrast: return "worth the lost detail";
        case Card::VariantTrail:    return "paths promoted enough";
        case Card::TraceOverMap:    return "trace wins everywhere?";
        default:                    return "";
    }
}

void drawCard(Card c, Canvas& canvas,
              const uint8_t* sceneBuf, uint32_t sceneBytes,
              Pt* scratch, int scratchCap)
{
    uint8_t lut[kLutEntries];

    switch (c) {
        case Card::Palette64:     cardPalette64(canvas);   return;
        case Card::SlotRoles:     cardSlotRoles(canvas);   return;
        case Card::LineWeights:   cardLineWeights(canvas); return;
        case Card::Dashes:        cardDashes(canvas);      return;
        case Card::TextBed:       cardTextBed(canvas);     return;

        case Card::SceneNative:
            drawScene(canvas, sceneBuf, sceneBytes, scratch, scratchCap, 240, 0, 0);
            return;

        case Card::SceneOverzoom:
            // The tile drawn at twice the pixels it was generalised for, and
            // centred, so what is on screen is the middle quarter of it. This
            // is what a sparse zoom ladder looks like from the wearer's side.
            drawScene(canvas, sceneBuf, sceneBytes, scratch, scratchCap, 480, -120, -120);
            return;

        case Card::SceneCoarse:
            drawScene(canvas, sceneBuf, sceneBytes, scratch, scratchCap, 120, 60, 60);
            return;

        case Card::VariantNight:
        case Card::VariantContrast:
        case Card::VariantTrail: {
            drawScene(canvas, sceneBuf, sceneBytes, scratch, scratchCap, 240, 0, 0);
            drawTrace(canvas);
            const Variant v = (c == Card::VariantNight)   ? Variant::Night
                            : (c == Card::VariantContrast) ? Variant::HighContrast
                                                           : Variant::Trail;
            buildLut(v, lut);
            canvas.applyLut(lut);
            return;
        }

        case Card::TraceOverMap:
            drawScene(canvas, sceneBuf, sceneBytes, scratch, scratchCap, 240, 0, 0);
            drawTrace(canvas);
            return;

        default:
            canvas.clear(code(Slot::Paper));
            return;
    }
}

} // namespace MapLab
