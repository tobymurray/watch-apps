/**
 ******************************************************************************
 * @file    EpochCounter.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Accelerometer samples in, activity counts out.
 ******************************************************************************
 *
 * Pure C++17. No SDK header, no allocation, no I/O.
 *
 * ---------------------------------------------------------------------------
 * What a "count" is, and why this one is derived the way it is
 *
 * Published wrist actigraphy scorers -- Cole-Kripke, Sadeh, Oakley -- all
 * consume *activity counts*: a per-epoch scalar produced by band-limiting the
 * accelerometer signal to the frequency range of human movement, rectifying
 * it, and integrating over the epoch. The band matters as much as the
 * integration: below it is postural drift and the gravity vector, above it is
 * impact shock and sensor noise, and neither is the thing being measured.
 *
 * This implements that pipeline, **per axis**:
 *
 *   1. high-pass 0.25 Hz  on each of x, y, z independently. This is the
 *                         "deviation from the gravity vector" step: gravity is
 *                         a constant on whichever axes the wrist happens to
 *                         present to it, and a constant is not movement.
 *   2. low-pass  3.00 Hz  removes impact transients and sensor noise.
 *   3. rectify            |x|, because direction carries no information here.
 *   4. integrate          per axis, over the epoch, weighted by each sample's
 *                         own time step (see below).
 *   5. combine            the three per-axis integrals as their vector
 *                         magnitude.
 *
 * Both filters are **two-pole** -- a cascade of two identical one-poles, so
 * 12 dB/octave rather than 6. One pole was measured to leave 0.05 Hz postural
 * drift at about half the passband response, which is not a band-pass so much
 * as a gentle preference. The second pole costs one float of state and one
 * multiply-add per axis per sample.
 *
 * ---------------------------------------------------------------------------
 * Per axis, not on the vector magnitude
 *
 * Filtering |a| instead is tempting -- it is orientation-invariant in one line
 * -- and it is wrong here in two ways that were measured rather than guessed:
 *
 *   - **It is blind to rotation.** A wrist that rolls from palm-down to
 *     palm-sideways changes which axis carries gravity without changing |a| at
 *     all, so a roll-over registers as nothing. Per axis, gravity moving from
 *     z to x is a large step on both, which is what it physically is.
 *   - **It is quadratically insensitive to movement across gravity.** For a
 *     transverse acceleration A on top of 1 g, |a| = sqrt(1 + A^2) ~ 1 + A^2/2:
 *     a second-order term. Measured, a 0.3 g transverse movement produced about
 *     one fifteenth the count of the same movement along the gravity axis. The
 *     count would then depend mostly on which way up the watch was.
 *
 * Combining the three axes as a vector magnitude *of the integrals* -- rather
 * than integrating the magnitude -- is also what Actigraph's own
 * "vector magnitude counts" do, so this stays close to the published practice
 * the scorer's coefficients came from.
 *
 * ---------------------------------------------------------------------------
 * The delivered rate is not the requested rate, and that changes the design
 *
 * A filter designed for 25 Hz is the wrong filter at 12.5 Hz, and this
 * platform does not guarantee either. The per-listener sample-rate gate thins
 * delivery on a boundary at *half* the expected period, an exact ratio falls on
 * the thinner side, and the thinning is quantised into bands rather than being
 * proportional to rate. So the delivered rate can differ from the requested one
 * by a factor of two, and can differ again between one night and the next.
 *
 * Two consequences, and both are load-bearing:
 *
 *   - **The filters are re-coefficiented per sample from the actual time step**,
 *     taken from the sensor's own timestamps rather than from a clock read in
 *     the message handler. A one-pole filter's alpha is a function of dt, so
 *     this costs two divides a sample and makes the pipeline correct under a
 *     rate that changes underneath it.
 *   - **The integral is dt-weighted**, so a count has units of g*s and is
 *     independent of how many samples happened to arrive. Summing |x| without
 *     the weight would make a count proportional to the delivered rate, and
 *     halving the rate would halve every count in the night -- which the scorer
 *     would faithfully read as a quieter night.
 *
 * Neither is optional. `Never infer elapsed time from a sample count` is the
 * platform rule; this is what obeying it looks like in the sample path.
 *
 * ---------------------------------------------------------------------------
 * The count scale, and the honest thing to say about it
 *
 * kCountsPerGSecond turns g*s into an integer. Its value is arbitrary. What is
 * *not* arbitrary is that a published scorer's coefficients were fitted against
 * a specific device's count units -- Cole-Kripke against an Actillume's -- and
 * those units are not these. Reusing the coefficients without a scale factor
 * relating the two is the single easiest way to build something that looks
 * validated and is not.
 *
 * So the scale factor lives in SleepWakeScorer as one named, documented
 * constant with a TODO naming the recording that would justify it, rather than
 * being buried here. See SleepWakeScorer::kCountScale.
 *
 ******************************************************************************
 */

#ifndef ENGINE_EPOCHCOUNTER_HPP
#define ENGINE_EPOCHCOUNTER_HPP

#include <cstdint>

