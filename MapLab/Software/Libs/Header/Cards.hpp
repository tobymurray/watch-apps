/**
 ******************************************************************************
 * @file    Cards.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The pictures. Twelve full-screen cards that put a judgement made in
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
