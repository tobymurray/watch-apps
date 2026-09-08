#ifndef HAPTICLAB_SCHEDULE_HPP
#define HAPTICLAB_SCHEDULE_HPP

#include <cstdint>

#include "Patterns.hpp"

/// The trial plan: which triples are offered, which member of each is played,
/// and in what order. A pure function of the set, the pass count and the seed,
/// so a run recorded with those three in `session.json` can be replanned
/// exactly. Why triples and not the whole set at once: HapticLab/README.md.
namespace Haptic {

/// xorshift32, because the plan has to be reproducible from the seed alone and
/// a library generator is neither on the watch nor identical across builds.
class Rng {
public:
    explicit Rng(uint32_t seed)
        : mState(seed != 0 ? seed : 0x1D872B41u)
    {}

    uint32_t next()
    {
        mState ^= mState << 13;
        mState ^= mState >> 17;
        mState ^= mState << 5;
        return mState;
    }

    /// Uniform on [0, bound), rejecting the tail rather than taking a modulus
    /// that would favour the low values.
    uint32_t below(uint32_t bound)
    {
        if (bound == 0) {
            return 0;
        }
        const uint32_t limit = UINT32_MAX - (UINT32_MAX % bound);
        uint32_t       value = next();
        while (value >= limit) {
            value = next();
        }
        return value % bound;
    }

private:
    uint32_t mState;
};

struct Triple {
    /// Ascending, which is also the order the three are drawn in: a screen
    /// order that depended on which was presented would leak the answer.
    uint8_t member[3];
};

/// One presentation: which triple was offered and which of its members played.
struct Trial {
    uint16_t triple;   ///< Index into the plan's triple list.
    uint8_t  slot;     ///< Which of the triple's three was presented, 0-2.
    uint8_t  block;    ///< Index into the plan's block list.
};

enum class BlockKind : uint8_t {
    Training = 0, ///< Feedback after every response.
    Test,         ///< No feedback.
    Count,
};

struct Block {
    BlockKind kind;
    uint16_t  firstTrial;
    uint16_t  trialCount;
};

/// Sized for the widest run the app offers: sixteen patterns at the default
/// pass count needs about 164 triples, three trials each, plus the training
/// block, and a rest every `kTrialsPerBlock`. A capacity that truncated the
/// plan would starve the last pairs silently, which is what these were too
/// small for on the first attempt.
constexpr uint16_t kMaxTriples = 256;
constexpr uint16_t kMaxTrials  = 1024;
constexpr uint8_t  kMaxBlocks  = 48;

/// Trials in one test block, chosen so a block is a few minutes rather than a
/// sitting: at the inter-trial interval this app uses, a block of this many is
/// a comparable length to the training block.
constexpr uint16_t kTrialsPerBlock = 24;

/// How many times each pattern is presented with feedback before the test
/// blocks start, which is what "a learned set" means here.
constexpr uint8_t kTrainingPresentationsEach = 2;

/// How many times every pair is offered by default. Four, because a pair's
/// cell needs twelve trials before the analysis will report it and each triple
/// use is worth three. Fewer is a run whose matrix refuses to answer; the cost
/// of more is in HapticLab/README.md under "What a run costs".
constexpr uint8_t kDefaultTestPasses = 4;

inline const char *blockKindName(BlockKind kind)
{
    switch (kind) {
    case BlockKind::Training: return "training";
    case BlockKind::Test:     return "test";
    case BlockKind::Count:    break;
    }
    return "unknown";
}

struct Plan {
    uint8_t  patternCount;
    uint16_t tripleCount;
    uint16_t trialCount;
    uint8_t  blockCount;
    Triple   triples[kMaxTriples];
    Trial    trials[kMaxTrials];
    Block    blocks[kMaxBlocks];
    /// How many trials each unordered pair of patterns co-occurs in, flattened
    /// row-major over `patternCount`, so the balance is a number the session
    /// file carries rather than a claim about this function.
    uint16_t pairTrials[kMaxPatterns * kMaxPatterns];

