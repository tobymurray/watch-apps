#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

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
    for (SetId set : { SetId::RhythmAndFamily, SetId::WithAmplitude }) {
        for (const Pattern &pattern : setOf(set)) {
            EXPECT_TRUE(isWellFormed(pattern)) << pattern.id << " in " << setName(set);
            EXPECT_LE(pattern.noteCount, kMaxNotes) << pattern.id;
        }
    }
}

TEST(CandidateSet, TheSetSizesAreWhatSetSizePromises)
{
    EXPECT_EQ(setOf(SetId::RhythmAndFamily).size(), setSize(SetId::RhythmAndFamily));
    EXPECT_EQ(setOf(SetId::WithAmplitude).size(), setSize(SetId::WithAmplitude));
    EXPECT_EQ(setSize(SetId::WithAmplitude), kMaxPatterns);
}

TEST(CandidateSet, EveryIdAndNameIsDistinctAndFitsTheScreen)
{
    for (SetId set : { SetId::RhythmAndFamily, SetId::WithAmplitude }) {
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
    for (SetId set : { SetId::RhythmAndFamily, SetId::WithAmplitude }) {
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
