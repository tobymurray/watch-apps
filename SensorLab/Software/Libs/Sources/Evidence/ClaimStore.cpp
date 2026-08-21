/**
 ******************************************************************************
 * @file    ClaimStore.cpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The claim table. Rationale is in ClaimStore.hpp.
 ******************************************************************************
 */

#include "Evidence/ClaimStore.hpp"

#include <cstring>

namespace SensorLab::Evidence
{

namespace
{

const char *const kVerdictNames[] = {
    "UNVERIFIED", "CONFIRMED", "LIKELY", "REFUTED", "INAPPLICABLE",
};

const char *const kConformanceNames[] = {
    "NO_CLAIM", "MATCHES", "DIFFERS", "UNTESTABLE",
};

const char *const kNoteText[] = {
    "",
    "fewer samples than this metric requires",
    "RequestDefault resolved no driver: nothing to subscribe to",
    "a driver resolved and delivered nothing",
    "classified as an event sensor: dt statistics do not apply",
    "the value never varied, so a quantisation step is meaningless",
    "delivered fewer fields than the shipped parser expects",
    "delivered more fields than the shipped parser expects",
    "frame is wider than the catalogue reserved field slots for",
    "run truncated: USB cable connected",
    "run truncated: device rebooted",
    "the probe for this layer is not built yet",
    "no reference value was entered",
};

static_assert(sizeof(kNoteText) / sizeof(kNoteText[0])
                  == static_cast<size_t>(Note::Count),
              "every Note needs its text, or the report prints an empty reason");

} // namespace

const char *toString(Verdict v)
{
    const size_t i = static_cast<size_t>(v);
    return (i < sizeof(kVerdictNames) / sizeof(kVerdictNames[0]))
               ? kVerdictNames[i] : "?";
}

const char *toString(Conformance c)
{
    const size_t i = static_cast<size_t>(c);
    return (i < sizeof(kConformanceNames) / sizeof(kConformanceNames[0]))
               ? kConformanceNames[i] : "?";
}

const char *toString(Note n)
{
    const size_t i = static_cast<size_t>(n);
    return (i < static_cast<size_t>(Note::Count)) ? kNoteText[i] : "?";
}

// ---------------------------------------------------------------------------

ClaimStore::ClaimStore()
{
    clear();
}

void ClaimStore::clear()
{
    const size_t want = Catalogue::claimCount();
    mCount = (want <= kSlots) ? want : kSlots;

    for (size_t i = 0; i < mCount; i++) {
        mClaims[i] = Claim {};
    }
    for (size_t i = 0; i < Catalogue::kTypeCount; i++) {
        mDescriptors[i][0] = '\0';
    }
    mSpreadsUsed    = 0;
    mSpreadsDropped = 0;
    mRunId          = 0;
}

Verdict ClaimStore::ceilingFor(Source s)
{
    switch (s) {
        // Reading settles what the *spec* says; only measurement settles what
        // the device does. Three rows in SleepLab's ledger were refuted by
        // reading code and none was ever confirmed that way, so REFUTED is the
        // ceiling rather than LIKELY: a spec read that contradicts a
        // measurement is decisive, and one that agrees with nothing is not.
        case Source::SpecRead: return Verdict::Refuted;
        // A value computed from other claims in this profile. Well-defined, and
        // no stronger than the claims under it.
        case Source::Derived:  return Verdict::Likely;
        case Source::Measured: return Verdict::Confirmed;
    }
    return Verdict::Unverified;
}

uint32_t ClaimStore::minimumNFor(size_t claimIdx)
{
    const Catalogue::ClaimRef r = Catalogue::describe(claimIdx);
    switch (r.kind) {
        case Catalogue::ClaimRef::Kind::PerType:
            return Catalogue::metricMinimumN(r.metric);
        case Catalogue::ClaimRef::Kind::PerField:
            return Catalogue::fieldMetricMinimumN(r.fieldMetric);
        case Catalogue::ClaimRef::Kind::Bespoke:
            return Catalogue::kBespoke[r.bespokeIdx].minimumN;
    }
    return 1;
}

Verdict ClaimStore::record(size_t claimIdx, const Measurement &m,
                           uint32_t uptimeMs, int64_t wallUtc)
{
    // No claim without a run. A row that cannot say which run produced it
    // cannot be diffed, cannot be re-derived, and cannot be defended.
    if (mRunId == 0 || claimIdx >= mCount) {
        return Verdict::Unverified;
    }

    Claim &c = mClaims[claimIdx];

    // An INAPPLICABLE claim is not measured over silently. Clearing it is an
    // explicit act -- see clearInapplicable() -- because a protocol that was
    // switched off for a stated reason coming back to life is a finding, not a
    // routine update.
    if (c.verdict == Verdict::Inapplicable && m.verdict != Verdict::Inapplicable) {
        return Verdict::Inapplicable;
    }

    c.value    = m.hasValue ? m.value : 0.0f;
    c.n        = m.n;
    c.uptimeMs = uptimeMs;
    c.wallUtc  = wallUtc;
    c.runId    = mRunId;
    c.note     = m.note;
    c.flags    = 0;
    if (m.hasValue) {
        c.flags |= Flag::kHasValue;
    }
    if (m.source != Source::Measured) {
        c.flags |= Flag::kInferred;
    }

    // A distribution's quantiles, if there is room. When there is not, the row
    // keeps its headline value and loses its spread -- and `spreadsDropped()`
    // says so, rather than the profile quietly reporting a rate with no spread
    // as though none existed.
    if (m.hasSpread) {
        if (c.spreadIdx == kNoSpread && mSpreadsUsed < kMaxSpreads) {
            c.spreadIdx = static_cast<uint16_t>(mSpreadsUsed++);
        }
        if (c.spreadIdx != kNoSpread) {
            mSpreads[c.spreadIdx] = m.spread;
            c.flags |= Flag::kHasSpread;
        } else {
            mSpreadsDropped++;
        }
    }

    // The minimum-n rule. The value is kept -- the screen shows progress and
    // the report can say "148 of 10 000" -- but the verdict is not promoted,
    // because the verdict is the only thing a reader acts on.
    const uint32_t minN = minimumNFor(claimIdx);
    const bool     wantsPromotion =
        (m.verdict == Verdict::Confirmed || m.verdict == Verdict::Likely);
    if (wantsPromotion && m.n < minN) {
        c.verdict     = Verdict::Unverified;
        c.note        = Note::BelowMinimumN;
        c.conformance = Conformance::NoClaim;
        return c.verdict;
    }

    // Reading cannot confirm. A probe that read a header and wants CONFIRMED
    // gets LIKELY at most; one that read a header and found the device
    // disagreeing gets REFUTED, which is what reading is *for*.
    Verdict v = m.verdict;
    if (v == Verdict::Confirmed) {
        const Verdict ceiling = ceilingFor(m.source);
        if (ceiling == Verdict::Likely) {
            v = Verdict::Likely;
        } else if (ceiling == Verdict::Refuted) {
            // A SpecRead cannot assert a positive fact about the device at all.
            v = Verdict::Unverified;
        }
    }
    c.verdict = v;

    // Conformance, against the spec's own claim where the catalogue has one.
    const Catalogue::Expectation e = Catalogue::expectationFor(claimIdx);
    if (!e.hasValue || !m.hasValue) {
        // Quote the spec or make no claim. No paraphrasing, no "probably".
        c.conformance = Conformance::NoClaim;
    } else {
        // Exact float equality is correct here and not a slip: every
        // catalogue expectation is a small integer count read out of a header
        // -- a field count -- and a tolerance would let a frame one field wide
        // of the parser read as conforming. A metric whose expectation is a
        // physical figure gets its own comparison when the datasheet rows land
        // in Docs/EXPECTED.md, on the host, where the tolerance can be stated
        // next to the source it came from.
        c.conformance = (m.value == e.value) ? Conformance::Matches
                                            : Conformance::Differs;
    }

    return c.verdict;
}

void ClaimStore::markInapplicable(size_t claimIdx, Note note, uint32_t uptimeMs,
                                  int64_t wallUtc)
{
    if (claimIdx >= mCount) {
        return;
    }
    Claim &c = mClaims[claimIdx];
    // Deliberately not gated on mRunId: "this claim cannot exist for this
    // sensor" is a statement about the catalogue and the firmware, not a
    // measurement, and the bespoke list's own INAPPLICABLE reasons are applied
    // at construction before any run is open.
    c.verdict     = Verdict::Inapplicable;
    c.note        = note;
    c.conformance = Conformance::NoClaim;
    c.n           = 0;
    c.flags       = 0;
    c.uptimeMs    = uptimeMs;
    c.wallUtc     = wallUtc;
    c.runId       = mRunId;
}

void ClaimStore::clearInapplicable(size_t claimIdx)
{
    if (claimIdx >= mCount) {
        return;
    }
    Claim &c = mClaims[claimIdx];
    if (c.verdict != Verdict::Inapplicable) {
        return;
    }
    c = Claim {};
}

const Claim &ClaimStore::at(size_t claimIdx) const
{
    static const Claim kEmpty {};
    return (claimIdx < mCount) ? mClaims[claimIdx] : kEmpty;
}

const Spread *ClaimStore::spreadFor(size_t claimIdx) const
{
    if (claimIdx >= mCount) {
        return nullptr;
    }
    const Claim &c = mClaims[claimIdx];
    if (!c.hasSpread() || c.spreadIdx >= mSpreadsUsed) {
        return nullptr;
    }
    return &mSpreads[c.spreadIdx];
}

void ClaimStore::setDescriptor(size_t typeIdx, const char *desc)
{
    if (typeIdx >= Catalogue::kTypeCount) {
        return;
    }
    if (desc == nullptr) {
        mDescriptors[typeIdx][0] = '\0';
        return;
    }
    // `RequestGetDesc::desc` is a 32-char array with no guarantee of a
    // terminator, so copy at most 32 and terminate here. A driver name that
    // filled the field exactly would otherwise run into whatever follows it in
    // the pool block.
    size_t i = 0;
    for (; i < kDescriptorMax - 1 && desc[i] != '\0'; i++) {
        mDescriptors[typeIdx][i] = desc[i];
    }
    mDescriptors[typeIdx][i] = '\0';
}

const char *ClaimStore::descriptor(size_t typeIdx) const
{
    return (typeIdx < Catalogue::kTypeCount) ? mDescriptors[typeIdx] : "";
}

// ---------------------------------------------------------------------------
// Completeness
// ---------------------------------------------------------------------------

namespace
{

void tally(Completeness &out, const Claim &c)
{
    if (c.verdict == Verdict::Inapplicable) {
        // Excluded from the denominator: a dt distribution for an event sensor
        // is not a gap in the profile, and counting it as one would make every
        // event sensor look permanently half-measured.
        out.inapplicable++;
        return;
    }
    out.applicable++;
    if (c.verdict != Verdict::Unverified) {
        out.answered++;
    }
    switch (c.verdict) {
        case Verdict::Confirmed: out.confirmed++; break;
        case Verdict::Likely:    out.likely++;    break;
        case Verdict::Refuted:   out.refuted++;   break;
        default: break;
    }
}

} // namespace

Completeness ClaimStore::completenessForType(size_t typeIdx) const
{
    Completeness out {};
    if (typeIdx >= Catalogue::kTypeCount) {
        return out;
    }
    for (size_t i = 0; i < mCount; i++) {
        if (Catalogue::describe(i).typeIdx == typeIdx) {
            tally(out, mClaims[i]);
        }
    }
    return out;
}

Completeness ClaimStore::completenessForType(size_t typeIdx,
                                             Catalogue::Layer layer) const
{
    Completeness out {};
    if (typeIdx >= Catalogue::kTypeCount) {
        return out;
    }
    for (size_t i = 0; i < mCount; i++) {
        const Catalogue::ClaimRef r = Catalogue::describe(i);
        if (r.typeIdx == typeIdx && r.layer == layer) {
            tally(out, mClaims[i]);
        }
    }
    return out;
}

Completeness ClaimStore::completenessForLayer(Catalogue::Layer layer) const
{
    Completeness out {};
    for (size_t i = 0; i < mCount; i++) {
        if (Catalogue::describe(i).layer == layer) {
            tally(out, mClaims[i]);
        }
    }
    return out;
}

Completeness ClaimStore::completenessOverall() const
{
    Completeness out {};
    for (size_t i = 0; i < mCount; i++) {
        tally(out, mClaims[i]);
    }
    return out;
}

} // namespace SensorLab::Evidence
