#ifndef HAPTICLAB_SWEEPSTATS_HPP
#define HAPTICLAB_SWEEPSTATS_HPP

#include <cstdint>

#include "Effects.hpp"

/// Phase 1's arithmetic: what the watch's own IMU can say about an effect it
/// cannot otherwise time.
///
/// The accelerometer hears the actuator through the case, so onset, offset and
/// a relative energy proxy come out of the sample-to-sample difference. It does
/// **not** give the waveform: an LRA carrier is well above the delivered rate
/// and aliases. Nothing here is a waveform measurement and nothing here should
/// be reported as one.
///
/// Integer throughout. `%f` is not linked in the watch's newlib in every build,
/// and when it is not it emits nothing at runtime rather than failing to link,
/// so no figure this app writes is fractional.
namespace Haptic {

struct ImuSample {
    /// The sensor's own clock, in microseconds.
    uint64_t timestampUs;
    int16_t  ax;
    int16_t  ay;
    int16_t  az;
    int16_t  gx;
    int16_t  gy;
    int16_t  gz;
};

/// Sample-to-sample motion, as the sum of the three axes' absolute changes.
/// L1 rather than the vector magnitude because there is no square root here
/// and the two are within a constant factor of each other for this purpose.
class DeltaStats
{
public:
    void reset()
    {
        mHavePrevious = false;
        mCount        = 0;
        mSum          = 0;
        mPeak         = 0;
    }

    /// Returns the delta this sample contributed, or 0 for the first one.
    uint32_t add(const ImuSample &sample)
    {
        uint32_t delta = 0;
        if (mHavePrevious) {
            delta = absDiff(sample.ax, mPrevX) + absDiff(sample.ay, mPrevY) +
                    absDiff(sample.az, mPrevZ);
            mSum += delta;
            if (delta > mPeak) {
                mPeak = delta;
            }
            mCount++;
        }
        mPrevX        = sample.ax;
        mPrevY        = sample.ay;
        mPrevZ        = sample.az;
        mHavePrevious = true;
        return delta;
    }

    uint16_t count() const { return mCount; }
    uint32_t peak() const { return mPeak; }

    uint32_t mean() const
    {
        return mCount == 0 ? 0 : static_cast<uint32_t>(mSum / mCount);
    }

private:
    static uint32_t absDiff(int16_t a, int16_t b)
    {
        const int32_t d = static_cast<int32_t>(a) - static_cast<int32_t>(b);
        return static_cast<uint32_t>(d < 0 ? -d : d);
    }

    bool     mHavePrevious = false;
    uint16_t mCount        = 0;
    uint64_t mSum          = 0;
    uint32_t mPeak         = 0;
    int16_t  mPrevX        = 0;
    int16_t  mPrevY        = 0;
    int16_t  mPrevZ        = 0;
};

/// How far above the quiet baseline a sample has to be before it counts as the
/// actuator rather than the wrist, as a fraction so the arithmetic stays
/// integer: four times the baseline.
constexpr uint32_t kOnsetFactorNumerator   = 4;
constexpr uint32_t kOnsetFactorDenominator = 1;

/// A floor under the threshold, so an implausibly quiet baseline cannot make
/// every sample look like an onset. In raw LSB of the accelerometer, and the
/// figure that would change it is a measured noise floor from a real sweep.
constexpr uint32_t kOnsetFloorLsb = 24;

/// Consecutive samples over the threshold before an onset is believed. Three
/// rather than two because a *single* stray sample produces two large deltas --
/// one going into it and one coming out, so two would accept a knock.
/// `ASingleStraySampleIsNotAnOnset` is what holds it.
constexpr uint8_t kOnsetRunLength = 3;

/// One effect, one repeat: the capture window and what it yields.
class SweepWindow
{
public:
    /// `baselineMeanDelta` comes from the settle window before the play, so
    /// the threshold is this wrist at this moment rather than a constant.
    void begin(uint64_t playTimestampUs, uint32_t baselineMeanDelta)
    {
        mPlayUs    = playTimestampUs;
        mThreshold = baselineMeanDelta * kOnsetFactorNumerator / kOnsetFactorDenominator;
        if (mThreshold < kOnsetFloorLsb) {
            mThreshold = kOnsetFloorLsb;
        }
        mStats.reset();
        mRun      = 0;
        mOnsetUs  = kNoEdge;
        mOffsetUs = kNoEdge;
    }

    void add(const ImuSample &sample)
    {
        const uint32_t delta = mStats.add(sample);
        if (mStats.count() == 0) {
            return;
        }

        if (delta > mThreshold) {
            mRun++;
            if (mRun >= kOnsetRunLength && mOnsetUs == kNoEdge) {
                mOnsetUs = static_cast<int64_t>(sample.timestampUs) - static_cast<int64_t>(mPlayUs);
            }
            if (mOnsetUs != kNoEdge) {
                mOffsetUs =
                    static_cast<int64_t>(sample.timestampUs) - static_cast<int64_t>(mPlayUs);
            }
        } else {
            mRun = 0;
        }
    }

    /// Sentinel for an edge that never happened, which is a real result: an
    /// effect index that plays nothing has no onset.
    static constexpr int64_t kNoEdge = -1;

    uint16_t samples() const { return mStats.count(); }
    uint32_t meanDelta() const { return mStats.mean(); }
    uint32_t peakDelta() const { return mStats.peak(); }
    uint32_t threshold() const { return mThreshold; }
    int64_t  onsetUs() const { return mOnsetUs; }
    int64_t  offsetUs() const { return mOffsetUs; }

    /// Offset minus onset, or -1 when there was no onset. This is the number
    /// that turns "effect 3" into a physical quantity.
    int64_t durationUs() const
    {
        return mOnsetUs == kNoEdge || mOffsetUs == kNoEdge ? kNoEdge : mOffsetUs - mOnsetUs;
    }

private:
    uint64_t   mPlayUs    = 0;
    uint32_t   mThreshold = kOnsetFloorLsb;
    uint8_t    mRun       = 0;
    int64_t    mOnsetUs   = kNoEdge;
    int64_t    mOffsetUs  = kNoEdge;
    DeltaStats mStats;
};

/// The duration an effect's own name claims, or 0 for the ones that claim
/// nothing. Two of the twenty-one name a duration, and they are the only
/// independent check on a measured one.
inline uint16_t declaredDurationMs(uint8_t effect)
{
    for (size_t i = 0; i < sizeof(kSelfTimingEffects) / sizeof(kSelfTimingEffects[0]); i++) {
        if (kSelfTimingEffects[i] == effect) {
            return kSelfTimedDurations[i];
        }
    }
    return 0;
}

} // namespace Haptic

#endif // HAPTICLAB_SWEEPSTATS_HPP
