/**
 * @file Cards_test.cpp
 * @brief That every card draws something, and that the ones under test draw
 *        the thing they claim to.
 *
 * A visual suite cannot be tested for beauty. It can be tested for the two
 * failures that would waste a hardware session: a card that is blank, and a
 * card that is showing a different card's content.
 */
#include <gtest/gtest.h>

#include <Cards.hpp>
#include <Canvas.hpp>
#include <Palette.hpp>
#include <VecScene.hpp>

#include <set>
#include <vector>

namespace {

using namespace MapLab;

struct Bed {
    std::vector<uint8_t> scene;
    std::vector<uint8_t> px;
    std::vector<Pt>      scratch;
    Canvas               canvas;
    uint32_t             bytes;

    Bed() : scene(48 * 1024, 0), px(240 * 240, 0), scratch(kMaxPointsPerFeature),
            canvas(px.data(), 240, 240)
    {
        bytes = generateScene(scene.data(), static_cast<uint32_t>(scene.size()),
                              SceneParams::suburban());
    }

    void draw(Card c)
    {
        canvas.clear(0x00);   // not a palette code, so anything left is undrawn
        drawCard(c, canvas, scene.data(), bytes, scratch.data(),
                 static_cast<int>(scratch.size()));
    }

    std::set<uint8_t> codes() const { return std::set<uint8_t>(px.begin(), px.end()); }
};

TEST(Cards, EveryCardCoversThePanelAndHasANameAndAQuestion)
{
    Bed bed;
    for (int i = 0; i < static_cast<int>(Card::Count); ++i) {
        const Card c = static_cast<Card>(i);
        bed.draw(c);
        const std::set<uint8_t> seen = bed.codes();
        EXPECT_EQ(seen.count(0x00), 0u) << "card " << cardName(c) << " left pixels undrawn";
        EXPECT_GT(seen.size(), 1u) << "card " << cardName(c) << " is a flat fill";
        EXPECT_STRNE(cardName(c), "?");
        EXPECT_STRNE(cardQuestion(c), "");
    }
}

TEST(Cards, EveryCardCarriesTheReferencePatches)
{
    // The reference is what makes two photographs comparable at all. A card
    // that lost it -- by drawing over it, or by a variant LUT recolouring it --
    // would produce a frame that cannot be normalised, and nothing on the
    // watch would say so.
    Bed bed;
    const int16_t probeY[4] = { 88, 109, 130, 151 };
    const uint8_t want[4]   = { 0xFF, 0xEA, 0xD5, 0xC0 };
    for (int i = 0; i < static_cast<int>(Card::Count); ++i) {
        const Card c = static_cast<Card>(i);
        bed.draw(c);
        for (int k = 0; k < 4; ++k) {
            const int idx = probeY[k] * 240;
            EXPECT_EQ(bed.px[idx + 4],   want[k]) << "left reference, card " << cardName(c);
            EXPECT_EQ(bed.px[idx + 232], want[k]) << "right reference, card " << cardName(c);
        }
    }
}

TEST(Cards, TheReferenceSurvivesAVariantLut)
{
    // Specifically the ordering rule: subject, then LUT, then reference. Draw
    // it before the LUT and night would hand back a recoloured "neutral" ramp.
    Bed bed;
    bed.draw(Card::TraceSlotsNight);
    EXPECT_EQ(bed.px[88 * 240 + 4], 0xFF);
    EXPECT_EQ(bed.px[151 * 240 + 4], 0xC0);
}

TEST(Cards, TheTraceSlotCardsPutTheTraceOverEverySlot)
{
    // R5 in one frame: every basemap slot present, and the trace present over
    // them. The day card is the one that can be checked by code -- the variant
    // cards remap both, which is the thing only an eye can judge.
    Bed bed;
    bed.draw(Card::TraceSlotsDay);
    const std::set<uint8_t> seen = bed.codes();
    for (const SlotSpec& s : kSlots) {
        EXPECT_EQ(seen.count(s.code), 1u) << "slot " << s.name << " missing from the R5 card";
    }
    EXPECT_EQ(seen.count(code(Slot::Trace)), 1u);
}

TEST(Cards, TheCoarseSceneFillsThePanelRatherThanSittingInIt)
{
    // The bug this replaces: one 120 px tile centred on a 240 px panel, so
    // three quarters of the card was paper and the density question was being
    // asked of a quarter of the field.
    Bed bed;
    bed.draw(Card::SceneCoarse);
    // Sample the four quadrant centres; each must carry drawn content, not the
    // paper the old card left there. Reference bars are at x<16 and x>224, so
    // these probes miss them.
    const int16_t qx[4] = { 60, 180, 60, 180 };
    const int16_t qy[4] = { 60, 60, 180, 180 };
    for (int q = 0; q < 4; ++q) {
        bool drawn = false;
        for (int16_t dy = -20; dy <= 20 && !drawn; ++dy) {
            for (int16_t dx = -20; dx <= 20 && !drawn; ++dx) {
                if (bed.px[(qy[q] + dy) * 240 + (qx[q] + dx)] != code(Slot::Paper)) {
                    drawn = true;
                }
            }
        }
        EXPECT_TRUE(drawn) << "quadrant " << q << " of the half-scale card is empty";
    }
}

TEST(Cards, ThePaletteCardShowsAllSixtyFourCodes)
{
    Bed bed;
    bed.draw(Card::Palette64);
    const std::set<uint8_t> seen = bed.codes();
    // 64 codes, plus the notch colour marking the spec's slots -- which is
    // itself one of the 64, so the count is exactly 64.
    EXPECT_EQ(seen.size(), 64u);
}

TEST(Cards, TheSlotCardShowsEverySpecifiedSlot)
{
    Bed bed;
    bed.draw(Card::SlotRoles);
    const std::set<uint8_t> seen = bed.codes();
    for (const SlotSpec& s : kSlots) {
        EXPECT_EQ(seen.count(s.code), 1u) << "slot " << s.name << " is missing from its card";
    }
}

TEST(Cards, TheNightVariantInvertsTheGround)
{
    Bed bed;
    bed.draw(Card::SceneNative);
    const uint8_t nativeCorner = bed.px[0];
    bed.draw(Card::VariantNight);
    EXPECT_NE(bed.px[0], nativeCorner);
    // Ground is the one neutral dark code; § 9's whole argument is that this
    // panel has exactly one to spend on it.
    EXPECT_EQ(bed.px[0], code(Slot::RoadMajor));
}

TEST(Cards, TheTraceCardDrawsTheTraceCode)
{
    Bed bed;
    bed.draw(Card::TraceOverMap);
    EXPECT_EQ(bed.codes().count(code(Slot::Trace)), 1u);
}

TEST(Cards, ASceneCardWithNoSceneDrawsARefusalRatherThanNothing)
{
    // "The pack would not open" and "the map here is empty" must not look
    // alike -- the same rule MapKit's three failure states follow.
    std::vector<uint8_t> px(240 * 240, 0);
    std::vector<Pt> scratch(kMaxPointsPerFeature);
    Canvas canvas(px.data(), 240, 240);
    drawCard(Card::SceneNative, canvas, nullptr, 0, scratch.data(),
             static_cast<int>(scratch.size()));
    const std::set<uint8_t> seen(px.begin(), px.end());
    EXPECT_EQ(seen.count(0x00), 0u);
    EXPECT_EQ(seen.count(code(Slot::RoadMajor)), 1u) << "expected the cross";
}

} // namespace
