#include "Cards.hpp"

#include "Palette.hpp"
#include "SceneRender.hpp"

namespace MapLab
{
namespace
{

/// The panel is round: a 240 px square's corners are not visible, and content
/// pushed into them cannot be judged. Cards keep their subject inside this,
/// which also leaves the reference strips at x 16..40 and x 200..224 alone.
/// Scene cards are the deliberate exception: a map is the whole panel, and the
/// strips sit on top of it.
constexpr int16_t kContentInset = 44;

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
    constexpr int16_t cell = 19;
    constexpr int16_t x0   = (240 - 8 * cell) / 2;   // 44, clearing the reference
    for (int i = 0; i < 64; ++i) {
        const uint8_t c = static_cast<uint8_t>(0xC0 | i);
        const int16_t cx = static_cast<int16_t>(x0 + (i % 8) * cell);
        const int16_t cy = static_cast<int16_t>(x0 + (i / 8) * cell);
        canvas.fillRect(cx, cy, cell - 2, cell - 2, c);
        // Notch the codes the cartography actually spends, so they can be
        // found on the panel without counting squares.
        for (const auto& s : kSlots) {
            if (s.code == c) {
                canvas.fillRect(cx, cy, 4, 4, code(Slot::Trace));
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
        const int16_t inset = static_cast<int16_t>((i < 2 || i >= n - 2) ? 64 : kContentInset);
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
            canvas.thickLine(44, y, 112, y, w, ink);
            // Cased: halo first, ink over it, at the spec's ratio (ink + 3).
            canvas.thickLine(128, y, 196, y, static_cast<int16_t>(w + 3), kHalo);
            canvas.thickLine(128, y, 196, y, w, ink);
            y = static_cast<int16_t>(y + 13);
        }
        y = static_cast<int16_t>(y + 4);
    }
    // Diagonals, where aliasing is worst and a 1 px line either survives or
    // does not.
    canvas.thickLine(44, 210, 110, 150, 1, code(Slot::Contour));
    canvas.thickLine(80, 210, 146, 150, 2, code(Slot::RoadMinor));
    canvas.thickLine(116, 210, 182, 150, 4, code(Slot::RoadMajor));
}

void cardDashes(Canvas& canvas)
{
    canvas.clear(code(Slot::Paper));
    // Four cycles at the spec's 2 px path weight, plus the same cycles at 3 px
    // in case the answer is that a dashed 2 px line is simply too thin here.
    const int16_t cycles[4][2] = { { 2, 2 }, { 3, 3 }, { 4, 4 }, { 3, 6 } };
    int16_t y = 60;
    for (auto& c : cycles) {
        canvas.dashedLine(44, y, 196, y, 2, c[0], c[1], code(Slot::Path));
        y = static_cast<int16_t>(y + 18);
    }
    y = static_cast<int16_t>(y + 10);
    for (auto& c : cycles) {
        canvas.dashedLine(44, y, 196, y, 3, c[0], c[1], code(Slot::Path));
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
    // Inboard of the bezel, not flush against it. The first version sat at
    // x<16 and x>224 -- geometrically inside the disc, but hard against the
    // curve where the bezel shadows the glass, and small. In the 2026-08-19
    // photographs they were legible by eye and could not be registered
    // automatically, which is half a reference. 24x30 at x 16..40 and 200..224
    // sits clear of the shadow with margin: at x=28 the disc spans y 43..197,
    // so the whole strip is comfortably inside it.
    //
    // Cards keep their content within x 44..196 to leave this alone. That
    // costs every card a little width and is the reason the palette grid is
    // 19 px cells rather than 26.
    for (int i = 0; i < 4; ++i) {
        const int16_t y = static_cast<int16_t>(60 + i * 30);
        canvas.fillRect(16,  y, 24, 30, neutrals[i]);
        canvas.fillRect(200, y, 24, 30, neutrals[i]);
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
    constexpr int16_t x0 = 44, w = 38, h = 44;
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
        {  46, 120 }, {  62,  98 }, {  82,  84 }, { 104,  80 },
        { 125,  86 }, { 145, 100 }, { 163, 118 }, { 179, 136 }, { 194, 150 },
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
    // Wider than kContentInset on purpose: the reference strips stop at y=180
    // and these bands live at 176..220, so they barely meet. The bands need to
    // be wider than the caption they sit under, or the glyphs hang off the fill
    // and the halo is being judged against paper instead.
    canvas.fillRect(30, 176, 180, 22, upper);
    canvas.fillRect(30, 198, 180, 22, lower);
}

/// Every basemap slot as a band, paper between, narrower at the top and bottom
/// where a round panel would clip a full-width rect.
void drawSlotBands(Canvas& canvas)
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
        const int16_t inset = static_cast<int16_t>((i < 2 || i >= n - 2) ? 64 : kContentInset);
        canvas.fillRect(inset, y, static_cast<int16_t>(240 - 2 * inset), bandH,
                        kSlots[i].code);
    }
}

/// Top and bottom of the band stack, so a trace can be drawn across all of it.
int16_t slotBandsTop()
{
    const int n = static_cast<int>(Slot::Count);
    return static_cast<int16_t>((240 - n * 17) / 2);
}
int16_t slotBandsBottom() { return static_cast<int16_t>(slotBandsTop() + static_cast<int>(Slot::Count) * 17); }

void cardTraceOverSlots(Canvas& canvas)
{
    drawSlotBands(canvas);
    // Diagonal, so the trace crosses every band and does it at an angle --
    // which is where a 3 px line is thinnest in practice, and where the day
    // variants' trace-red-against-road-maroon problem showed up indoors.
    canvas.thickLine(56, slotBandsTop(), 184, slotBandsBottom(), 3, code(Slot::Trace));
}

/// The remedy, next to the thing it is meant to remedy, in one frame.
///
/// Left line is the trace as the spec draws it today. Right line is the same
/// ink at the same width with a `paper` casing under it. Both cross every
/// slot, in the same light, in the same photograph -- which is the only way to
/// answer "does casing rescue R5" without arguing about two exposures.
///
/// Why the variants draw this *after* the LUT: `kHalo` is `paper`, and a
/// variant remaps paper along with everything else. In `night` paper becomes
/// dark, so a casing applied before the restyle would put a dark halo on a
/// dark ground and rescue nothing. Drawing it afterwards is also the honest
/// model of the real thing -- the trace is app-drawn over a restyled basemap,
/// not part of the basemap being restyled.
void drawCasedComparison(Canvas& canvas)
{
    const int16_t t = slotBandsTop(), b = slotBandsBottom();
    canvas.thickLine(62, t, 102, b, 3, code(Slot::Trace));
    canvas.thickLine(144, t, 184, b, 5, kHalo);
    canvas.thickLine(144, t, 184, b, 3, code(Slot::Trace));
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
        case Card::TraceCasedDay:      return "cased day";
        case Card::TraceCasedNight:    return "cased night";
        case Card::TraceCasedContrast: return "cased contrast";
        case Card::TraceCasedTrail:    return "cased trail";
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
        case Card::TraceCasedDay:      return "cased vs uncased";
        case Card::TraceCasedNight:    return "cased vs uncased";
        case Card::TraceCasedContrast: return "cased vs uncased";
        case Card::TraceCasedTrail:    return "cased vs uncased";
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

        case Card::TraceCasedDay:
            drawSlotBands(canvas);
            drawCasedComparison(canvas);
            return;

        case Card::TraceCasedNight:
        case Card::TraceCasedContrast:
        case Card::TraceCasedTrail: {
            drawSlotBands(canvas);
            const Variant v = (c == Card::TraceCasedNight)    ? Variant::Night
                            : (c == Card::TraceCasedContrast) ? Variant::HighContrast
                                                              : Variant::Trail;
            buildLut(v, lut);
            canvas.applyLut(lut);
            // After the LUT, deliberately. See drawCasedComparison.
            drawCasedComparison(canvas);
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
