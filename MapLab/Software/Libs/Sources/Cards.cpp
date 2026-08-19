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

/// Render one tile at an offset. Does not clear, so a card can lay several
/// down; returns false if the scene would not open, leaving the canvas alone.
bool renderTileAt(Canvas& canvas, const uint8_t* buf, uint32_t bytes,
                  Pt* scratch, int scratchCap, int16_t tilePx, int16_t ox, int16_t oy)
{
    SceneReader r;
    if (!r.open(buf, bytes)) {
        return false;
    }
    renderScene(r, canvas, scratch, scratchCap, ox, oy, tilePx);
    return true;
}

/// A card that cannot draw its subject says so in the only vocabulary it has:
/// a diagonal cross, which no map ever produces.
void drawRefusal(Canvas& canvas)
{
    canvas.thickLine(0, 0, 239, 239, 2, code(Slot::RoadMajor));
    canvas.thickLine(239, 0, 0, 239, 2, code(Slot::RoadMajor));
}

void drawScene(Canvas& canvas, const uint8_t* buf, uint32_t bytes,
               Pt* scratch, int scratchCap, int16_t tilePx, int16_t ox, int16_t oy)
{
    canvas.clear(code(Slot::Paper));
    if (!renderTileAt(canvas, buf, bytes, scratch, scratchCap, tilePx, ox, oy)) {
        drawRefusal(canvas);
    }
}

