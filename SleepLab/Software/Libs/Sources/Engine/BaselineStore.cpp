/**
 ******************************************************************************
 * @file    BaselineStore.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The wearer's own normal. Rationale is in the header.
 ******************************************************************************
 */

#include "Engine/BaselineStore.hpp"

namespace Engine
{

void BaselineStore::add(const Sample &s)
{
    mSamples[mNext] = s;
    mNext = (mNext + 1) % kWindowNights;
    if (mCount < kWindowNights) {
        mCount++;
    }
}

void BaselineStore::restore(const Sample *samples, size_t count, size_t nextSlot)
{
    mCount = (count < kWindowNights) ? count : kWindowNights;
    mNext  = (nextSlot < kWindowNights) ? nextSlot : 0;

    for (size_t i = 0; i < kWindowNights; ++i) {
        mSamples[i] = (samples != nullptr && i < kWindowNights) ? samples[i]
                                                                : Sample{};
    }
}

int32_t BaselineStore::median(const int32_t *values, size_t n)
{
    if (n == 0) {
        return kAbsent;
    }

    // Insertion sort into a local copy. n is at most kWindowNights (28), so
    // this is a few hundred comparisons once a night -- cheaper in code and in
    // stack than anything cleverer, and it leaves the caller's array alone.
    int32_t sorted[kWindowNights];
    for (size_t i = 0; i < n; ++i) {
        int32_t v = values[i];
        size_t  j = i;
        while (j > 0 && sorted[j - 1] > v) {
            sorted[j] = sorted[j - 1];
            --j;
        }
        sorted[j] = v;
    }

    // Even counts take the lower of the two middle values rather than their
    // mean. The values are integers in fixed units (bpm x10, whole percent),
    // and averaging would invent a precision the inputs do not have.
    return sorted[(n - 1) / 2];
}

BaselineStore::Delta BaselineStore::build(int32_t tonight,
                                          int32_t Sample::*field) const
{
    Delta d;
    d.nights = mCount;
    d.value  = tonight;

    if (mCount < kMinNights) {
        d.nightsNeeded = kMinNights - mCount;
        return d;   // available stays false; every field stays absent
    }

    // Only nights that actually carried this field. A night recorded with the
    // heart-rate sensor off contributes nothing to an HR baseline and must not
    // contribute a sentinel to it either.
    int32_t present[kWindowNights];
    size_t  n = 0;
    for (size_t i = 0; i < mCount; ++i) {
        const int32_t v = mSamples[i].*field;
        if (v != kAbsent) {
            present[n++] = v;
        }
    }

    if (n < kMinNights) {
        // Enough nights, but not enough of them measured *this*. The wearer
        // needs the same thing said: more nights, with that sensor on.
        d.nightsNeeded = kMinNights - n;
        return d;
    }

    d.baseline = median(present, n);

    if (tonight == kAbsent) {
        // A baseline exists but tonight has nothing to compare. The baseline
        // is still worth showing, so `available` is set -- what is absent is
        // the delta, and the caller can tell those apart.
        d.available = true;
        return d;
    }

    d.delta     = tonight - d.baseline;
    d.available = true;
    return d;
}

BaselineStore::Delta BaselineStore::hrMin(int32_t tonightX10) const
{
    return build(tonightX10, &Sample::hrMinX10);
}

BaselineStore::Delta BaselineStore::efficiency(int32_t tonightPct) const
{
    return build(tonightPct, &Sample::efficiencyPct);
}

BaselineStore::Delta BaselineStore::totalSleep(int32_t tonightMin) const
{
    return build(tonightMin, &Sample::totalSleepMin);
}

} // namespace Engine
