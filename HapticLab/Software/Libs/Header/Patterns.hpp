#ifndef HAPTICLAB_PATTERNS_HPP
#define HAPTICLAB_PATTERNS_HPP

#include <cstddef>
#include <cstdint>

#include "Effects.hpp"

/// The candidate set, as data rather than as code, so a row of `patterns.csv`
/// describes the pattern that was played without reference to this file and a
/// later change to the table cannot orphan a recorded run. Why these patterns
/// and no others: HapticLab/README.md, "The candidate set".
namespace Haptic {

/// How the gaps between a pattern's pulses are spaced.
enum class Rhythm : uint8_t {
    Single = 0, ///< One pulse, so there is no gap to space.
    Even,
    ShortLong,
    LongShort,
    Count,
};

/// Which amplitude tier of its waveform a pattern's pulses use, where tier 0
/// is the named 100 % entry and the others are the gaps above it. Every tier
/// but `Full` depends on Docs/LEDGER.md row V1 holding.
enum class Amplitude : uint8_t {
    Full = 0,
    Mid,
    Low,
    Rising,
    Falling,
    Count,
};

/// How long a pattern is meant to feel, which is a design intent rather than a
/// measurement: `modelledDurationMs` is the arithmetic and Phase 1 the check.
enum class Length : uint8_t {
    Brief = 0,
    Sustained,
    Count,
};

struct Note {
    uint8_t  effect;  ///< 0 = this note is a pause
    uint16_t pauseMs; ///< 0 = this note is an effect
};

/// The longest a pattern id or name may be, both sized to leave the round
/// panel's chord at the button rows some room; `Layout` holds the widths.
constexpr size_t kMaxPatternIdLength   = 4;
constexpr size_t kMaxPatternNameLength = 8;

struct Pattern {
    char      id[kMaxPatternIdLength + 1];
    char      name[kMaxPatternNameLength + 1];
    Family    family;
    uint8_t   pulses;
    Rhythm    rhythm;
    Amplitude amplitude;
    Length    length;
    uint8_t   noteCount;
    Note      notes[kMaxNotes];
};

/// The gaps the rhythm axis uses. Both are on the wire format's 10 ms step, and
/// the ratio between them is what the axis actually varies.
constexpr uint16_t kShortGapMs = 80;
constexpr uint16_t kLongGapMs  = 320;

/// A pulse train of `count` pulses at `effect`, gaps taken from `gaps`.
inline void fillTrain(Pattern &out, const uint8_t effects[], uint8_t count, const uint16_t gaps[])
{
    out.noteCount = 0;
    for (uint8_t i = 0; i < count && out.noteCount < kMaxNotes; i++) {
        if (i > 0) {
            out.notes[out.noteCount].effect  = 0;
            out.notes[out.noteCount].pauseMs = quantisePause(gaps[i - 1]);
            out.noteCount++;
        }
        if (out.noteCount >= kMaxNotes) {
            break;
        }
        out.notes[out.noteCount].effect  = effects[i];
        out.notes[out.noteCount].pauseMs = 0;
        out.noteCount++;
    }
}

/// Which candidate set a run used, recorded so a run whose set is no longer
/// generated is still readable.
enum class SetId : uint8_t {
    /// Twelve patterns that vary family, pulse count, rhythm and length, and
    /// nothing that needs an amplitude axis to exist.
    RhythmAndFamily = 0,
    /// The same twelve plus two intensity-only siblings of `TAP` and two
    /// ramps. **Kept for the record and not worth running**: row S7a measured
    /// the click ladder these are built on as flat, so it would spend an hour
    /// confirming a foregone conclusion.
    WithAmplitude,
    /// Three patterns and nothing else: the soft-bump ladder, which is the one
    /// amplitude ladder the actuator has. A whole session in about ten
    /// minutes, and the only way to ask the intensity question without
    /// inflating the main set to sixteen.
    ///
    /// B01 against either of the others is a real amplitude question: effects 8
    /// and 9 measure at 40 to 49 % of effect 7 on both sweeps, and a factor of
    /// two is well outside what the energy proxy can confuse (row S14).
    ///
    /// **B02 against B03 is not.** The two came out 1 % apart on one sweep and
    /// 21 % apart on the other, so whether the actuator separates them at all
    /// is below what this instrument can measure (row S7a). That makes the pair
    /// worth keeping and worth reading carefully: an above-chance result on it
    /// is either an amplitude step the IMU cannot see or something reaching the
    /// wearer other than the skin, and the no-contact control is what tells the
    /// two apart. It is not evidence of amplitude resolution on its own and no
    /// vocabulary entry may read it that way.
    ///
    /// Needs a higher pass count than the others rather than the default: with
    /// three patterns a pass is one triple, so four passes leaves twelve
    /// trials a pair, exactly at the analysis floor and far too few for the
    /// interval to clear a 10 % threshold. Twelve passes gives thirty-six.
    BumpLadder,
    Count,
};

constexpr uint8_t kRhythmAndFamilyCount = 12;
constexpr uint8_t kWithAmplitudeCount   = 16;
constexpr uint8_t kBumpLadderCount      = 3;
constexpr uint8_t kMaxPatterns          = kWithAmplitudeCount;

inline const char *setName(SetId set)
{
    switch (set) {
    case SetId::RhythmAndFamily: return "rhythm-and-family";
    case SetId::WithAmplitude:   return "with-amplitude";
    case SetId::BumpLadder:      return "bump-ladder";
    case SetId::Count:           break;
    }
    return "unknown";
}

/// Effect indices the amplitude axis reaches for, each one a gap immediately
/// above `STRONG CLICK`'s named entry. Docs/LEDGER.md row S7a measured these
/// three at 100 %, 106 % and 104 % of each other's burst energy, so **this
/// ladder is flat** and the patterns built on it test nothing.
constexpr uint8_t kTapFull = 1;
constexpr uint8_t kTapMid  = 2;
constexpr uint8_t kTapLow  = 3;

/// The gaps above `SOFT BUMP`, which row S7a measured at 100 %, 50 % and 41 %
/// of each other's burst energy at an identical 85 ms. This is the only
/// amplitude ladder the actuator actually has.
constexpr uint8_t kBumpFull = 7;
constexpr uint8_t kBumpMid  = 8;
constexpr uint8_t kBumpLow  = 9;

namespace detail {

inline Pattern single(const char *id, const char *name, uint8_t effect, Family family, Length length)
{
    Pattern p {};
    for (size_t i = 0; i < kMaxPatternIdLength && id[i] != '\0'; i++) {
        p.id[i] = id[i];
    }
    for (size_t i = 0; i < kMaxPatternNameLength && name[i] != '\0'; i++) {
        p.name[i] = name[i];
    }
    p.family    = family;
    p.pulses    = 1;
    p.rhythm    = Rhythm::Single;
    p.amplitude = Amplitude::Full;
    p.length    = length;
    const uint8_t  effects[] = { effect };
    fillTrain(p, effects, 1, nullptr);
    return p;
}

inline Pattern train(const char *id, const char *name, const uint8_t effects[], uint8_t count,
                     const uint16_t gaps[], Family family, Rhythm rhythm, Amplitude amplitude)
{
    Pattern p = single(id, name, effects[0], family, Length::Brief);
    p.pulses    = count;
    p.rhythm    = rhythm;
    p.amplitude = amplitude;
    fillTrain(p, effects, count, gaps);
    return p;
}

} // namespace detail

/// Writes the candidate set into `out` and returns how many patterns it holds.
inline uint8_t buildSet(SetId set, Pattern out[kMaxPatterns])
{
    using detail::single;
    using detail::train;

    const uint8_t  tap2[]  = { kTapFull, kTapFull };
    const uint8_t  tap3[]  = { kTapFull, kTapFull, kTapFull };
    const uint8_t  tick2[] = { 24, 24 };
    const uint8_t  rise[]  = { kTapLow, kTapMid, kTapFull };
    const uint8_t  fall[]  = { kTapFull, kTapMid, kTapLow };
    const uint16_t even[]  = { kShortGapMs, kShortGapMs };
    const uint16_t sl[]    = { kShortGapMs, kLongGapMs };
    const uint16_t ls[]    = { kLongGapMs, kShortGapMs };

    uint8_t n = 0;
    if (set == SetId::BumpLadder) {
        // The three share a waveform and a duration and differ only in
        // amplitude, which is the whole point: nothing else can be the cue.
        Pattern full = single("B01", "BUMP", kBumpFull, Family::Bump, Length::Brief);
        Pattern mid  = single("B02", "BUMPMID", kBumpMid, Family::Bump, Length::Brief);
        mid.amplitude = Amplitude::Mid;
        Pattern low  = single("B03", "BUMPLOW", kBumpLow, Family::Bump, Length::Brief);
        low.amplitude = Amplitude::Low;
        out[n++] = full;
        out[n++] = mid;
        out[n++] = low;
        return n;
    }

    out[n++] = single("P01", "TAP", kTapFull, Family::Click, Length::Brief);
    out[n++] = single("P02", "TICK", 24, Family::Tick, Length::Brief);
    out[n++] = single("P03", "BUMP", 7, Family::Bump, Length::Brief);
    out[n++] = single("P04", "BUZZ", 14, Family::Buzz, Length::Brief);
    out[n++] = single("P05", "PULSE", 52, Family::Pulsing, Length::Brief);
    out[n++] = single("P06", "ALERT", 15, Family::Alert, Length::Brief);
    out[n++] = single("P07", "LONG", 16, Family::Alert, Length::Sustained);
    out[n++] = train("P08", "DOUBLE", tap2, 2, even, Family::Click, Rhythm::Even, Amplitude::Full);
    out[n++] = train("P09", "TRIPLE", tap3, 3, even, Family::Click, Rhythm::Even, Amplitude::Full);
    out[n++] = train("P10", "SKIP", tap3, 3, sl, Family::Click, Rhythm::ShortLong, Amplitude::Full);
    out[n++] = train("P11", "LIMP", tap3, 3, ls, Family::Click, Rhythm::LongShort, Amplitude::Full);
    out[n++] = train("P12", "TWOTICK", tick2, 2, even, Family::Tick, Rhythm::Even, Amplitude::Full);

    if (set == SetId::WithAmplitude) {
        Pattern mid = single("P13", "TAPMID", kTapMid, Family::Click, Length::Brief);
        mid.amplitude = Amplitude::Mid;
        out[n++] = mid;
        Pattern low = single("P14", "TAPLOW", kTapLow, Family::Click, Length::Brief);
        low.amplitude = Amplitude::Low;
        out[n++] = low;
        out[n++] = train("P15", "RISE", rise, 3, even, Family::Click, Rhythm::Even, Amplitude::Rising);
        out[n++] = train("P16", "FALL", fall, 3, even, Family::Click, Rhythm::Even, Amplitude::Falling);
    }

    return n;
}

inline uint8_t setSize(SetId set)
{
    switch (set) {
    case SetId::WithAmplitude: return kWithAmplitudeCount;
    case SetId::BumpLadder:    return kBumpLadderCount;
    case SetId::RhythmAndFamily:
    case SetId::Count:
        break;
    }
    return kRhythmAndFamilyCount;
}

/// Per-effect durations in ms, indexed by effect number, which Phase 1 fills in
/// and every timing in the app is derived from.
struct EffectDurations {
    uint16_t ms[kMaxEffect + 1];
    bool     measured;

