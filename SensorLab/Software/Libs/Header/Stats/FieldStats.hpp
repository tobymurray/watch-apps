/**
 ******************************************************************************
 * @file    FieldStats.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Layer 5, per field: domain, resolution, stuck, non-finite, distinct.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * The four things this exists to catch
 *
 * **A non-finite sample.** One of them once poisoned every subsequent SleepLab
 * epoch to exactly zero. They are counted and dropped here, never integrated,
 * and the count is a claim of its own -- because a sensor producing one NaN an
 * hour is a different finding from one producing none.
 *
 * **A stuck value.** `BATTERY_LEVEL` read 100.0 % at both ends of an 8.45 h
 * night in which the fuel gauge lost 10 mAh (ledger row S18). Not absent
 * enough to notice, not working enough to use -- the hardest failure mode to
 * see and the most important to publish. `stuckMaxRun()` and `everChanged()`
 * are that row, mechanised.
 *
 * **The effective resolution.** The smallest non-zero absolute difference
 * between consecutive *distinct* values. For a float fed from an integer ADC
 * this recovers the LSB, and the LSB recovers the configured full-scale range
 * -- which is how you learn whether the accelerometer is in +-2 g or +-16 g
 * without reading a register.
 *
 *   It is a **lower bound on the quantisation step, not the step**, and it is
 *   meaningless if the value did not vary. Both caveats are enforced rather
 *   than documented: `lsb()` reports nothing until at least two distinct values
 *   have been seen, and the claim carries `NeverVaried` when they have not.
 *
 *   The bound is one-sided in a knowable direction. Consecutive samples of a
 *   quantised signal differ by an integer multiple of the true step, so every
 *   observed difference is >= the step and the minimum of them converges *down*
 *   to it. It can never come out smaller than the truth, which is why "lower
 *   bound" is the honest label and why more samples only ever improve it.
 *
 * **An enum outside its documented set.** Tracked exactly up to
 * `kDistinctMax` distinct values, then counted. Sixteen is chosen because the
 * widest documented enum on this platform has four members
 * (`ACTIVITY_RECOGNITION`) and a field that has produced more than sixteen
 * distinct values is not an enum, which is itself the answer.
 *
 * ---------------------------------------------------------------------------
 * Per-sample cost
 *
 * A finite check, two compares against the extrema, a float add to a double
 * sum, a bitwise compare against the previous value, one subtract and compare
 * for the LSB, and a linear scan of at most sixteen `uint32_t` for the distinct
 * set -- which is skipped entirely unless the field is declared enum-like. No
 * division, no allocation, no branch on sensor type.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_FIELDSTATS_HPP
#define SENSORLAB_FIELDSTATS_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SensorLab::Stats
{

/// Distinct values tracked exactly for an enum-like field. See the header note.
constexpr size_t kDistinctMax = 16;

/**
 * @brief Everything layer 5 claims about one field of one sensor.
 *
 * Fed the raw 32-bit word rather than a float, because that is what the frame
 * carries and because the stuck-value test has to be on the *bits*: two
 * distinct NaN payloads are not the same value, and 0.0f and -0.0f compare
 * equal as floats while being a real change in the wire data.
 */
class FieldStats
{
public:
    FieldStats() { reset(false); }

    /// @param enumLike  Track the distinct-value set. Set for fields the
    ///                  generated table reads through `.u` or `.i`, which is
    ///                  every enum, every boolean-in-a-u32 and every counter
    ///                  on the platform. Cheap, but not free, so it is off for
    ///                  a float axis where sixteen distinct values is the first
    ///                  sixteen samples.
    void reset(bool enumLike)
    {
        mEnumLike     = enumLike;
        mCount        = 0;
        mNonFinite    = 0;
        mDenormal     = 0;
        mMin          = 0.0f;
        mMax          = 0.0f;
        mSum          = 0.0;
        mHasPrevBits  = false;
        mPrevBits     = 0;
        mRun          = 0;
        mMaxRun       = 0;
        mChanged      = false;
        mHasPrevValue = false;
        mPrevValue    = 0.0f;
        mHasLsb       = false;
        mLsb          = 0.0f;
        mDistinct     = 0;
        mDistinctOver = false;
    }

