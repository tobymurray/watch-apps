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
/// Every sample is filed by **its own timestamp** against the play, and the
/// threshold is computed when the window closes rather than when it opens.
/// That is not tidiness. Samples reach the app several hundred milliseconds
/// after they were taken, so the phase that is current when a batch is
/// *delivered* is not the phase the samples in it were taken in. The second
/// sweep labelled the first four repeats of effect 1 entirely `capture` when
/// three of them had not been played yet, and its quiet stretch never reached
/// the baseline at all: zero in 277 of 762 rows, with 298 rows reporting an
/// onset before the play. Docs/LEDGER.md row S11 has the mechanism and the
/// evidence that the sensor's clock and `ISystem::getTimeMs` share a base,
/// which is what makes filing by timestamp possible.
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
/// every sample an onset. In raw LSB of the accelerometer. Reading the second
/// sweep's raw samples back put an off-wrist quiet level at 18 LSB, giving a
/// threshold of 72 that separated every one of effect 1's bursts cleanly, so
/// this floor sits below a real quiet level and only ever catches a stiller
/// wrist than that one.
constexpr uint32_t kOnsetFloorLsb = 24;

/// Consecutive samples over the threshold before an onset is believed. Three
/// rather than two because a *single* stray sample produces two large deltas,
/// one going into it and one coming out, so two would accept a knock.
/// `ASingleStraySampleIsNotAnOnset` is what holds it.
constexpr uint8_t kOnsetRunLength = 3;

/// How far the burst has to fall below its own peak before it has ended, as a
/// percentage. This is what stops the ring-down being counted as drive: see
/// `finish()` for the two effects that settle it and why the figure is not
/// fitted to them.
constexpr uint32_t kOffsetPeakPercent = 10;

/// Quiet time that ends the burst. Without it the offset is the last sample
/// above threshold anywhere in the window, so a wrist that moves a second
/// after the effect finished extends the effect: the first sweep read
/// `STRONG_CLICK_100` as 2717 ms in one repeat and 80 ms in the other. At the
/// delivered rate this is about 25 samples of quiet.
constexpr uint32_t kBurstGapMs = 250;

/// One sample's contribution, kept until the window closes because the
/// threshold cannot be known until the quiet part of the window is complete.
struct WindowEntry {
    /// Microseconds from the window's origin, which is when the quiet stretch
    /// started rather than when the effect was played.
    int32_t  atUs;
    uint16_t delta;
};

/// A settle stretch plus a capture is 3.1 s at the delivered rate, so about
/// 310 entries; this leaves room for the batching to overshoot. An overflow is
/// reported rather than the tail being dropped silently.
constexpr uint16_t kMaxWindowEntries = 512;

/// One effect, one repeat: everything the window collected, and what it means
/// once the play instant is known and the window has closed.
class SweepWindow
{
public:
    /// Called when the quiet stretch starts. `originUs` only has to precede
    /// every sample the window will see.
    void begin(uint64_t originUs)
    {
        mOriginUs      = originUs;
        mPlayAtUs      = kNoPlay;
        mNewestUs      = 0;
        mHaveNewest    = false;
        mCount         = 0;
        mOverflowed    = false;
        mBaselineDelta = 0;
        mHadBaseline   = false;
        mThreshold     = kOnsetFloorLsb;
        mOnsetUs       = kNoEdge;
        mOffsetUs      = kNoEdge;
        mBurstMean     = 0;
        mBurstPeak     = 0;
        mBurstSamples  = 0;
        mDeltas.reset();
    }

    /// Called when the effect is asked for. Everything timestamped before this
    /// instant is the baseline and everything after it is the candidate burst,
    /// whichever batch each sample happened to arrive in.
    void notePlay(uint64_t playUs)
    {
        mPlayAtUs = static_cast<int32_t>(static_cast<int64_t>(playUs) -
                                        static_cast<int64_t>(mOriginUs));
    }

    void add(const ImuSample &sample)
    {
        const int64_t atUs = static_cast<int64_t>(sample.timestampUs) -
                             static_cast<int64_t>(mOriginUs);
        // The tail of the previous repeat, delivered late. Taking it would put
        // the previous effect's ring-down into this repeat's baseline, which is
        // the same contamination in a new disguise. Dropping it costs nothing:
        // it is quiet time after a burst that has already been measured.
        if (atUs < 0) {
            return;
        }
        mNewestUs   = static_cast<int32_t>(atUs);
        mHaveNewest = true;

        const uint32_t delta = mDeltas.add(sample);
        if (mDeltas.count() == 0) {
            return; // the first sample has nothing to difference against
        }
        if (mCount >= kMaxWindowEntries) {
            mOverflowed = true;
            return;
        }
        mEntries[mCount].atUs  = static_cast<int32_t>(atUs);
        mEntries[mCount].delta = delta > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(delta);
        mCount++;
    }