namespace Engine
{

/**
 * @brief Streaming activity-count accumulator for one epoch at a time.
 *
 * Fed one accelerometer sample at a time as they arrive; asked for a count
 * when the epoch closes. Holds four floats of filter state and nothing else --
 * no buffer of samples, because an epoch at 25 Hz is 750 samples and there is
 * no reason to keep them.
 *
 * The filter state deliberately survives `closeEpoch()`. A high-pass filter
 * reset at every epoch boundary would emit a settling transient into the first
 * second of every epoch, and 960 transients a night is a signal.
 */
class EpochCounter
{
public:
    // -- The band ------------------------------------------------------------
    //
    // 0.25-3 Hz is the band the published count-generation methods work in. The
    // lower edge is above the frequency of postural change and below the
    // slowest deliberate limb movement; the upper edge is above voluntary wrist
    // movement and below impact shock. Neither number is tunable per user and
    // neither is a threshold -- they are properties of human movement, which is
    // why they are the one set of constants here that needs no local recording
    // to justify.

    /// High-pass corner, Hz. Removes gravity and postural drift. Two-pole.
    static constexpr float kHighPassHz = 0.25f;

    /// Low-pass corner, Hz. Removes impact transients and sensor noise.
    /// Two-pole.
    static constexpr float kLowPassHz  = 3.00f;

    /// Counts per g*s of integrated band-limited acceleration.
    ///
    /// Arbitrary, but fixed: it sets the scale every threshold downstream is
    /// expressed in, so changing it invalidates every recorded night and every
    /// constant calibrated against them.
    ///
    /// Measured against this pipeline: a continuous 0.3 g movement at 1 Hz --
    /// vigorous, well beyond anything a sleeper does -- integrates to about
    /// 4500 counts over a 30 s epoch, and a noiseless still wrist to zero. So a
    /// night sits comfortably inside a uint32_t with several orders of
    /// magnitude of headroom, which is the only property this constant needs
    /// to have on its own. What it does *not* do is relate these counts to the
    /// units a published scorer was fitted against -- that is
    /// SleepWakeScorer::kCountScale, and it is a separate and unearned number.
    static constexpr float kCountsPerGSecond = 1000.0f;

    /// Samples further apart than this start a new integration rather than
    /// being bridged.
    ///
    /// A gap is not a slow sample: if delivery stopped for four seconds, the
    /// wrist did not spend four seconds at the last value it reported, and
    /// dt-weighting the next sample by the whole gap would invent a large count
    /// out of a delivery failure. Above this the sample re-seeds the filter and
    /// contributes nothing, and the epoch's `samples` count is what tells the
    /// scorer the epoch is thin.
    ///
    /// 500 ms is two periods of the low-pass corner: below it the filter state
    /// is still meaningful, above it the signal between the two samples is
    /// simply unknown.
    static constexpr uint32_t kMaxGapMs = 500;

    EpochCounter() = default;

    /**
     * @brief Feed one accelerometer sample.
     *
     * @param timestampMs Sensor's own timestamp, milliseconds. Monotonic;
     *                    unsigned differences are used, so a wrap is handled.
     *                    Must not be a clock read at handler time -- batches
     *                    arrive late and in bursts, and the loop's clock would
     *                    attribute a whole batch to the instant it was
     *                    delivered.
     * @param x,y,z       Acceleration in g.
     */
    void add(uint32_t timestampMs, float x, float y, float z);

    /**
     * @brief Close the epoch and take its count.
     *
     * Resets the accumulators but deliberately keeps the filter state -- see
     * the class comment.
     *
     * @param[out] count   Integrated band-limited acceleration, in the units
     *                     kCountsPerGSecond defines.
     * @param[out] peak    Largest single-sample rectified value seen, scaled
     *                     the same way and expressed per second, so it is
     *                     comparable across epochs of different length.
     * @param[out] samples Samples the epoch was built from, gaps excluded.
     */
    void closeEpoch(uint32_t &count, uint32_t &peak, uint16_t &samples);

    /// Samples accumulated into the epoch currently open.
    uint16_t pendingSamples() const { return mSamples; }

    /**
     * @brief Forget everything, filter state included.
     *
     * For a genuine discontinuity -- a resumed night after a restart, or the
     * start of a session -- where the previous state describes a different
     * stretch of time and carrying it forward would leak one night's last
     * movement into the next night's first epoch.
     */
    void reset();

private:
    /// One axis of the band-pass. Two one-poles cascaded for each of the
    /// high-pass and the low-pass, all four re-coefficiented per sample from
    /// the actual dt.
    ///
    /// Cascaded one-poles rather than biquads: a biquad's coefficients are far
    /// more expensive to recompute, and recomputing them every sample is the
    /// whole point -- the delivered rate is not the requested rate and is not
    /// even constant within a night.
    struct Axis
    {
        float hp1In  = 0.0f;  ///< Last input to the first high-pass pole.
        float hp1Out = 0.0f;
        float hp2Out = 0.0f;  ///< Second high-pass pole, fed by the first.
        float lp1Out = 0.0f;
        float lp2Out = 0.0f;  ///< Band-pass output.
        float sumGs  = 0.0f;  ///< Rectified, dt-weighted integral this epoch.

        /// Seed the filter state at @p v with zero output, so the first sample
        /// of a night does not push a full 1 g step through the high-pass and
        /// emit a settling transient several seconds long.
        void prime(float v);
        /// Advance by one sample and return the rectified band-pass output.
        float step(float v, float dt, float aHp, float bLp);
    };

    Axis mAxis[3];
    bool mPrimed = false;

    uint32_t mLastTsMs = 0;

    // Epoch accumulators. The per-axis sums are in g*s and are combined and
    // scaled only on close, so the scale factor is applied once rather than
    // 750 times.
    float    mPeakG   = 0.0f;
    uint16_t mSamples = 0;
};

} // namespace Engine

#endif // ENGINE_EPOCHCOUNTER_HPP
