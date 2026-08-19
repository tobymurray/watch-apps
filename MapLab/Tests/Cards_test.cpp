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
