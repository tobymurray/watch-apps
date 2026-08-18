/**
 ******************************************************************************
 * @file    EpochCounter.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Accelerometer samples in, activity counts out. Spec in the header.
 ******************************************************************************
 */

#include "Engine/EpochCounter.hpp"

#include <cmath>

namespace Engine
{
namespace {

/// 2*pi, for turning a corner frequency into a time constant.
constexpr float kTwoPi = 6.28318530718f;

/// Time constant of a one-pole filter at @p hz, in seconds.
constexpr float tau(float hz) { return 1.0f / (kTwoPi * hz); }

} // namespace

void EpochCounter::Axis::prime(float v)
{
    hp1In  = v;
    hp1Out = 0.0f;
    hp2Out = 0.0f;
    lp1Out = 0.0f;
    lp2Out = 0.0f;
}

float EpochCounter::Axis::step(float v, float dt, float aHp, float bLp)
{
    (void)dt;   // the caller has already folded dt into aHp and bLp

    // Two one-pole high-passes in cascade:
    //   y[n] = a * (y[n-1] + x[n] - x[n-1])
    // As dt -> 0 each passes everything; as dt grows each approaches a plain
    // difference. Both are the correct limits.
    const float hp1 = aHp * (hp1Out + v - hp1In);
    hp1In  = v;
    // The second pole's "previous input" is the first pole's previous output,
    // which is exactly what hp1Out still holds at this point.
    const float hp2 = aHp * (hp2Out + hp1 - hp1Out);
    hp1Out = hp1;
    hp2Out = hp2;

    // Two one-pole low-passes in cascade:
    //   y[n] = y[n-1] + b * (x[n] - y[n-1])
    lp1Out += bLp * (hp2    - lp1Out);
    lp2Out += bLp * (lp1Out - lp2Out);

    return std::fabs(lp2Out);
}

void EpochCounter::reset()
{
    *this = EpochCounter{};
}

void EpochCounter::add(uint32_t timestampMs, float x, float y, float z)
{
    const float in[3] = { x, y, z };

    // A sample that is not a number is not evidence, and it is worse than no
    // evidence: the filter state deliberately survives an epoch boundary -- which
    // is right, and which means one bad value reaches all five poles and every
    // epoch after it for the rest of the night integrates to NaN. The saturation
    // guard in closeEpoch() tests `>= kMax`, which NaN fails, so the conversion
    // to uint32_t is undefined and in practice zero.
    //
    // Zero is the most dangerous value this class can emit. It is what a rigid
    // object on furniture looks like and it is what the soundest sleep of the
    // night looks like, and the sample count stays healthy either way -- so
    // neither the recorder's data-gap flag nor the scorer's thin-epoch guard
    // fires and nothing downstream can tell. Measured before this guard: count
    // and peak both exactly 0, for every subsequent epoch, from one NaN.
    //
    // Dropped rather than clamped. There is no value to substitute: the
    // accelerometer did not report a number, so the honest contribution is none,
    // and `samples` not being incremented is what carries that forward -- an
    // epoch that loses enough samples this way becomes a data gap, which is what
    // it is.
    for (int a = 0; a < 3; ++a) {
        if (!std::isfinite(in[a])) {
            return;
        }
    }

    if (!mPrimed) {
        // Seed at the current value rather than at zero. Starting from zero
        // would push a full 1 g step through the high-pass and emit a settling
        // transient several seconds long into the first epoch of every night.
        for (int a = 0; a < 3; ++a) {
            mAxis[a].prime(in[a]);
        }
        mLastTsMs = timestampMs;
        mPrimed   = true;
        mSamples++;
        return;
    }

    // Unsigned difference, so the sensor timestamp wrapping is handled.
    const uint32_t dtMs = timestampMs - mLastTsMs;
    mLastTsMs = timestampMs;

    if (dtMs == 0) {
        // Two samples sharing an instant. Real: batches can carry samples
        // stamped identically. There is no time for them to integrate over, so
        // they update the filters' input memory and contribute no area.
        for (int a = 0; a < 3; ++a) {
            mAxis[a].hp1In = in[a];
        }
        return;
    }

    if (dtMs > kMaxGapMs) {
        // Delivery stopped and restarted. The wrist did not hold still for the
        // gap; we simply do not know what it did. Re-seed rather than integrate
        // a fabricated rectangle across it -- see kMaxGapMs.
        for (int a = 0; a < 3; ++a) {
            mAxis[a].prime(in[a]);
        }
        return;
    }

    const float dt = static_cast<float>(dtMs) * 0.001f;

    // Both coefficients are functions of this sample's own dt. Computed once
    // and shared across the three axes, which is the whole reason the axes are
    // stepped from here rather than each computing its own.
    const float aHp = tau(kHighPassHz) / (tau(kHighPassHz) + dt);
    const float bLp = dt / (tau(kLowPassHz) + dt);

    float sumSq = 0.0f;
    for (int a = 0; a < 3; ++a) {
        const float rectified = mAxis[a].step(in[a], dt, aHp, bLp);
        // dt-weighted, so the per-axis integral is an area in g*s and not a
        // function of how many samples happened to arrive -- the difference
        // between a measurement and an artefact of the delivery rate.
        mAxis[a].sumGs += rectified * dt;
        sumSq += rectified * rectified;
    }

    // Peak is a rate, not an area, so it is compared unweighted. It separates
    // one hard movement from continuous fidgeting, which integrate to the same
    // count and are not the same thing.
    const float instantaneous = std::sqrt(sumSq);
    if (instantaneous > mPeakG) {
        mPeakG = instantaneous;
    }

    mSamples++;
}

void EpochCounter::closeEpoch(uint32_t &count, uint32_t &peak, uint16_t &samples)
{
    // Vector magnitude of the three per-axis integrals, which is what
    // Actigraph's own "vector magnitude counts" are. Combining here rather than
    // per sample is what keeps each axis's own band-pass independent -- see the
    // header on why filtering |a| instead is wrong.
    const float sum = std::sqrt(mAxis[0].sumGs * mAxis[0].sumGs +
                                mAxis[1].sumGs * mAxis[1].sumGs +
                                mAxis[2].sumGs * mAxis[2].sumGs);

    const float scaledSum  = sum * kCountsPerGSecond;
    const float scaledPeak = mPeakG * kCountsPerGSecond;

    // Saturate rather than wrap. A count that overflowed to a small number is
    // indistinguishable from a still epoch, and a still epoch is the thing the
    // whole scorer is looking for.
    //
    // The test is `!(v < kMax)` rather than `v >= kMax` so that a value which is
    // neither -- a NaN that reached here despite the guard in add() -- saturates
    // instead of being cast. Casting it is undefined and in practice yields
    // zero, which is the one answer that must never come out of arithmetic that
    // failed: saturated is wrong and obvious, zero is wrong and invisible.
    constexpr float kMax = 4.0e9f;
    count = !(scaledSum  < kMax) ? 0xFFFFFFFFu : static_cast<uint32_t>(scaledSum);
    peak  = !(scaledPeak < kMax) ? 0xFFFFFFFFu : static_cast<uint32_t>(scaledPeak);
    samples = mSamples;

    for (int a = 0; a < 3; ++a) {
        mAxis[a].sumGs = 0.0f;
    }
    mPeakG   = 0.0f;
    mSamples = 0;
    // Filter state is deliberately kept -- see the class comment.
}

} // namespace Engine
