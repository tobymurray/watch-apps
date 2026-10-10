#ifndef SLUMBER_ACTIVITY_COUNT_HPP
#define SLUMBER_ACTIVITY_COUNT_HPP

#include <cmath>
#include <cstdint>

namespace Slumber
{

/// Activity counts on SleepLab's `Engine::EpochCounter` scale, which is the
/// scale SleepPeriods' thresholds were measured in.
///
/// The same arithmetic, sample for sample: a two-pole 0.25 Hz high-pass and a
/// two-pole 3 Hz low-pass per axis, re-coefficiented from each sample's own
/// time step, rectified, dt-weighted and integrated, then combined as a vector
/// magnitude. SleepLab's header holds the reasoning for each step.
class ActivityCount
{
public:
    static constexpr float    kHighPassHz       = 0.25f;
    static constexpr float    kLowPassHz        = 3.00f;
    static constexpr float    kCountsPerGSecond = 1000.0f;
    static constexpr uint32_t kMaxGapMs         = 500;

    struct Out {
        uint32_t count   = 0;
        uint32_t peak    = 0;  ///< largest rectified sample, same scale
        uint32_t axis[3] = {0, 0, 0};
        uint16_t samples = 0;  ///< gaps excluded
    };

    void add(uint32_t timestampMs, float x, float y, float z)
    {
        const float in[3] = {x, y, z};
        for (float v : in) {
            if (!std::isfinite(v)) {
                return;
            }
        }
        if (!mPrimed) {
            for (int a = 0; a < 3; ++a) {
                mAxis[a].prime(in[a]);
            }
            mLastTsMs = timestampMs;
            mPrimed   = true;
            ++mSamples;
            return;
        }

        const uint32_t dtMs = timestampMs - mLastTsMs;
        mLastTsMs = timestampMs;
        if (dtMs == 0) {
            for (int a = 0; a < 3; ++a) {
                mAxis[a].hp1In = in[a];
            }
            return;
        }
        if (dtMs > kMaxGapMs) {
            for (int a = 0; a < 3; ++a) {
                mAxis[a].prime(in[a]);
            }
            return;
        }

        const float dt  = static_cast<float>(dtMs) * 0.001f;
        const float aHp = tau(kHighPassHz) / (tau(kHighPassHz) + dt);
        const float bLp = dt / (tau(kLowPassHz) + dt);

        float sumSq = 0.0f;
        for (int a = 0; a < 3; ++a) {
            const float r = mAxis[a].step(in[a], aHp, bLp);
            mAxis[a].sumGs += r * dt;
            sumSq += r * r;
        }
        const float instantaneous = std::sqrt(sumSq);
        if (instantaneous > mPeakG) {
            mPeakG = instantaneous;
        }
        ++mSamples;
    }

    /// Takes the epoch's counts and keeps the filter state running.
    Out close()
    {
        Out o;
        const float sum = std::sqrt(mAxis[0].sumGs * mAxis[0].sumGs +
                                    mAxis[1].sumGs * mAxis[1].sumGs +
                                    mAxis[2].sumGs * mAxis[2].sumGs);
        o.count   = scaled(sum);
        o.peak    = scaled(mPeakG);
        o.samples = mSamples;
        for (int a = 0; a < 3; ++a) {
            o.axis[a]      = scaled(mAxis[a].sumGs);
            mAxis[a].sumGs = 0.0f;
        }
        mPeakG   = 0.0f;
        mSamples = 0;
        return o;
    }

private:
    static constexpr float kTwoPi = 6.28318530718f;

    static constexpr float tau(float hz) { return 1.0f / (kTwoPi * hz); }

    static uint32_t scaled(float gs)
    {
        const float v = gs * kCountsPerGSecond;
        return !(v < 4.0e9f) ? 0xFFFFFFFFu : static_cast<uint32_t>(v);
    }

    struct Axis {
        float hp1In  = 0.0f;
        float hp1Out = 0.0f;
        float hp2Out = 0.0f;
        float lp1Out = 0.0f;
        float lp2Out = 0.0f;
        float sumGs  = 0.0f;

        void prime(float v)
        {
            hp1In  = v;
            hp1Out = 0.0f;
            hp2Out = 0.0f;
            lp1Out = 0.0f;
            lp2Out = 0.0f;
        }

        float step(float v, float aHp, float bLp)
        {
            const float hp1 = aHp * (hp1Out + v - hp1In);
            hp1In           = v;
            const float hp2 = aHp * (hp2Out + hp1 - hp1Out);
            hp1Out          = hp1;
            hp2Out          = hp2;
            lp1Out += bLp * (hp2 - lp1Out);
            lp2Out += bLp * (lp1Out - lp2Out);
            return std::fabs(lp2Out);
        }
    };

    Axis     mAxis[3];
    bool     mPrimed   = false;
    uint32_t mLastTsMs = 0;
    float    mPeakG    = 0.0f;
    uint16_t mSamples  = 0;
};

} // namespace Slumber

#endif // SLUMBER_ACTIVITY_COUNT_HPP
