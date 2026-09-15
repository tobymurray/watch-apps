#ifndef HAPTICLAB_EFFECTS_HPP
#define HAPTICLAB_EFFECTS_HPP

#include <cstddef>
#include <cstdint>

/// The vibro effect space as an app can reach it, free of SDK types so the
/// pure tests can build without one. `EffectBridge.hpp` is what holds these
/// values to `SDK::Message::RequestVibroPlay`.
namespace Haptic {

/// WIRE FORMAT: `RequestVibroPlay::Note::effect` is documented as 1-127, with
/// 0 meaning the note is a pause instead. Whether 127 plays and what 128 does
/// is Docs/LEDGER.md row V2.
constexpr uint8_t kMinEffect = 1;
constexpr uint8_t kMaxEffect = 127;

/// Indices the wire format accepts and the sweep found unusable, so no pattern
/// may contain one. 124 upward produce no burst in any repeat on either sweep,
/// which the wrist agreed with on probe `V2-effect-127`. 118 is worse than
/// silent: it drives the actuator continuously for seconds past the ask, with
/// no reachable way to stop it, so one note of it poisons everything that
/// follows. Docs/LEDGER.md rows S6 and S12.
constexpr uint8_t kUnusableEffects[] = { 118, 124, 125, 126, 127 };

/// Whether `effect` is an index a pattern may use. Being inside the wire
/// format's range is not enough.
inline bool isUsableEffect(uint8_t effect)
{
    if (effect < kMinEffect || effect > kMaxEffect) {
        return false;
    }
    for (size_t i = 0; i < sizeof(kUnusableEffects) / sizeof(kUnusableEffects[0]); i++) {
        if (kUnusableEffects[i] == effect) {
            return false;
        }
    }
    return true;
}

/// WIRE FORMAT: `IVibro::Note::pause` is documented as 1-1270 ms in steps of
/// 10 ms; the message's own copy of the struct omits the range and the step.
constexpr uint16_t kMinPauseMs  = 10;
constexpr uint16_t kMaxPauseMs  = 1270;
constexpr uint16_t kPauseStepMs = 10;

/// WIRE FORMAT: eight notes a message, pauses included, and the message
/// carries neither of `IVibro`'s two loop counts.
constexpr uint8_t kMaxNotes = 8;

/// How long to wait for the kernel to signal a `RequestVibroPlay` accepted.
/// Non-zero so the result is readable: with a zero timeout `sendMessage` is
/// fire and forget and `getResult()` never leaves PENDING. It says accepted,
/// never finished: there is no message that reports a pattern's end.
constexpr uint32_t kVibroSendTimeoutMs = 200;

/// The upper bound this app models an unmeasured effect at, because two named
/// effects state their own duration and 1000 ms is the longer of them. Phase 1
/// replaces it per effect; until then every timing derived from it is a
/// ceiling rather than an estimate.
constexpr uint16_t kNominalEffectMs = 1000;

/// The qualitative families the effect names divide into, which is the
/// waveform-character axis of the candidate set.
enum class Family : uint8_t {
    Click = 0,
    Tick,
    Bump,
    Buzz,
    Pulsing,
    Alert,
    Count,
};

struct NamedEffect {
    uint8_t     index;
    Family      family;
    const char *name;
};

/// The effects the SDK gives a name, each one the 100 % amplitude entry of its
/// waveform. The gaps between them are Docs/LEDGER.md row V1: whether they play
/// at all, and whether they are the same waveform quieter, is the whole
/// question of whether this app has an amplitude axis.
constexpr NamedEffect kNamedEffects[] = {
    {  1, Family::Click,   "STRONG CLICK"    },
    {  4, Family::Click,   "SHARP CLICK"     },
    {  7, Family::Bump,    "SOFT BUMP"       },
    { 10, Family::Click,   "DOUBLE CLICK"    },
    { 14, Family::Buzz,    "STRONG BUZZ"     },
    { 15, Family::Alert,   "ALERT 750MS"     },
    { 16, Family::Alert,   "ALERT 1000MS"    },
    { 17, Family::Click,   "STRONG CLICK 1"  },
    { 21, Family::Click,   "MEDIUM CLICK 1"  },
    { 24, Family::Tick,    "SHARP TICK 1"    },
    { 27, Family::Click,   "SH DBL CLK STR"  },
    { 31, Family::Click,   "SH DBL CLK MED"  },
    { 34, Family::Tick,    "SH DBL SH TICK"  },
    { 37, Family::Click,   "LG DBL SH C STR" },
    { 41, Family::Click,   "LG DBL SH C MED" },
    { 44, Family::Tick,    "LG DBL SH TICK"  },
    { 47, Family::Buzz,    "BUZZ 1"          },
    { 52, Family::Pulsing, "PULSING STRONG"  },
    { 54, Family::Pulsing, "PULSING MEDIUM"  },
    { 56, Family::Pulsing, "PULSING SHARP"   },
};

constexpr size_t kNamedEffectCount = sizeof(kNamedEffects) / sizeof(kNamedEffects[0]);

/// The two effects whose names state a duration, so a measured duration can be
/// checked against what the effect claims to be.
constexpr uint8_t  kSelfTimingEffects[]  = { 15, 16 };
constexpr uint16_t kSelfTimedDurations[] = { 750, 1000 };

inline const NamedEffect *namedEffect(uint8_t index)
{
    for (size_t i = 0; i < kNamedEffectCount; i++) {
        if (kNamedEffects[i].index == index) {
            return &kNamedEffects[i];
        }
    }
    return nullptr;
}

/// The name of the effect at `index`, or "UNNAMED" for one of the gaps.
inline const char *effectName(uint8_t index)
{
    const NamedEffect *named = namedEffect(index);
    return named != nullptr ? named->name : "UNNAMED";
}

inline const char *familyName(Family family)
{
    switch (family) {
    case Family::Click:   return "click";
    case Family::Tick:    return "tick";
    case Family::Bump:    return "bump";
    case Family::Buzz:    return "buzz";
    case Family::Pulsing: return "pulsing";
    case Family::Alert:   return "alert";
    case Family::Count:   break;
    }
    return "unknown";
}

/// A pause the wire format can carry: on the 10 ms step, and inside 1-1270.
inline uint16_t quantisePause(uint32_t requestedMs)
{
    if (requestedMs == 0) {
        return 0;
    }
    uint32_t stepped = ((requestedMs + kPauseStepMs / 2) / kPauseStepMs) * kPauseStepMs;
    if (stepped < kMinPauseMs) {
        stepped = kMinPauseMs;
    }
    if (stepped > kMaxPauseMs) {
        stepped = kMaxPauseMs;
    }
    return static_cast<uint16_t>(stepped);
}

} // namespace Haptic

#endif // HAPTICLAB_EFFECTS_HPP
