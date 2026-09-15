#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "EffectTable.hpp"
#include "Patterns.hpp"
#include "Protocol.hpp"

using namespace Haptic;

namespace {

std::vector<Pattern> setOf(SetId set)
{
    Pattern buf[kMaxPatterns] {};
    const uint8_t count = buildSet(set, buf);
    return std::vector<Pattern>(buf, buf + count);
}

} // namespace

TEST(CandidateSet, EveryPatternIsSomethingTheWireFormatCanCarry)
{
    for (SetId set : { SetId::RhythmAndFamily, SetId::WithAmplitude, SetId::BumpLadder }) {
        for (const Pattern &pattern : setOf(set)) {
            EXPECT_TRUE(isWellFormed(pattern)) << pattern.id << " in " << setName(set);
            EXPECT_LE(pattern.noteCount, kMaxNotes) << pattern.id;
        }
    }
}

TEST(CandidateSet, TheSetSizesAreWhatSetSizePromises)
{
    for (SetId set : { SetId::RhythmAndFamily, SetId::WithAmplitude, SetId::BumpLadder }) {
        EXPECT_EQ(setOf(set).size(), setSize(set)) << setName(set);
    }
    EXPECT_EQ(setSize(SetId::WithAmplitude), kMaxPatterns);
}

/// The bump ladder is the one amplitude ladder the actuator has, so the three
/// have to differ in amplitude and in nothing else: a difference in waveform,
/// pulse count, rhythm or note timing would give the wearer another cue and
/// the run would stop being an intensity test.
TEST(CandidateSet, TheBumpLadderVariesAmplitudeAndNothingElse)
{
    const std::vector<Pattern> ladder = setOf(SetId::BumpLadder);
    ASSERT_EQ(ladder.size(), 3u);

    std::set<uint8_t> amplitudes;
    std::set<uint8_t> effects;
    for (const Pattern &p : ladder) {
        EXPECT_EQ(p.family, Family::Bump) << p.id;
        EXPECT_EQ(p.pulses, 1) << p.id;
        EXPECT_EQ(p.rhythm, Rhythm::Single) << p.id;
        EXPECT_EQ(p.length, Length::Brief) << p.id;
        EXPECT_EQ(p.noteCount, 1) << p.id;
        EXPECT_EQ(ladder.front().noteCount, p.noteCount) << p.id;
        amplitudes.insert(static_cast<uint8_t>(p.amplitude));
        effects.insert(p.notes[0].effect);
    }
    EXPECT_EQ(amplitudes.size(), 3u) << "two of the three share an amplitude tier";
    EXPECT_EQ(effects, (std::set<uint8_t> { kBumpFull, kBumpMid, kBumpLow }));

    // Every one of them needs row V6 to hold, which is the point.
    for (const Pattern &p : ladder) {
        const bool needsAxis = needsAmplitudeAxis(p);
        EXPECT_EQ(needsAxis, p.notes[0].effect != kBumpFull) << p.id;
    }
}

TEST(CandidateSet, EveryIdAndNameIsDistinctAndFitsTheScreen)
{
    for (SetId set : { SetId::RhythmAndFamily, SetId::WithAmplitude, SetId::BumpLadder }) {
        std::set<std::string> ids;
        std::set<std::string> names;
        for (const Pattern &pattern : setOf(set)) {
            EXPECT_LE(std::strlen(pattern.id), kMaxPatternIdLength) << pattern.id;
            EXPECT_LE(std::strlen(pattern.name), kMaxPatternNameLength) << pattern.name;
            EXPECT_GT(std::strlen(pattern.name), 0u) << pattern.id << " has no name";
            EXPECT_TRUE(ids.insert(pattern.id).second) << pattern.id << " is not unique";
            EXPECT_TRUE(names.insert(pattern.name).second) << pattern.name << " is not unique";
        }
    }
}