    /// Splits the window at the play, takes the baseline from what came before
    /// and the burst from what came after, in three passes: the quiet stretch
    /// sets the threshold, the threshold finds the burst, and the burst's own
    /// peak decides where it ends.
    void finish()
    {
        if (mPlayAtUs == kNoPlay) {
            return;
        }

        uint64_t quietSum   = 0;
        uint16_t quietCount = 0;
        for (uint16_t i = 0; i < mCount; i++) {
            if (mEntries[i].atUs < mPlayAtUs) {
                quietSum += mEntries[i].delta;
                quietCount++;
            }
        }
        mHadBaseline   = quietCount > 0;
        mBaselineDelta = mHadBaseline ? static_cast<uint32_t>(quietSum / quietCount) : 0;

        mThreshold = mBaselineDelta * kOnsetFactorNumerator / kOnsetFactorDenominator;
        if (mThreshold < kOnsetFloorLsb) {
            mThreshold = kOnsetFloorLsb;
        }

        // The first run of consecutive loud samples after the play, ended by a
        // gap rather than by the window closing.
        uint16_t first   = 0;
        uint16_t last    = 0;
        uint16_t runFrom = 0;
        uint8_t  run     = 0;
        bool     started = false;
        for (uint16_t i = 0; i < mCount; i++) {
            if (mEntries[i].atUs < mPlayAtUs) {
                continue;
            }
            if (mEntries[i].delta > mThreshold) {
                if (run == 0) {
                    runFrom = i;
                }
                run++;
                if (!started && run >= kOnsetRunLength) {
                    started = true;
                    first   = runFrom;
                }
                if (started) {
                    last = i;
                }
            } else {
                run = 0;
                if (started && static_cast<int64_t>(mEntries[i].atUs) - mEntries[last].atUs >=
                                   static_cast<int64_t>(kBurstGapMs) * 1000) {
                    break;
                }
            }
        }
        if (!started) {
            return;
        }

        for (uint16_t i = first; i <= last; i++) {
            if (mEntries[i].delta > mBurstPeak) {
                mBurstPeak = mEntries[i].delta;
            }
        }

        // Where the burst ends, measured against its own peak rather than
        // against the wrist's quiet level. The two effects that name a duration
        // are the only ground truth this app has, and against a baseline
        // multiple both read long: 793 ms against a declared 750 and 1040
        // against a declared 1000. Trimming the tail to a tenth of the peak
        // puts them at 751 and 996, and the answer moves by under 1 % anywhere
        // between a twentieth and a fifth, so this is a criterion rather than a
        // constant fitted to two points. What it removes is the ring-down: the
        // actuator is still moving after it has stopped being driven, and a
        // threshold set by the wrist cannot tell the difference.
        const uint32_t tail = mBurstPeak * kOffsetPeakPercent / 100;
        while (last > first && mEntries[last].delta < tail) {
            last--;
        }

        uint64_t burstSum = 0;
        for (uint16_t i = first; i <= last; i++) {
            burstSum += mEntries[i].delta;
            mBurstSamples++;
        }
        mOnsetUs  = static_cast<int64_t>(mEntries[first].atUs) - mPlayAtUs;
        mOffsetUs = static_cast<int64_t>(mEntries[last].atUs) - mPlayAtUs;
        // Over the burst rather than over the whole window: a mean diluted by
        // three seconds of quiet is not an energy, which is why the first
        // sweep's table put effect 1 at 101 against a peak of 4599.
        mBurstMean = mBurstSamples > 0 ? static_cast<uint32_t>(burstSum / mBurstSamples) : 0;
    }

    /// Whether a sample timestamped this far past the play has arrived. This
    /// is the only proof that the effect is inside the window: samples reach
    /// the app several hundred milliseconds after they were taken, so a window
    /// closed on a wall clock is closed on samples still in flight, and the
    /// burst can be among them.
    bool coversPlayPlus(int64_t sinceUs) const
    {
        return mPlayAtUs != kNoPlay && mHaveNewest &&
               static_cast<int64_t>(mNewestUs) - mPlayAtUs >= sinceUs;
    }

    /// Sentinel for an edge that never happened, which is a real result: an
    /// effect index that plays nothing has no onset.
    static constexpr int64_t kNoEdge = -1;

    bool overflowed() const { return mOverflowed; }
    /// False when no sample landed before the play, so the threshold fell back
    /// to its floor and the row should be doubted.
    bool     hadBaseline() const { return mHadBaseline; }
    uint32_t baselineDelta() const { return mBaselineDelta; }
    uint32_t threshold() const { return mThreshold; }
    uint16_t windowSamples() const { return mCount; }
    uint16_t samples() const { return mBurstSamples; }
    uint32_t meanDelta() const { return mBurstMean; }
    uint32_t peakDelta() const { return mBurstPeak; }
    int64_t  onsetUs() const { return mOnsetUs; }
    int64_t  offsetUs() const { return mOffsetUs; }

    /// Offset minus onset, or -1 when there was no onset. This is the number
    /// that turns "effect 3" into a physical quantity.
    int64_t durationUs() const
    {
        return mOnsetUs == kNoEdge || mOffsetUs == kNoEdge ? kNoEdge : mOffsetUs - mOnsetUs;
    }

private:
    static constexpr int32_t kNoPlay = INT32_MIN;

    uint64_t    mOriginUs = 0;
    int32_t     mPlayAtUs = kNoPlay;
    WindowEntry mEntries[kMaxWindowEntries] {};
    uint16_t    mCount      = 0;
    bool        mOverflowed = false;
    int32_t     mNewestUs   = 0;
    bool        mHaveNewest = false;
    DeltaStats  mDeltas;

    uint32_t mBaselineDelta = 0;
    bool     mHadBaseline   = false;
    uint32_t mThreshold     = kOnsetFloorLsb;
    int64_t  mOnsetUs       = kNoEdge;
    int64_t  mOffsetUs      = kNoEdge;
    uint32_t mBurstMean     = 0;
    uint32_t mBurstPeak     = 0;
    uint16_t mBurstSamples  = 0;
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