/// The half-scale card: one clear, then a 2x2 of tiles. Clearing per tile --
/// which is what calling drawScene four times would do -- leaves only the last
/// one on the panel, and the card silently becomes a one-tile card again.
void drawSceneMosaic(Canvas& canvas, const uint8_t* buf, uint32_t bytes,
                     Pt* scratch, int scratchCap, int16_t tilePx)
{
    canvas.clear(code(Slot::Paper));
    for (int16_t oy = 0; oy < 240; oy = static_cast<int16_t>(oy + tilePx)) {
        for (int16_t ox = 0; ox < 240; ox = static_cast<int16_t>(ox + tilePx)) {
            if (!renderTileAt(canvas, buf, bytes, scratch, scratchCap, tilePx, ox, oy)) {
                drawRefusal(canvas);
                return;
            }
        }
    }
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


/// Known values in a fixed place on every card. See the header for why.
void drawReference(Canvas& canvas)
{
    // r=g=b at each of the four quantum levels. Chosen over an arbitrary grey
    // ramp because these are codes the panel can actually make, so the
    // reference is a thing the display does rather than a thing a camera is
    // asked to believe.
    const uint8_t neutrals[4] = { 0xFF, 0xEA, 0xD5, 0xC0 };
    // x < 16 and x > 224 clears the 64-code grid, which spans 16..224, so no
    // card loses a swatch to the reference. y 78..162 is the span still inside
    // the round panel at that far out.
    for (int i = 0; i < 4; ++i) {
        const int16_t y = static_cast<int16_t>(78 + i * 21);
        canvas.fillRect(0,   y, 16, 21, neutrals[i]);
        canvas.fillRect(224, y, 16, 21, neutrals[i]);
    }
}

void cardChannelRamps(Canvas& canvas)
{
    canvas.clear(code(Slot::Paper));
    // One row per channel, stepping that channel 0->3 with the other two held
    // at 0. The 64-code grid asks the same question in 26 px cells; this asks
    // it in 40x44 blocks, because a small patch of a code reads darker than a
    // large one and "is one quantum one step" is a judgement about area.
    // The neutral ramp is not repeated here -- it is on every card already, as
    // the reference.
    constexpr int16_t x0 = 40, w = 40, h = 44;
    int16_t y = 40;
    for (int ch = 0; ch < 3; ++ch) {
        for (uint8_t lv = 0; lv < 4; ++lv) {
            const uint8_t c = (ch == 0) ? abgr2222(lv, 0, 0)
                            : (ch == 1) ? abgr2222(0, lv, 0)
                                        : abgr2222(0, 0, lv);
            canvas.fillRect(static_cast<int16_t>(x0 + lv * w), y, w, h, c);
        }
        y = static_cast<int16_t>(y + h + 6);
    }
}

void cardSlotsAtWidth(Canvas& canvas)
{
    canvas.clear(code(Slot::Paper));
    // Card 2 asks which slots vanish as 15 px bands. A slot that survives as a
    // band can still vanish as a 1 px contour, and 1 px is the width the
    // cartography actually spends it at. Three widths per slot, in the order
    // the spec draws them: contour, path, road.
    const int n = static_cast<int>(Slot::Count);
    constexpr int16_t top = 30, step = 14;
    for (int i = 0; i < n; ++i) {
        const int16_t y = static_cast<int16_t>(top + i * step);
        const uint8_t c = kSlots[i].code;
        canvas.thickLine(60, y, 100, y, 1, c);
        canvas.thickLine(106, y, 146, y, 2, c);
        canvas.thickLine(152, y, 186, y, 3, c);
    }
}

void cardCurves(Canvas& canvas)
{
    canvas.clear(code(Slot::Paper));
    // Card 3 tests a horizontal and a 45-degree diagonal, which are the two
    // angles a non-antialiased rasteriser is best at. A road is neither.
    // Points are a fixed table rather than computed: there is no floating
    // point in this library, and a curve is exactly where an integer
    // approximation would become the thing under test instead of the subject.
    const Pt sweep[] = {
        {  38, 120 }, {  56,  98 }, {  78,  84 }, { 102,  80 },
        { 126,  86 }, { 148, 100 }, { 168, 118 }, { 186, 136 }, { 202, 150 },
    };
    const int nPts = static_cast<int>(sizeof(sweep) / sizeof(sweep[0]));
    const struct { int16_t dy; int16_t w; uint8_t ink; } rows[] = {
        { -58, 1, code(Slot::Contour)   },
        {   0, 2, code(Slot::Path)      },
        {  58, 3, code(Slot::RoadMajor) },
    };
    for (const auto& r : rows) {
        Pt pts[nPts];
        for (int i = 0; i < nPts; ++i) {
            pts[i].x = sweep[i].x;
            pts[i].y = static_cast<int16_t>(sweep[i].y + r.dy);
        }
        canvas.polyline(pts, nPts, r.w, r.ink);
    }
}

/// The bands for a text card. Split light/dark across two cards because the
/// view only writes two lines, so one card can only ever test two fills.
void cardTextBedFills(Canvas& canvas, uint8_t upper, uint8_t lower)
{
    canvas.clear(code(Slot::Paper));
    // Aligned to where the view puts the two caption lines in card mode, so
    // the glyphs land on the fill rather than beside it.
    canvas.fillRect(34, 176, 172, 22, upper);
    canvas.fillRect(34, 198, 172, 22, lower);
}

void cardTraceOverSlots(Canvas& canvas)
{
    canvas.clear(code(Slot::Paper));
    // Rule R5 -- "the trace must win against every basemap colour" -- was only
    // ever tested against whatever colours a generated scene happened to put
    // under the trace. This puts it against all of them, deliberately.
    const int n = static_cast<int>(Slot::Count);
    constexpr int16_t bandH = 15;
    const int16_t top = static_cast<int16_t>((240 - n * (bandH + 2)) / 2);
    for (int i = 0; i < n; ++i) {
        const int16_t y = static_cast<int16_t>(top + i * (bandH + 2));
        const int16_t inset = static_cast<int16_t>((i < 2 || i >= n - 2) ? 58 : kInset);
        canvas.fillRect(inset, y, static_cast<int16_t>(240 - 2 * inset), bandH,
                        kSlots[i].code);
    }
    // Diagonal, so the trace crosses every band and does it at an angle --
    // which is where a 3 px line is thinnest in practice, and where the day
    // variants' trace-red-against-road-maroon problem showed up indoors.
    const int16_t bot = static_cast<int16_t>(top + n * (bandH + 2));
    canvas.thickLine(52, top, 188, bot, 3, code(Slot::Trace));
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
        case Card::ChannelRamps:       return "ramps";
        case Card::SlotsAtWidth:       return "slots at width";
        case Card::Curves:             return "curves";
        case Card::TextBedDark:        return "text dark";
        case Card::TraceSlotsDay:      return "trace/slots day";
        case Card::TraceSlotsNight:    return "trace/slots night";
        case Card::TraceSlotsContrast: return "trace/slots contrast";
        case Card::TraceSlotsTrail:    return "trace/slots trail";
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
        case Card::ChannelRamps:       return "one quantum = one step?";
        case Card::SlotsAtWidth:       return "which vanish at 1 px";
        case Card::Curves:             return "does a curve stay a line";
        case Card::TextBedDark:        return "does the halo save it";
        case Card::TraceSlotsDay:      return "trace over every slot";
        case Card::TraceSlotsNight:    return "trace over every slot";
        case Card::TraceSlotsContrast: return "trace over every slot";
        case Card::TraceSlotsTrail:    return "trace over every slot";
        default:                    return "";
    }
}

