/**
 ******************************************************************************
 * @file    Cards.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The pictures. Full-screen cards that put a judgement made in
 *          simulation onto the panel it was made about.
 ******************************************************************************
 *
 * Every cartographic decision the vector pipeline rests on was taken against
 * an sRGB monitor showing a colorimetric *model* of this panel:
 *
 *   - the 64 codes and their L* values (E1) -- a model;
 *   - the 14-slot palette and its contrast ratios (§ 3) -- arithmetic on that
 *     model;
 *   - the line weights and the dash (§ 4) -- explicitly "judgements informed
 *     by the 1:1 renders, not measurements", and "the part of this spec most
 *     likely to change after a hardware legibility trial";
 *   - the four LUT variants (§ 9) -- "proved the mechanism in simulation".
 *
 * This is the hardware legibility trial. The cards exist to be *looked at*,
 * indoors and in sunlight, and photographed for the investigation bundle. They
 * deliberately carry no measurement of their own: a benchmark can tell you a
 * render costs 40 ms, and nothing but an eye can tell you a 1 px contour is a
 * texture rather than a shimmer.
 *
 * What each card asks is in the README, one question per card, so that
 * somebody holding the watch knows what they are being asked to decide.
 *
 * Text is not drawn here. TouchGFX owns the font stack, so the text cards draw
 * their backgrounds here and the view puts real glyphs on top -- which is also
 * the arrangement a label-drawing renderer would have to use.
 *
 * ---------------------------------------------------------------------------
 * The reference patches
 *
 * Every card carries four neutral codes -- r=g=b at each quantum level -- in
 * two 16 px bars at the extreme left and right. They are not part of any
 * card's subject. They are there because the instrument photographing these
 * cards has auto white balance and auto exposure, so every frame arrives under
 * a different colour transform and no two frames can otherwise be compared --
 * least of all an indoor frame against a daylight one, which is the comparison
 * Gate D actually turns on.
 *
 * Two bars rather than one because a reflective panel glares directionally: a
 * left-right difference in the same four patches is an illumination gradient
 * across the glass, not a fact about the palette.
 *
 * They are drawn *last*, after any variant LUT, so the reference is the same
 * neutral ramp on every card including the restyled ones.
 *
 * Pure: canvas in, pixels out. Host-tested for coverage rather than beauty.
 ******************************************************************************
 */

#ifndef MAPLAB_CARDS_HPP
#define MAPLAB_CARDS_HPP

#include "Canvas.hpp"
#include "VecScene.hpp"

namespace MapLab
{

enum class Card : uint8_t {
    Palette64 = 0,   ///< All 64 codes. Is the model's ordering the panel's?
    SlotRoles,       ///< The 14 spec slots against paper, at their real roles.
    LineWeights,     ///< 1-4 px, each ink, straight and diagonal, cased and not.
    Dashes,          ///< Which on/off cycle reads as a trail at 2 px.
    TextBed,         ///< Fills for the view to write text over.
    SceneNative,     ///< A generated tile at its authored scale.
    SceneOverzoom,   ///< The same tile drawn 2x -- the sparse-ladder question.
    SceneCoarse,     ///< The same tile at half scale -- the generalisation question.
    VariantNight,
    VariantContrast,
    VariantTrail,
    TraceOverMap,    ///< R5: the trace must win against every basemap colour.

    // ---- added after the 2026-08-19 indoor session, which is why these come
    // after the first twelve rather than in a tidier order: the twelve are
    // already photographed and their numbering is cited in the investigation
    // bundle, so renumbering them would orphan that evidence.
    ChannelRamps,    ///< One quantum of each channel, as areas big enough to judge.
    SlotsAtWidth,    ///< The slots at 1/2/3 px -- the width they are really drawn.
    Curves,          ///< Weights on a curve, where stair-stepping is worst.
    TextBedDark,     ///< The dark half of the fills the halo has to survive.
    TraceSlotsDay,   ///< R5 against every slot, not against whatever a scene put there.
    TraceSlotsNight,
    TraceSlotsContrast,
    TraceSlotsTrail,

    // ---- added after the 2026-08-19 lighting series refuted R5. These carry
    // the proposed remedy -- a `paper` casing under the trace -- next to the
    // uncased line, in one frame, so direct sun tests the fix rather than
    // re-confirming the failure.
    TraceCasedDay,
    TraceCasedNight,
    TraceCasedContrast,
    TraceCasedTrail,
    Count
};

const char* cardName(Card c);
/// One line of what the card is asking, shown under it on the watch.
const char* cardQuestion(Card c);

/**
 * @brief Draw a card, full-canvas.
 *
 * @param sceneBuf/sceneBytes  a tile from generateScene(); the scene cards
 *        need one and the others ignore it. Passed in rather than generated
 *        here so that every card in a session shows the same geometry.
 */
void drawCard(Card c, Canvas& canvas,
              const uint8_t* sceneBuf, uint32_t sceneBytes,
              Pt* scratch, int scratchCap);

} // namespace MapLab

#endif // MAPLAB_CARDS_HPP