    /// One sample's raw word, interpreted as the field's declared kind.
    ///
    /// @param bits    The 32-bit union word, verbatim.
    /// @param asFloat The same word read the way the shipped parser reads it.
    ///                Passing both avoids a type-punning branch on the sample
    ///                path and keeps the bit-exact stuck test independent of
    ///                how the value is interpreted.
    void add(uint32_t bits, float asFloat)
    {
        // -- Stuck run, on the bits ------------------------------------------
        // Before the finite check, deliberately: a field stuck at one
        // particular NaN is stuck, and dropping the sample here would hide it.
        if (mHasPrevBits && bits == mPrevBits) {
            mRun++;
        } else {
            if (mHasPrevBits) {
                mChanged = true;
            }
            mRun = 1;
        }
        if (mRun > mMaxRun) {
            mMaxRun = mRun;
        }
        mHasPrevBits = true;
        mPrevBits    = bits;

        // -- Distinct set, for enums and booleans ----------------------------
        if (mEnumLike) {
            noteDistinct(bits);
        }

        // -- Non-finite accounting -------------------------------------------
        // Done by exponent rather than by <cmath>, so this header pulls in
        // nothing and behaves identically on the host, in the simulator and on
        // newlib. IEEE-754 binary32: exponent all ones is Inf or NaN, exponent
        // zero with a non-zero mantissa is a denormal.
        const uint32_t exponent = (bits >> 23) & 0xFFu;
        const uint32_t mantissa = bits & 0x7FFFFFu;
        if (exponent == 0xFFu) {
            mNonFinite++;
            return;   // never integrated: one of these poisoned a whole night
        }
        if (exponent == 0u && mantissa != 0u) {
            mDenormal++;
            // Counted but kept: a denormal is a real, if tiny, value, and
            // dropping it would bias a mean towards zero from the other side.
        }

        // -- Domain -----------------------------------------------------------
        if (mCount == 0) {
            mMin = asFloat;
            mMax = asFloat;
        } else if (asFloat < mMin) {
            mMin = asFloat;
        } else if (asFloat > mMax) {
            mMax = asFloat;
        }
        mCount++;
        mSum += static_cast<double>(asFloat);

        // -- Effective resolution ---------------------------------------------
        // Smallest non-zero |difference| between consecutive distinct values.
        // Consecutive samples of a quantised signal differ by an integer
        // multiple of the true step, so this converges downwards to it and can
        // never read smaller -- a lower bound, in a known direction.
        if (mHasPrevValue) {
            float d = asFloat - mPrevValue;
            if (d < 0.0f) {
                d = -d;
            }
            if (d > 0.0f && (!mHasLsb || d < mLsb)) {
                mHasLsb = true;
                mLsb    = d;
            }
        }
        mHasPrevValue = true;
        mPrevValue    = asFloat;
    }

    /// Finite samples integrated. Non-finite ones are excluded, and counted
    /// separately, so a mean is never a mean of poison.
    uint32_t count()     const { return mCount; }
    uint32_t nonFinite() const { return mNonFinite; }
    uint32_t denormal()  const { return mDenormal; }

    float min() const { return mMin; }
    float max() const { return mMax; }
    float mean() const
    {
        return (mCount == 0) ? 0.0f
                             : static_cast<float>(mSum / static_cast<double>(mCount));
    }

    /// Longest run of bit-identical values, in samples. Includes the current
    /// run, so a field that never changed reports every sample it saw.
    uint32_t stuckMaxRun() const { return mMaxRun; }
    /// Whether the field's bits ever differed from the previous sample's.
    bool     everChanged() const { return mChanged; }

    /// Lower bound on the quantisation step, or false when the value has not
    /// varied enough for the bound to mean anything.
    bool  hasLsb() const { return mHasLsb; }
    float lsb()    const { return mLsb; }

    /// Distinct raw words seen, exactly, while the tracked set has not
    /// overflowed. `distinctOverflowed()` says the answer is "more than
    /// `kDistinctMax`", which for a field documented as an enum is the finding.
    uint8_t distinctCount()      const { return mDistinct; }
    bool    distinctOverflowed() const { return mDistinctOver; }
    /// The @p i th distinct word seen, in first-seen order. Written into the
    /// profile for enum fields so a value outside the documented set can be
    /// named rather than merely counted.
    uint32_t distinctValue(size_t i) const
    {
        return (i < mDistinct) ? mDistinctSet[i] : 0u;
    }

private:
    void noteDistinct(uint32_t bits)
    {
        for (size_t i = 0; i < mDistinct; i++) {
            if (mDistinctSet[i] == bits) {
                return;
            }
        }
        if (mDistinct < kDistinctMax) {
            mDistinctSet[mDistinct++] = bits;
        } else {
            mDistinctOver = true;
        }
    }

    uint32_t mDistinctSet[kDistinctMax] {};
    double   mSum          = 0.0;
    uint32_t mCount        = 0;
    uint32_t mNonFinite    = 0;
    uint32_t mDenormal     = 0;
    uint32_t mPrevBits     = 0;
    uint32_t mRun          = 0;
    uint32_t mMaxRun       = 0;
    float    mMin          = 0.0f;
    float    mMax          = 0.0f;
    float    mPrevValue    = 0.0f;
    float    mLsb          = 0.0f;
    uint8_t  mDistinct     = 0;
    bool     mDistinctOver = false;
    bool     mEnumLike     = false;
    bool     mHasPrevBits  = false;
    bool     mHasPrevValue = false;
    bool     mHasLsb       = false;
    bool     mChanged      = false;
};

} // namespace SensorLab::Stats

#endif // SENSORLAB_FIELDSTATS_HPP