    /// Every effect at `kNominalEffectMs`, which is a ceiling and not a guess.
    static EffectDurations nominal()
    {
        EffectDurations d {};
        for (size_t i = 0; i <= kMaxEffect; i++) {
            d.ms[i] = kNominalEffectMs;
        }
        d.measured = false;
        return d;
    }

    uint16_t of(uint8_t effect) const
    {
        return effect <= kMaxEffect ? ms[effect] : kNominalEffectMs;
    }
};

/// How long the app believes `pattern` takes, which is the only thing it can
/// know: `RequestVibroPlay` completing means accepted, never finished.
inline uint32_t modelledDurationMs(const Pattern &pattern, const EffectDurations &durations)
{
    uint32_t total = 0;
    for (uint8_t i = 0; i < pattern.noteCount; i++) {
        const Note &note = pattern.notes[i];
        total += note.effect != 0 ? durations.of(note.effect) : note.pauseMs;
    }
    return total;
}

/// The longest modelled pattern in the set, which is what the presentation
/// window is sized from so its length leaks nothing about which was played.
inline uint32_t longestModelledMs(const Pattern patterns[], uint8_t count,
                                  const EffectDurations &durations)
{
    uint32_t longest = 0;
    for (uint8_t i = 0; i < count; i++) {
        const uint32_t ms = modelledDurationMs(patterns[i], durations);
        if (ms > longest) {
            longest = ms;
        }
    }
    return longest;
}

constexpr uint32_t kFnvOffsetBasis = 0x811C9DC5u;
constexpr uint32_t kFnvPrime       = 0x01000193u;

inline uint32_t fnv1a(uint32_t hash, uint8_t byte)
{
    return (hash ^ byte) * kFnvPrime;
}

/// Identifies the set by its contents rather than by its name, so a
/// regenerated set that differs at all is a different hash in `session.json`.
inline uint32_t setHash(const Pattern patterns[], uint8_t count)
{
    uint32_t h = kFnvOffsetBasis;
    for (uint8_t i = 0; i < count; i++) {
        const Pattern &p = patterns[i];
        for (size_t c = 0; c < sizeof(p.id); c++) {
            h = fnv1a(h, static_cast<uint8_t>(p.id[c]));
        }
        h = fnv1a(h, p.noteCount);
        for (uint8_t n = 0; n < p.noteCount; n++) {
            h = fnv1a(h, p.notes[n].effect);
            h = fnv1a(h, static_cast<uint8_t>(p.notes[n].pauseMs & 0xFF));
            h = fnv1a(h, static_cast<uint8_t>(p.notes[n].pauseMs >> 8));
        }
    }
    return h;
}

/// Whether every note of `pattern` is something the wire format can carry.
inline bool isWellFormed(const Pattern &pattern)
{
    if (pattern.noteCount == 0 || pattern.noteCount > kMaxNotes) {
        return false;
    }
    for (uint8_t i = 0; i < pattern.noteCount; i++) {
        const Note &note = pattern.notes[i];
        const bool isPause  = note.effect == 0;
        const bool isEffect = note.pauseMs == 0;
        if (isPause == isEffect) {
            return false;
        }
        if (isPause && (note.pauseMs < kMinPauseMs || note.pauseMs > kMaxPauseMs ||
                        note.pauseMs % kPauseStepMs != 0)) {
            return false;
        }
        if (!isPause && (note.effect < kMinEffect || note.effect > kMaxEffect)) {
            return false;
        }
    }
    return true;
}

/// Whether the pattern needs Docs/LEDGER.md row V1 to hold to mean anything.
inline bool needsAmplitudeAxis(const Pattern &pattern)
{
    if (pattern.amplitude != Amplitude::Full) {
        return true;
    }
    for (uint8_t i = 0; i < pattern.noteCount; i++) {
        if (pattern.notes[i].effect != 0 && namedEffect(pattern.notes[i].effect) == nullptr) {
            return true;
        }
    }
    return false;
}

inline const char *rhythmName(Rhythm rhythm)
{
    switch (rhythm) {
    case Rhythm::Single:    return "single";
    case Rhythm::Even:      return "even";
    case Rhythm::ShortLong: return "short-long";
    case Rhythm::LongShort: return "long-short";
    case Rhythm::Count:     break;
    }
    return "unknown";
}

inline const char *amplitudeName(Amplitude amplitude)
{
    switch (amplitude) {
    case Amplitude::Full:    return "full";
    case Amplitude::Mid:     return "mid";
    case Amplitude::Low:     return "low";
    case Amplitude::Rising:  return "rising";
    case Amplitude::Falling: return "falling";
    case Amplitude::Count:   break;
    }
    return "unknown";
}

inline const char *lengthName(Length length)
{
    switch (length) {
    case Length::Brief:     return "brief";
    case Length::Sustained: return "sustained";
    case Length::Count:     break;
    }
    return "unknown";
}

} // namespace Haptic

#endif // HAPTICLAB_PATTERNS_HPP
