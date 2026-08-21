/**
 ******************************************************************************
 * @file    Histogram.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   A fixed-bin histogram coarse enough to store, and its quantiles.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * Why a histogram rather than the samples
 *
 * A p95 needs an order statistic, and an order statistic needs either every
 * sample or a summary. This app's sample path is the hottest in the repository
 * -- 308 accelerometer batches a minute at 9.6 samples each (ledger row S17)
 * for one sensor, with a dozen possibly subscribed -- so keeping the samples is
 * not available: ten thousand floats per field per sensor is megabytes, and the
 * service has 500 KB.
 *
 * So: linear bins of a stated width, plus exact min and max outside them. The
 * bin width is recorded in the profile with every quantile derived from it,
 * because a p95 read off 1 ms bins and a p95 read off 10 ms bins are different
 * numbers and a reader has to be able to tell which they are looking at.
 *
 * The quantiles are therefore **bin-resolution estimates, not exact order
 * statistics**, and `quantile()` says so by construction: it returns the bin's
 * midpoint. The error is bounded by half a bin width and that is the honest
 * thing to publish. Exact min and max are kept separately because they are
 * cheap and because a single 4-second gap in an otherwise 20 ms stream is the
 * finding, and a histogram whose top bin is "480 ms or more" would lose it.
 *
 * ---------------------------------------------------------------------------
 * Per-sample cost
 *
 * One multiply by a precomputed reciprocal, one truncation, two compares and an
 * increment. No division: the reciprocal is computed once in `reset()`.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_HISTOGRAM_HPP
#define SENSORLAB_HISTOGRAM_HPP

#include <cstddef>
#include <cstdint>

namespace SensorLab::Stats
{

/**
 * @brief Linear-bin histogram with exact extrema and overflow accounting.
 *
 * @tparam kBins Number of bins. 64 at 1 ms covers 0-64 ms, which spans every
 *         delivered rate this device has been observed at (row S3's ~48 Hz is
 *         21 ms) with room for the gaps that matter; the overflow counter and
 *         the exact max carry everything above it.
 */
template <size_t kBins>
class Histogram
{
public:
    static_assert(kBins >= 2, "a one-bin histogram is a counter");

    Histogram() { reset(1.0f, 0.0f); }

    /// @param binWidth  Width of each bin, in the sample's own units. Recorded
    ///                  in the profile next to every quantile derived from it.
    /// @param origin    Value the first bin starts at. Non-zero for a quantity
    ///                  whose interesting range does not begin at zero.
    void reset(float binWidth, float origin = 0.0f)
    {
        mBinWidth = (binWidth > 0.0f) ? binWidth : 1.0f;
        mInvWidth = 1.0f / mBinWidth;
        mOrigin   = origin;
        mCount    = 0;
        mUnder    = 0;
        mOver     = 0;
        mMin      = 0.0f;
        mMax      = 0.0f;
        mSum      = 0.0;
        for (size_t i = 0; i < kBins; i++) {
            mBin[i] = 0;
        }
    }

    /// One sample. Non-finite values are the caller's problem: `FieldStats`
    /// counts and drops them before they get here, because a NaN would land in
    /// no bin and poison the sum.
    void add(float v)
    {
        if (mCount == 0) {
            mMin = v;
            mMax = v;
        } else if (v < mMin) {
            mMin = v;
        } else if (v > mMax) {
            mMax = v;
        }
        mCount++;
        mSum += static_cast<double>(v);

        const float scaled = (v - mOrigin) * mInvWidth;
        if (scaled < 0.0f) {
            mUnder++;
            return;
        }
        const size_t idx = static_cast<size_t>(scaled);
        if (idx >= kBins) {
            mOver++;
            return;
        }
        mBin[idx]++;
    }

    uint32_t count()    const { return mCount; }
    float    binWidth() const { return mBinWidth; }
    float    origin()   const { return mOrigin; }
    /// Exact, not a bin midpoint.
    float    min()      const { return mMin; }
    float    max()      const { return mMax; }
    uint32_t underflow() const { return mUnder; }
    uint32_t overflow()  const { return mOver; }

    float mean() const
    {
        return (mCount == 0) ? 0.0f
                             : static_cast<float>(mSum / static_cast<double>(mCount));
    }

    /**
     * @brief Bin-midpoint estimate of the @p q quantile, 0.0 to 1.0.
     *
     * Samples that fell below the first bin or above the last are counted in
     * position -- so a distribution whose mass is mostly in the overflow still
     * reports quantiles in the right *order* -- but a quantile that lands in
     * the overflow returns the exact max rather than an invented bin centre,
     * and one that lands in the underflow returns the exact min. Both are
     * honest: the histogram genuinely does not know where in the tail the
     * sample was.
     */
    float quantile(float q) const
    {
        if (mCount == 0) {
            return 0.0f;
        }
        if (q <= 0.0f) {
            return mMin;
        }
        if (q >= 1.0f) {
            return mMax;
        }

        // Rank of the wanted sample, one-based, rounded up: the conventional
        // nearest-rank definition, so p50 of an even count is the upper of the
        // two middles rather than an interpolation between bins that do not
        // touch.
        const uint32_t target = static_cast<uint32_t>(
            static_cast<double>(q) * static_cast<double>(mCount) + 0.999999);

        uint32_t seen = mUnder;
        if (target <= seen) {
            return mMin;
        }
        for (size_t i = 0; i < kBins; i++) {
            seen += mBin[i];
            if (target <= seen) {
                return mOrigin + (static_cast<float>(i) + 0.5f) * mBinWidth;
            }
        }
        return mMax;
    }

    /// Raw bin counts, for the profile. Written as an array so a host tool can
    /// re-derive any quantile the app did not report -- an analysis without its
    /// inputs cannot be corrected.
    uint32_t bin(size_t i) const { return (i < kBins) ? mBin[i] : 0; }
    static constexpr size_t bins() { return kBins; }

private:
    uint32_t mBin[kBins] {};
    uint32_t mCount    = 0;
    uint32_t mUnder    = 0;
    uint32_t mOver     = 0;
    float    mBinWidth = 1.0f;
    float    mInvWidth = 1.0f;
    float    mOrigin   = 0.0f;
    float    mMin      = 0.0f;
    float    mMax      = 0.0f;
    /// Double, because a float sum over ten thousand samples of similar
    /// magnitude loses the low bits and the mean drifts. It costs eight bytes
    /// once per histogram, not per sample.
    double   mSum      = 0.0;
};

/// The dt histogram's shape. 1 ms bins over 0-128 ms: fine enough to separate
/// a 20 ms stream from a 21 ms one, wide enough to hold everything the device
/// has been seen to deliver, and the overflow plus the exact max carry the
/// gaps. 128 bins is 512 bytes per stream.
using DtHistogram = Histogram<128>;

/// Samples per batch. 1-wide bins over 0-64: measured 9.6 on hardware (row
/// S17) against a requested 5 s latency that would have implied ~247.
using BatchHistogram = Histogram<64>;

} // namespace SensorLab::Stats

#endif // SENSORLAB_HISTOGRAM_HPP
