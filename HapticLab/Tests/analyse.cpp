/// Regenerates the whole report from a run directory, so every number in it is
/// recomputable and disagreeable-with.
///
/// It is built from the same headers the watch links, so there is one
/// implementation of the confusion matrix, the interval arithmetic, the
/// information transfer and the clique search rather than two that have to
/// agree. `Tools/analyse.sh` is how it is run; `Confusion_test.cpp` is what
/// holds the arithmetic.
///
///     hapticlab-analyse <run-dir> [<run-dir> ...]
///
/// Several run directories are pooled, which is how a masked and an unmasked
/// block of the same condition are read together. A no-contact run is never
/// pooled with a wrist run: the tool refuses, because pooling a control with
/// the data it controls is the one mistake that would hide the leakage it
/// exists to find.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Analysis/Confusion.hpp"
#include "Patterns.hpp"
#include "Records.hpp"

using namespace Haptic;
using namespace Haptic::Analysis;

namespace {

/// Chance on a three-alternative forced choice, which is what an above-chance
/// no-contact control is measured against.
constexpr double kChance = 1.0 / 3.0;

/// The rate below which a pair counts as distinguishable. Stated here rather
/// than chosen per report: a threshold picked after seeing the data is not a
/// threshold.
constexpr double kConfusionThreshold = 0.10;

std::vector<std::string> split(const std::string &line, char sep = ',')
{
    std::vector<std::string> out;
    std::string              field;
    std::istringstream       stream(line);
    while (std::getline(stream, field, sep)) {
        out.push_back(field);
    }
    return out;
}

/// A CSV whose columns are addressed by name, so a column added in the middle
/// does not silently shift every reader.
class Table
{
public:
    bool load(const std::string &path)
    {
        std::ifstream file(path);
        if (!file) {
            return false;
        }
        std::string line;
        if (!std::getline(file, line)) {
            return false;
        }
        const std::vector<std::string> names = split(line);
        for (size_t i = 0; i < names.size(); i++) {
            mColumns[names[i]] = i;
        }
        while (std::getline(file, line)) {
            if (line.empty()) {
                continue;
            }
            mRows.push_back(split(line));
        }
        return true;
    }

    size_t size() const { return mRows.size(); }
    bool   has(const std::string &column) const { return mColumns.count(column) != 0; }

    std::string get(size_t row, const std::string &column) const
    {
        const auto found = mColumns.find(column);
        if (found == mColumns.end() || found->second >= mRows[row].size()) {
            return "";
        }
        return mRows[row][found->second];
    }

    long number(size_t row, const std::string &column) const
    {
        const std::string value = get(row, column);
        return value.empty() ? 0 : std::strtol(value.c_str(), nullptr, 10);
    }

private:
    std::map<std::string, size_t> mColumns;
    std::vector<std::vector<std::string>> mRows;
};

/// Pulls one string field out of `session.json` without a JSON parser: the
/// header is written by one formatter and every field is on its own line.
std::string sessionField(const std::string &path, const std::string &key)
{
    std::ifstream file(path);
    std::string   line;
    const std::string needle = "\"" + key + "\":";
    while (std::getline(file, line)) {
        const size_t at = line.find(needle);
        if (at == std::string::npos) {
            continue;
        }
        std::string value = line.substr(at + needle.size());
        while (!value.empty() && (value.front() == ' ' || value.front() == '"')) {
            value.erase(value.begin());
        }
        while (!value.empty() && (value.back() == ',' || value.back() == '"' || value.back() == ' ')) {
            value.pop_back();
        }
        return value;
    }
    return "";
}

struct Session {
    std::string directory;
    std::string runId;
    std::string contact;
    std::string masking;
    std::string set;
    std::string endReason;
    std::string capsHit;
    std::string firmware;
    std::string appVersion;
    std::string seed;
};

struct Pooled {
    std::vector<Session> sessions;
    /// Pattern id to index, taken from `patterns.csv` so the report reads a run
    /// whose generator has since changed.
    std::map<std::string, uint8_t> index;
    std::vector<std::string>       ids;
    std::vector<std::string>       names;
    Tally                          tally;
    /// Accuracy per block index, over every session pooled.
    std::map<int, std::pair<int, int>> blockHits;
    std::map<int, std::string>         blockKinds;
    /// Reaction times of correct responses, per pattern.
    std::map<uint8_t, std::vector<long>> reactions;
    /// Every button event, by disposition, which is what the discarded presses
    /// are kept for.
    std::map<std::string, int> dispositions;
    int trials = 0;
    int trainingTrials = 0;
    int misses = 0;
    int timeouts = 0;
};

std::string percent(double value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", value * 100.0);
    return buf;
}

/// Two decimals, because the third is not a figure this many trials support.
std::string bits(double value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", value);
    return buf;
}

std::string interval(const Interval &value, uint16_t trials)
{
    if (!value.reportable) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "not reported (%u of %u trials)", trials,
                      kMinTrialsPerPair);
        return buf;
    }
    return percent(value.point) + " % (" + percent(value.low) + " to " + percent(value.high) + ")";
}