/// Everything except the reference patches, which drawCard adds afterwards.
static void drawSubject(Card c, Canvas& canvas,
                        const uint8_t* sceneBuf, uint32_t sceneBytes,
                        Pt* scratch, int scratchCap)
{
    uint8_t lut[kLutEntries];

    switch (c) {
        case Card::Palette64:     cardPalette64(canvas);   return;
        case Card::SlotRoles:     cardSlotRoles(canvas);   return;
        case Card::LineWeights:   cardLineWeights(canvas); return;
        case Card::Dashes:        cardDashes(canvas);      return;
        case Card::TextBed:
            cardTextBedFills(canvas, code(Slot::Paper), code(Slot::Landuse));
            return;
        case Card::TextBedDark:
            // The other end of the range. White glyphs over `water` and
            // `road_major` is the case a paper halo is supposed to be
            // unnecessary for, and the case it is easiest to be wrong about.
            cardTextBedFills(canvas, code(Slot::Water), code(Slot::RoadMajor));
            return;

        case Card::ChannelRamps:  cardChannelRamps(canvas); return;
        case Card::SlotsAtWidth:  cardSlotsAtWidth(canvas); return;
        case Card::Curves:        cardCurves(canvas);       return;

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
            // Four tiles at half scale, filling the panel, rather than one
            // sitting in the middle of it. Density is judged over a field: a
            // 120 px render surrounded by paper understates how crowded a
            // coarse zoom really looks, which is the whole question here.
            // The repetition is visible and is the price of asking the right
            // question -- there is one generated tile in a session, on purpose.
            drawSceneMosaic(canvas, sceneBuf, sceneBytes, scratch, scratchCap, 120);
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

        case Card::TraceSlotsDay:
            cardTraceOverSlots(canvas);
            return;

        case Card::TraceSlotsNight:
        case Card::TraceSlotsContrast:
        case Card::TraceSlotsTrail: {
            cardTraceOverSlots(canvas);
            const Variant v = (c == Card::TraceSlotsNight)    ? Variant::Night
                            : (c == Card::TraceSlotsContrast) ? Variant::HighContrast
                                                              : Variant::Trail;
            buildLut(v, lut);
            canvas.applyLut(lut);
            return;
        }

        default:
            canvas.clear(code(Slot::Paper));
            return;
    }
}

void drawCard(Card c, Canvas& canvas,
              const uint8_t* sceneBuf, uint32_t sceneBytes,
              Pt* scratch, int scratchCap)
{
    drawSubject(c, canvas, sceneBuf, sceneBytes, scratch, scratchCap);
    // Last, and outside the variant LUTs, so the same four neutral codes are
    // in the same two places in every photograph of every card.
    drawReference(canvas);
}

} // namespace MapLab
