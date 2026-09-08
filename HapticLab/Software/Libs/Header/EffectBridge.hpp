#ifndef HAPTICLAB_EFFECTBRIDGE_HPP
#define HAPTICLAB_EFFECTBRIDGE_HPP

#include "SDK/Messages/CommandMessages.hpp"

#include "Effects.hpp"
#include "Patterns.hpp"

/// The one place this app's own effect table touches the SDK's. `Effects.hpp`
/// is deliberately free of SDK types so the pure tests build without one, and
/// the price of that is a second copy of twenty-one numbers; these assertions
/// are what makes an SDK that renumbers or renames them a build failure here
/// rather than a pattern set that plays something else.
namespace Haptic {

using Vibro = SDK::Message::RequestVibroPlay;

static_assert(kMaxNotes == Vibro::skMaxNotes, "the note limit moved");

static_assert(kNamedEffects[0].index == Vibro::Effect::STRONG_CLICK_100, "effect 1 moved");
static_assert(kNamedEffects[1].index == Vibro::Effect::SHARP_CLICK_100, "effect 4 moved");
static_assert(kNamedEffects[2].index == Vibro::Effect::SOFT_BUMP_100, "effect 7 moved");
static_assert(kNamedEffects[3].index == Vibro::Effect::DOUBLE_CLICK_100, "effect 10 moved");
static_assert(kNamedEffects[4].index == Vibro::Effect::STRONG_BUZZ_100, "effect 14 moved");
static_assert(kNamedEffects[5].index == Vibro::Effect::ALERT_750MS_100, "effect 15 moved");
static_assert(kNamedEffects[6].index == Vibro::Effect::ALERT_1000MS_100, "effect 16 moved");
static_assert(kNamedEffects[7].index == Vibro::Effect::STRONG_CLICK_1_100, "effect 17 moved");
static_assert(kNamedEffects[8].index == Vibro::Effect::MEDIUM_CLICK_1_100, "effect 21 moved");
static_assert(kNamedEffects[9].index == Vibro::Effect::SHARP_TICK_1_100, "effect 24 moved");
static_assert(kNamedEffects[10].index == Vibro::Effect::SHORT_DOUBLE_CLICK_STRONG_1_100,
              "effect 27 moved");
static_assert(kNamedEffects[11].index == Vibro::Effect::SHORT_DOUBLE_CLICK_MEDIUM_1_100,
              "effect 31 moved");
static_assert(kNamedEffects[12].index == Vibro::Effect::SHORT_DOUBLE_SHARP_TICK_1_100,
              "effect 34 moved");
static_assert(kNamedEffects[13].index == Vibro::Effect::LONG_DOUBLE_SHARP_CLICK_STRONG_1_100,
              "effect 37 moved");
static_assert(kNamedEffects[14].index == Vibro::Effect::LONG_DOUBLE_SHARP_CLICK_MEDIUM_1_100,
              "effect 41 moved");
static_assert(kNamedEffects[15].index == Vibro::Effect::LONG_DOUBLE_SHARP_TICK_1_100,
              "effect 44 moved");
static_assert(kNamedEffects[16].index == Vibro::Effect::BUZZ_1_100, "effect 47 moved");
static_assert(kNamedEffects[17].index == Vibro::Effect::PULSING_STRONG_1_100, "effect 52 moved");
static_assert(kNamedEffects[18].index == Vibro::Effect::PULSING_MEDIUM_1_100, "effect 54 moved");
static_assert(kNamedEffects[19].index == Vibro::Effect::PULSING_SHARP_1_100, "effect 56 moved");
static_assert(kNamedEffectCount == 20, "the named effect list changed length");

/// Copies a note list onto the wire. `declaredNotesCount` is separate from
/// `count` because Phase 0 asks what the device does when the message says it
/// carries more notes than the array can hold.
inline void fillVibroMessage(Vibro &msg, const Note notes[], uint8_t count,
                             uint32_t declaredNotesCount)
{
    const uint8_t copied = count <= kMaxNotes ? count : kMaxNotes;
    for (uint8_t i = 0; i < copied; i++) {
        msg.notes[i].effect = notes[i].effect;
        msg.notes[i].pause  = notes[i].pauseMs;
    }
    msg.notesCount = declaredNotesCount;
}

} // namespace Haptic

#endif // HAPTICLAB_EFFECTBRIDGE_HPP