    uint16_t pairTrialsFor(uint8_t a, uint8_t b) const
    {
        const uint8_t lo = a < b ? a : b;
        const uint8_t hi = a < b ? b : a;
        return pairTrials[static_cast<uint16_t>(lo) * kMaxPatterns + hi];
    }
};

namespace detail {

inline uint16_t &pairSlot(uint16_t counts[], uint8_t a, uint8_t b)
{
    const uint8_t lo = a < b ? a : b;
    const uint8_t hi = a < b ? b : a;
    return counts[static_cast<uint16_t>(lo) * kMaxPatterns + hi];
}

/// Pairs a set of `n` needs covered, over three pairs a triple.
inline uint16_t triplesForOnePass(uint8_t n)
{
    const uint16_t pairs = static_cast<uint16_t>(n) * (n - 1) / 2;
    return static_cast<uint16_t>((pairs + 2) / 3);
}

} // namespace detail

/// Every triple contributes three trials, one per member presented, and a
/// pair that is in the triple is on the screen for all three, so a pair's trial
/// count is three times how many triples contain it. That is the arithmetic
/// behind `testPasses`: the analysis refuses a pair with fewer than twelve
/// trials, so a pair needs four triples before its cell says anything.
constexpr uint16_t kTrialsPerTripleUse = 3;

/// Chooses triples until **every** pair has been offered at least `passes`
/// times, rather than until a fixed count has been drawn.
///
/// The fixed count is what a pass "should" cost, and drawing exactly that many
/// leaves the spread at two: some pairs end up covered once where others are
/// covered three times, and a pair covered once has three trials behind it
/// against a twelve-trial floor. Running to the floor instead costs about four
/// extra triples on twelve patterns and turns "probably balanced" into a
/// guarantee, which is what the report needs.
///
/// Each step takes a pair that is currently least covered, then the third
/// member that leaves the fewest pairs behind, breaking what is tied with
/// `rng`.
inline uint16_t planTriples(uint8_t n, uint8_t passes, Rng &rng, Triple out[], uint16_t cap)
{
    if (n < 3 || passes == 0) {
        return 0;
    }

    uint16_t covered[kMaxPatterns * kMaxPatterns] = {};
    uint16_t chosen = 0;

    while (chosen < cap) {
        uint16_t lowest = UINT16_MAX;
        for (uint8_t a = 0; a < n; a++) {
            for (uint8_t b = static_cast<uint8_t>(a + 1); b < n; b++) {
                const uint16_t count = detail::pairSlot(covered, a, b);
                if (count < lowest) {
                    lowest = count;
                }
            }
        }
        if (lowest >= passes) {
            break;
        }

        // One of the least-covered pairs, chosen without collecting them all.
        uint16_t candidates = 0;
        uint8_t  a = 0, b = 1;
        for (uint8_t i = 0; i < n; i++) {
            for (uint8_t j = static_cast<uint8_t>(i + 1); j < n; j++) {
                if (detail::pairSlot(covered, i, j) != lowest) {
                    continue;
                }
                candidates++;
                if (rng.below(candidates) == 0) {
                    a = i;
                    b = j;
                }
            }
        }

        // The third member that brings the most other least-covered pairs up
        // with it, and failing that the one whose pairs are least covered.
        int32_t  bestHelps = -1;
        uint32_t bestSum   = UINT32_MAX;
        uint16_t ties      = 0;
        uint8_t  c         = a == 0 || b == 0 ? (a == 1 || b == 1 ? 2 : 1) : 0;
        for (uint8_t k = 0; k < n; k++) {
            if (k == a || k == b) {
                continue;
            }
            const uint16_t ac = detail::pairSlot(covered, a, k);
            const uint16_t bc = detail::pairSlot(covered, b, k);
            const int32_t  helps = (ac == lowest ? 1 : 0) + (bc == lowest ? 1 : 0);
            const uint32_t sum   = static_cast<uint32_t>(ac) + bc;

            if (helps > bestHelps || (helps == bestHelps && sum < bestSum)) {
                bestHelps = helps;
                bestSum   = sum;
                ties      = 1;
                c         = k;
            } else if (helps == bestHelps && sum == bestSum) {
                ties++;
                if (rng.below(ties) == 0) {
                    c = k;
                }
            }
        }

        detail::pairSlot(covered, a, b)++;
        detail::pairSlot(covered, a, c)++;
        detail::pairSlot(covered, b, c)++;

        uint8_t member[3] = { a, b, c };
        for (uint8_t i = 0; i < 3; i++) {
            for (uint8_t j = static_cast<uint8_t>(i + 1); j < 3; j++) {
                if (member[j] < member[i]) {
                    const uint8_t tmp = member[i];
                    member[i]         = member[j];
                    member[j]         = tmp;
                }
            }
        }
        out[chosen].member[0] = member[0];
        out[chosen].member[1] = member[1];
        out[chosen].member[2] = member[2];
        chosen++;
    }

    return chosen;
}

/// Shuffles `trials`, then reorders them so no pattern is presented twice in a
/// row: at each position it takes the pattern with the most presentations still
/// to place, which is the standard way to space a multiset out and cannot get
/// stuck while no single pattern holds more than half of what is left.
///
/// A plain shuffle with a repair pass was the first attempt and it left
/// repeats at the tail, where every remaining trial was the same pattern.
/// Adaptation to one pattern would be measured as identification of it, so
/// "usually spaced" was not good enough.
inline void interleaveWithoutRepeats(Trial trials[], uint16_t count, const Triple triples[],
                                     uint8_t patternCount, Rng &rng)
{
    for (uint16_t i = count; i > 1; i--) {
        const uint16_t j = static_cast<uint16_t>(rng.below(i));
        const Trial    tmp = trials[i - 1];
        trials[i - 1]      = trials[j];
        trials[j]          = tmp;
    }

    const auto presented = [&](const Trial &t) { return triples[t.triple].member[t.slot]; };

    uint16_t remaining[kMaxPatterns] = {};
    for (uint16_t i = 0; i < count; i++) {
        remaining[presented(trials[i])]++;
    }

    uint8_t last = 0xFF;
    for (uint16_t i = 0; i < count; i++) {
        uint8_t  chosen    = 0xFF;
        uint16_t mostLeft  = 0;
        uint16_t ties      = 0;
        for (uint8_t p = 0; p < patternCount; p++) {
            if (remaining[p] == 0 || p == last) {
                continue;
            }
            if (remaining[p] > mostLeft) {
                mostLeft = remaining[p];
                ties     = 1;
                chosen   = p;
            } else if (remaining[p] == mostLeft) {
                ties++;
                if (rng.below(ties) == 0) {
                    chosen = p;
                }
            }
        }
        // Only the pattern just presented is left, which the size condition
        // makes impossible for a balanced plan but which is not worth crashing
        // over: the repeat is in the data either way.
        if (chosen == 0xFF) {
            chosen = last;
        }

        for (uint16_t j = i; j < count; j++) {
            if (presented(trials[j]) == chosen) {
                const Trial tmp = trials[i];
                trials[i]       = trials[j];
                trials[j]       = tmp;
                break;
            }
        }
        remaining[chosen]--;
        last = chosen;
    }
}

/// Builds the whole plan. `testPasses` passes of pair coverage make the test
/// blocks; the training block is `kTrainingPresentationsEach` presentations of
/// every pattern, drawn from the same triples so training and test ask the
/// same discrimination.
inline void buildPlan(SetId set, uint8_t testPasses, uint32_t seed, Plan &out)
{
    out = Plan {};
    Rng rng(seed);

    out.patternCount = setSize(set);
    out.tripleCount  = planTriples(out.patternCount, testPasses, rng, out.triples, kMaxTriples);

    // Training first, and only from triples already in the plan: a pattern the
    // wearer never saw offered against a given rival in training is not what
    // the test blocks then ask about.
    uint16_t trainingSeen[kMaxPatterns] = {};
    uint16_t trainingCount              = 0;
    for (uint8_t pass = 0; pass < kTrainingPresentationsEach; pass++) {
        for (uint16_t t = 0; t < out.tripleCount && trainingCount < kMaxTrials; t++) {
            for (uint8_t slot = 0; slot < 3 && trainingCount < kMaxTrials; slot++) {
                const uint8_t pattern = out.triples[t].member[slot];
                if (trainingSeen[pattern] > pass) {
                    continue;
                }
                trainingSeen[pattern]++;
                out.trials[trainingCount].triple = t;
                out.trials[trainingCount].slot   = slot;
                out.trials[trainingCount].block  = 0;
                trainingCount++;
            }
        }
    }
    interleaveWithoutRepeats(out.trials, trainingCount, out.triples, out.patternCount, rng);

    out.blocks[0].kind       = BlockKind::Training;
    out.blocks[0].firstTrial = 0;
    out.blocks[0].trialCount = trainingCount;
    out.blockCount           = 1;

    // Every triple contributes three trials, one for each member presented, so
    // no pattern is over-represented as the answer within a triple.
    Trial    test[kMaxTrials];
    uint16_t testCount = 0;
    for (uint16_t t = 0; t < out.tripleCount; t++) {
        for (uint8_t slot = 0; slot < 3 && testCount < kMaxTrials - trainingCount; slot++) {
            test[testCount].triple = t;
            test[testCount].slot   = slot;
            test[testCount].block  = 0;
            testCount++;
        }
    }
    interleaveWithoutRepeats(test, testCount, out.triples, out.patternCount, rng);

    uint16_t written = trainingCount;
    uint16_t taken   = 0;
    while (taken < testCount && out.blockCount < kMaxBlocks && written < kMaxTrials) {
        const uint8_t  block  = out.blockCount;
        uint16_t       inThis = 0;
        while (taken < testCount && inThis < kTrialsPerBlock && written < kMaxTrials) {
            out.trials[written]       = test[taken];
            out.trials[written].block = block;
            written++;
            taken++;
            inThis++;
        }
        out.blocks[block].kind       = BlockKind::Test;
        out.blocks[block].firstTrial = static_cast<uint16_t>(written - inThis);
        out.blocks[block].trialCount = inThis;
        out.blockCount++;
    }
    out.trialCount = written;

    // Test trials only: the training block has feedback after every response,
    // so its trials are evidence about learning rather than about confusion,
    // and the analysis leaves them out of the matrix for the same reason.
    for (uint16_t i = 0; i < out.trialCount; i++) {
        if (out.blocks[out.trials[i].block].kind != BlockKind::Test) {
            continue;
        }
        const Triple &triple = out.triples[out.trials[i].triple];
        for (uint8_t a = 0; a < 3; a++) {
            for (uint8_t b = static_cast<uint8_t>(a + 1); b < 3; b++) {
                detail::pairSlot(out.pairTrials, triple.member[a], triple.member[b])++;
            }
        }
    }
}

/// The presented pattern of a trial, by index into the candidate set.
inline uint8_t presentedPattern(const Plan &plan, uint16_t trial)
{
    return plan.triples[plan.trials[trial].triple].member[plan.trials[trial].slot];
}

/// How many test trials the least- and most-covered pair appear in, which is
/// the balance claim `session.json` records instead of asserting.
inline void pairBalance(const Plan &plan, uint16_t &lowest, uint16_t &highest)
{
    lowest  = UINT16_MAX;
    highest = 0;
    for (uint8_t a = 0; a < plan.patternCount; a++) {
        for (uint8_t b = static_cast<uint8_t>(a + 1); b < plan.patternCount; b++) {
            const uint16_t count = plan.pairTrialsFor(a, b);
            if (count < lowest) {
                lowest = count;
            }
            if (count > highest) {
                highest = count;
            }
        }
    }
    if (lowest == UINT16_MAX) {
        lowest = 0;
    }
}

} // namespace Haptic

#endif // HAPTICLAB_SCHEDULE_HPP