/// A name a trial screen cannot draw is a name the wearer cannot read, and the
/// renderer's own test holds the width at eight characters of the widest glyph.
TEST(CandidateSet, EveryNameIsUpperCaseAndPlain)
{
    for (const Pattern &pattern : setOf(SetId::WithAmplitude)) {
        for (const char *c = pattern.name; *c != '\0'; c++) {
            const bool plain = (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9');
            EXPECT_TRUE(plain) << pattern.name << " has a character the atlas may not cover";
        }
    }
}

/// The smaller set exists so a run can proceed when Docs/LEDGER.md row V1 is
/// refuted, so nothing in it may depend on the amplitude axis.
TEST(CandidateSet, NothingInTheRhythmSetNeedsTheAmplitudeAxis)
{
    for (const Pattern &pattern : setOf(SetId::RhythmAndFamily)) {
        EXPECT_FALSE(needsAmplitudeAxis(pattern)) << pattern.id;
    }
}

/// And the larger one exists so the experiment can report that intensity alone
/// fails, which needs at least one pair that differs by intensity only.
TEST(CandidateSet, TheAmplitudeSetOffersAnIntensityOnlyTriple)
{
    const std::vector<Pattern> patterns = setOf(SetId::WithAmplitude);
    uint8_t intensityOnly = 0;
    for (const Pattern &pattern : patterns) {
        if (pattern.pulses == 1 && pattern.rhythm == Rhythm::Single &&
            pattern.family == Family::Click && needsAmplitudeAxis(pattern)) {
            intensityOnly++;
        }
    }
    EXPECT_GE(intensityOnly, 2) << "nothing here can refute the intensity axis";
}

TEST(CandidateSet, EveryRhythmAndEveryFamilyAxisIsExercised)
{
    std::set<uint8_t> rhythms;
    std::set<uint8_t> families;
    std::set<uint8_t> pulseCounts;
    std::set<uint8_t> lengths;
    for (const Pattern &pattern : setOf(SetId::RhythmAndFamily)) {
        rhythms.insert(static_cast<uint8_t>(pattern.rhythm));
        families.insert(static_cast<uint8_t>(pattern.family));
        pulseCounts.insert(pattern.pulses);
        lengths.insert(static_cast<uint8_t>(pattern.length));
    }
    EXPECT_EQ(rhythms.size(), static_cast<size_t>(Rhythm::Count));
    EXPECT_EQ(families.size(), static_cast<size_t>(Family::Count));
    EXPECT_EQ(pulseCounts, (std::set<uint8_t> { 1, 2, 3 }));
    EXPECT_EQ(lengths.size(), static_cast<size_t>(Length::Count));
}

TEST(CandidateSet, TheHashChangesWithTheNotesAndNotWithTheOrderOfTheCall)
{
    Pattern a[kMaxPatterns] {};
    Pattern b[kMaxPatterns] {};
    const uint8_t count = buildSet(SetId::RhythmAndFamily, a);
    buildSet(SetId::RhythmAndFamily, b);
    EXPECT_EQ(setHash(a, count), setHash(b, count));

    b[3].notes[0].effect++;
    EXPECT_NE(setHash(a, count), setHash(b, count));
}

TEST(Pauses, EveryPauseIsOnTheTenMillisecondStepAndInsideTheRange)
{
    EXPECT_EQ(quantisePause(0), 0);
    EXPECT_EQ(quantisePause(1), kMinPauseMs);
    EXPECT_EQ(quantisePause(84), 80);
    EXPECT_EQ(quantisePause(85), 90);
    EXPECT_EQ(quantisePause(9999), kMaxPauseMs);
    for (uint32_t ms = 1; ms <= 2000; ms++) {
        const uint16_t q = quantisePause(ms);
        EXPECT_EQ(q % kPauseStepMs, 0) << ms;
        EXPECT_GE(q, kMinPauseMs) << ms;
        EXPECT_LE(q, kMaxPauseMs) << ms;
    }
}

/// The presentation window is derived from the longest pattern rather than
/// chosen, and it has to cover every pattern in the set: a window shorter than
/// a pattern would take a response while the wrist was still being buzzed.
TEST(Timing, ThePresentationWindowCoversTheLongestPatternInTheSet)
{
    const EffectDurations nominal = EffectDurations::nominal();
    for (SetId set : { SetId::RhythmAndFamily, SetId::WithAmplitude, SetId::BumpLadder }) {
        const std::vector<Pattern> patterns = setOf(set);
        const Timing timing = timingFor(patterns.data(), static_cast<uint8_t>(patterns.size()), nominal);
        for (const Pattern &pattern : patterns) {
            EXPECT_LE(modelledDurationMs(pattern, nominal), timing.presentationMs) << pattern.id;
        }
        EXPECT_EQ(timing.presentationMs,
                  longestModelledMs(patterns.data(), static_cast<uint8_t>(patterns.size()), nominal) +
                      kPresentationMarginMs);
    }
}

/// The nominal duration is a ceiling taken from the only two effects whose own
/// names state one, so a measured table can only shorten the window.
TEST(Timing, AMeasuredDurationTableShortensTheWindowRatherThanLengtheningIt)
{
    const std::vector<Pattern> patterns = setOf(SetId::RhythmAndFamily);
    const Timing nominal =
        timingFor(patterns.data(), static_cast<uint8_t>(patterns.size()), EffectDurations::nominal());

    EffectDurations measured = EffectDurations::nominal();
    for (size_t i = 0; i <= kMaxEffect; i++) {
        measured.ms[i] = 120;
    }
    measured.ms[15]   = 750;
    measured.ms[16]   = 1000;
    measured.measured = true;

    const Timing after = timingFor(patterns.data(), static_cast<uint8_t>(patterns.size()), measured);
    EXPECT_LT(after.presentationMs, nominal.presentationMs);
}

TEST(Timing, TheNominalCeilingIsTheLongerOfTheTwoSelfTimedEffects)
{
    EXPECT_EQ(kNominalEffectMs, kSelfTimedDurations[1]);
    EXPECT_GE(kNominalEffectMs, kSelfTimedDurations[0]);
}

/// The wire format takes 1 to 127 and the sweep found five of those unusable,
/// so being in range is not enough to be in a pattern.
TEST(Effects, AnIndexTheSweepDisqualifiedIsNotAUsableEffect)
{
    EXPECT_TRUE(isUsableEffect(1));
    EXPECT_TRUE(isUsableEffect(117));
    EXPECT_TRUE(isUsableEffect(123));
    EXPECT_FALSE(isUsableEffect(0)) << "0 is a pause, never an effect";
    EXPECT_FALSE(isUsableEffect(118)) << "the buzz with no reachable stop";
    for (uint8_t effect = 124; effect <= 127; effect++) {
        EXPECT_FALSE(isUsableEffect(effect)) << int(effect) << " plays nothing";
    }
}

TEST(Patterns, APatternCannotBeBuiltOnADisqualifiedEffect)
{
    Pattern p {};
    p.noteCount       = 1;
    p.notes[0].effect = 123;
    EXPECT_TRUE(isWellFormed(p));

    for (uint8_t effect : { 118, 124, 125, 126, 127 }) {
        p.notes[0].effect = effect;
        EXPECT_FALSE(isWellFormed(p)) << "a pattern was allowed to use effect " << int(effect);
    }
}

/// Every candidate set has to be buildable out of effects that work, which is
/// the check that would have caught a set reaching for one that does not.
TEST(Patterns, EverySetUsesOnlyUsableEffects)
{
    for (uint8_t s = 0; s < static_cast<uint8_t>(SetId::Count); s++) {
        Pattern patterns[kMaxPatterns] {};
        const uint8_t count = buildSet(static_cast<SetId>(s), patterns);
        for (uint8_t i = 0; i < count; i++) {
            EXPECT_TRUE(isWellFormed(patterns[i]))
                << setName(static_cast<SetId>(s)) << " pattern " << patterns[i].id;
        }
    }
}

/// The measured table is what every timing now derives from, so the two effects
/// that name their own duration are the check that it was generated from a real
/// sweep rather than left at the ceiling.
TEST(EffectTable, TheMeasuredTableAgreesWithTheEffectsThatNameTheirDuration)
{
    const EffectDurations measured = measuredDurations();
    EXPECT_TRUE(measured.measured);
    EXPECT_NEAR(measured.of(15), 750, 40);
    EXPECT_NEAR(measured.of(16), 1000, 50);
    // And it is not the nominal table wearing a flag.
    EXPECT_LT(measured.of(1), kNominalEffectMs / 2);
    EXPECT_LT(measured.of(24), kNominalEffectMs / 2);
}

/// The whole point of measuring: the window was sized off a ceiling of a second
/// per effect, which made it nearly four seconds for patterns that finish in a
/// fraction of that.
TEST(EffectTable, TheMeasuredTableShrinksThePresentationWindow)
{
    Pattern patterns[kMaxPatterns] {};
    const uint8_t count = buildSet(SetId::RhythmAndFamily, patterns);

    const Timing guessed  = timingFor(patterns, count, EffectDurations::nominal());
    const Timing measured = timingFor(patterns, count, measuredDurations());

    EXPECT_LT(measured.presentationMs, guessed.presentationMs / 2);
    // Still covers the longest pattern in the set, which is what it is for.
    EXPECT_GT(measured.presentationMs, longestModelledMs(patterns, count, measuredDurations()));
    EXPECT_TRUE(measured.usable());
}