long median(std::vector<long> values)
{
    if (values.empty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

bool loadRun(const std::string &dir, Pooled &pooled)
{
    Table patterns;
    if (!patterns.load(dir + "/patterns.csv")) {
        std::cerr << dir << ": no patterns.csv\n";
        return false;
    }

    Session session;
    session.directory  = dir;
    session.runId      = sessionField(dir + "/session.json", "run_id");
    session.contact    = sessionField(dir + "/session.json", "contact");
    session.masking    = sessionField(dir + "/session.json", "masking");
    session.set        = sessionField(dir + "/session.json", "candidate_set");
    session.endReason  = sessionField(dir + "/session.json", "end_reason");
    session.capsHit    = sessionField(dir + "/session.json", "caps_hit");
    session.firmware   = sessionField(dir + "/session.json", "firmware");
    session.appVersion = sessionField(dir + "/session.json", "app_version");
    session.seed       = sessionField(dir + "/session.json", "seed");

    for (const Session &existing : pooled.sessions) {
        if (existing.contact != session.contact) {
            std::cerr << "refusing to pool " << dir << " (" << session.contact << ") with "
                      << existing.directory << " (" << existing.contact
                      << "): a control and the data it controls are separate reports\n";
            return false;
        }
    }

    for (size_t row = 0; row < patterns.size(); row++) {
        const std::string id = patterns.get(row, "pattern_id");
        if (id.empty() || pooled.index.count(id) != 0) {
            continue;
        }
        pooled.index[id] = static_cast<uint8_t>(pooled.ids.size());
        pooled.ids.push_back(id);
        pooled.names.push_back(patterns.get(row, "name"));
    }
    pooled.tally.patternCount = static_cast<uint8_t>(pooled.ids.size());

    Table trials;
    if (!trials.load(dir + "/trials.csv")) {
        std::cerr << dir << ": no trials.csv\n";
        return false;
    }

    for (size_t row = 0; row < trials.size(); row++) {
        const std::string presentedId = trials.get(row, "presented_id");
        if (pooled.index.count(presentedId) == 0) {
            continue;
        }
        const uint8_t presented = pooled.index[presentedId];

        uint8_t offered[3] = { presented, presented, presented };
        bool    complete   = true;
        for (int slot = 0; slot < 3; slot++) {
            const std::string id = trials.get(row, "offered_" + std::to_string(slot));
            if (pooled.index.count(id) == 0) {
                complete = false;
                break;
            }
            offered[slot] = pooled.index[id];
        }
        if (!complete) {
            continue;
        }

        const std::string response = trials.get(row, "response");
        const int         block    = static_cast<int>(trials.number(row, "block"));
        const bool        training = trials.get(row, "block_kind") == "training";
        pooled.trials++;
        if (training) {
            pooled.trainingTrials++;
        }

        if (response == "not-felt") {
            if (!training) {
                pooled.tally.addMiss(presented, offered);
            }
            pooled.misses++;
            continue;
        }
        if (response == "none") {
            // A window that closed with nothing pressed is not a miss and is
            // not evidence about a pair either.
            pooled.timeouts++;
            continue;
        }

        int slot = -1;
        if (response == "slot0") {
            slot = 0;
        } else if (response == "slot1") {
            slot = 1;
        } else if (response == "slot2") {
            slot = 2;
        }
        if (slot < 0) {
            continue;
        }

        const uint8_t responded = offered[slot];
        // The training block has feedback after every response, so its trials
        // are evidence about learning rather than about confusion. They are
        // counted per block and kept out of the matrix, which is why accuracy
        // by block is reported separately rather than averaged in.
        if (!training) {
            pooled.tally.addResponse(presented, responded, offered);
        }

        pooled.blockHits[block].second++;
        pooled.blockKinds[block] = training ? "training" : "test";
        if (responded == presented) {
            pooled.blockHits[block].first++;
            if (!training) {
                pooled.reactions[presented].push_back(trials.number(row, "reaction_ms"));
            }
        }
    }

    Table events;
    if (events.load(dir + "/events.csv")) {
        for (size_t row = 0; row < events.size(); row++) {
            pooled.dispositions[events.get(row, "disposition")]++;
        }
    }

    pooled.sessions.push_back(session);
    return true;
}

void report(const Pooled &pooled)
{
    const Tally &tally = pooled.tally;

    std::cout << "# HapticLab report\n\n";
    std::cout << "Generated by `hapticlab-analyse` from the run directories named below. "
                 "Every number here comes from `trials.csv`; nothing is inherited and nothing "
                 "is measured in the simulator, which can say nothing about any sensation.\n\n";

    std::cout << "## The runs\n\n";
    std::cout << "| run | condition | set | seed | firmware | app | ended | caps |\n";
    std::cout << "| --- | --- | --- | --- | --- | --- | --- | --- |\n";
    for (const Session &session : pooled.sessions) {
        std::cout << "| " << session.runId << " | " << session.contact << ", " << session.masking
                  << " | " << session.set << " | " << session.seed << " | " << session.firmware
                  << " | " << session.appVersion << " | " << session.endReason << " | "
                  << session.capsHit << " |\n";
    }
    std::cout << "\n**This is n=1 unless the table above has more than one wrist in it.** The "
                 "protocol exists so a second wrist can be added; one wrist's answer is one "
                 "wrist's answer.\n\n";

    const bool control = !pooled.sessions.empty() && pooled.sessions.front().contact == "table";

    // -- Overall -------------------------------------------------------------
    int hits = 0, answered = 0;
    for (const auto &block : pooled.blockHits) {
        const auto kind = pooled.blockKinds.find(block.first);
        if (kind != pooled.blockKinds.end() && kind->second == "training") {
            continue;
        }
        hits += block.second.first;
        answered += block.second.second;
    }
    const Interval overall = wilson(static_cast<uint16_t>(hits), static_cast<uint16_t>(answered));

    std::cout << "## Overall\n\n";
    std::cout << "- Trials: " << pooled.trials << ", of which " << pooled.misses
              << " were answered with the miss key and " << pooled.timeouts
              << " closed with no answer.\n";
    std::cout << "- " << pooled.trainingTrials
              << " of those were training trials, which had feedback after every response and "
                 "are counted by block below rather than in the matrix.\n";
    std::cout << "- Accuracy over answered test trials: " << interval(overall, static_cast<uint16_t>(answered))
              << ", against " << percent(kChance) << " % chance on three alternatives.\n";
    std::cout << "- Information transfer: " << bits(informationTransferBits(tally))
              << " bits, against " << bits(stimulusEntropyBits(tally))
              << " bits of stimulus entropy.\n\n";

    if (control) {
        std::cout << "### The no-contact control\n\n";
        if (overall.reportable && overall.low > kChance) {
            std::cout << "**Above chance with the watch off the wrist.** The interval's lower "
                         "bound is " << percent(overall.low)
                      << " %, above the " << percent(kChance)
                      << " % a guess would give, so this much of any contact result came through "
                         "hearing or sight rather than skin. Stop and find the leak before "
                         "believing the contact data.\n\n";
        } else if (overall.reportable) {
            std::cout << "At chance, which is what a control should be: the interval "
                      << interval(overall, static_cast<uint16_t>(answered))
                      << " includes " << percent(kChance) << " %.\n\n";
        } else {
            std::cout << "Too few trials to say. " << answered << " answered trials against a "
                      << kMinTrialsPerPair << "-trial floor.\n\n";
        }
    }

    // -- Learning ------------------------------------------------------------
    std::cout << "## Accuracy by block\n\n";
    std::cout << "Blocks in order, so learning is visible rather than averaged away. The "
                 "training block had feedback after every response, which is why its trials are "
                 "here and not in the confusion matrix.\n\n";
    std::cout << "| block | kind | answered | accuracy |\n| --- | --- | --- | --- |\n";
    for (const auto &block : pooled.blockHits) {
        const Interval accuracy = wilson(static_cast<uint16_t>(block.second.first),
                                         static_cast<uint16_t>(block.second.second));
        const auto kind = pooled.blockKinds.find(block.first);
        std::cout << "| " << block.first << " | "
                  << (kind == pooled.blockKinds.end() ? "unknown" : kind->second) << " | "
                  << block.second.second << " | "
                  << interval(accuracy, static_cast<uint16_t>(block.second.second)) << " |\n";
    }
    std::cout << "\n";

    // -- Per pattern ---------------------------------------------------------
    std::cout << "## By pattern\n\n";
    std::cout << "| pattern | presented | accuracy | misses | median reaction |\n";
    std::cout << "| --- | --- | --- | --- | --- |\n";
    for (uint8_t i = 0; i < tally.patternCount; i++) {
        const Interval accuracy = Analysis::accuracy(tally, i);
        const auto     found    = pooled.reactions.find(i);
        const long     rt       = found == pooled.reactions.end() ? 0 : median(found->second);
        std::cout << "| " << pooled.ids[i] << " " << pooled.names[i] << " | "
                  << tally.presentations[i] << " | "
                  << interval(accuracy, tally.presentations[i]) << " | " << tally.misses[i]
                  << " | " << (rt > 0 ? std::to_string(rt) + " ms" : "-") << " |\n";
    }
    std::cout << "\nA pattern identified slowly is a poor navigation cue even when it is "
                 "identified correctly, which is why the reaction time is here and not in a "
                 "footnote. It is measured from the frame that opened the response window, so it "
                 "carries the panel's own 100 ms tick.\n\n";

    // -- Confusion matrix ----------------------------------------------------
    std::cout << "## Pairwise confusion\n\n";
    std::cout << "Each cell is the rate at which the two were taken for each other, over the "
                 "trials in which both were on the screen, with a 95 % Wilson interval. A pair "
                 "with fewer than " << kMinTrialsPerPair
              << " such trials is not reported at all; the shortfall is given instead.\n\n";
    std::cout << "| pair | trials | confusions | rate |\n| --- | --- | --- | --- |\n";
    for (uint8_t a = 0; a < tally.patternCount; a++) {
        for (uint8_t b = static_cast<uint8_t>(a + 1); b < tally.patternCount; b++) {
            const PairCounts pair = tally.pairFor(a, b);
            if (pair.trials == 0) {
                continue;
            }
            const Interval rate = pairConfusion(tally, a, b);
            std::cout << "| " << pooled.ids[a] << " / " << pooled.ids[b] << " | " << pair.trials
                      << " | " << pair.confusions << " | " << interval(rate, pair.trials);
            if (!rate.reportable) {
                std::cout << ", " << trialsStillNeeded(tally, a, b) << " more needed";
            }
            std::cout << " |\n";
        }
    }
    std::cout << "\n";

    // -- The answer ----------------------------------------------------------
    const DistinguishableSet best = largestDistinguishableSet(tally, kConfusionThreshold);
    std::cout << "## The maximally distinguishable set\n\n";
    std::cout << "The largest group whose every internal pair is confused below "
              << percent(kConfusionThreshold)
              << " %, judged on the interval's upper bound rather than its point estimate: one "
                 "confusion in twelve is not proof of a rate under the threshold.\n\n";
    std::cout << "**" << int(best.size) << " patterns.**";
    if (best.undersampledPairs > 0) {
        std::cout << " " << best.undersampledPairs
                  << " pairs were dropped for want of trials rather than for confusion, so this "
                     "is a floor and not the answer.";
    }
    std::cout << "\n\n";
    std::cout << "| pattern | worst pair inside the set |\n| --- | --- |\n";
    for (uint8_t i = 0; i < tally.patternCount; i++) {
        if (!(best.members & (1u << i))) {
            continue;
        }
        const Interval worst = worstPairIn(tally, best.members, i);
        std::cout << "| " << pooled.ids[i] << " " << pooled.names[i] << " | "
                  << (worst.reportable ? interval(worst, kMinTrialsPerPair) : std::string("alone"))
                  << " |\n";
    }
    std::cout << "\n";

    if (best.size <= 2) {
        std::cout << "**Two or fewer is a result to stop on rather than to report.** Either the "
                     "patterns are not distinguishable on this wrist, or something in the "
                     "protocol is wrong; the no-contact control and the event log are where to "
                     "look first.\n\n";
    }

    // -- Events --------------------------------------------------------------
    std::cout << "## Every button event\n\n";
    std::cout << "Including the ones the trial logic ignored. A press during playback is evidence "
                 "the wearer decided before the pattern finished, which is a real result about "
                 "that pattern.\n\n";
    std::cout << "| disposition | count |\n| --- | --- |\n";
    for (const auto &entry : pooled.dispositions) {
        std::cout << "| " << entry.first << " | " << entry.second << " |\n";
    }
    std::cout << "\n";

    std::cout << "## For sanity only\n\n";
    std::cout << "Wrist and forearm tactile displays in the literature tend to land somewhere "
                 "around 2 to 3 bits of information transfer, which is roughly four to eight "
                 "reliably identified items. **UNVERIFIED here, and not this device's number.** "
                 "If the figure above is far outside that, look for a confound before believing "
                 "it.\n";
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::cerr << "usage: hapticlab-analyse <run-dir> [<run-dir> ...]\n";
        return 2;
    }

    Pooled pooled;
    for (int i = 1; i < argc; i++) {
        if (!loadRun(argv[i], pooled)) {
            return 1;
        }
    }
    if (pooled.tally.patternCount == 0) {
        std::cerr << "no patterns in any run\n";
        return 1;
    }

    report(pooled);
    return 0;
}
