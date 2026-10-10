#ifndef SLUMBER_TILT_HPP
#define SLUMBER_TILT_HPP

#include <cmath>
#include <cstdint>

namespace Slumber
{

/// How far the wrist turned, from the gravity vector averaged over 5 s blocks.
///
/// van Hees et al. 2015 (PLoS One 10:e0142533) find sleep where the 5 s arm
/// angle stops changing. This logs the angle between consecutive blocks'
/// gravity vectors, which bounds the change in any one axis's angle, so it
/// holds whichever axis on this watch faces out of the dial.
class Tilt
{
public:
    static constexpr uint32_t kBlockMs = 5000;
    /// Further apart than this, two samples do not share a block.
    static constexpr uint32_t kMaxGapMs = 2000;

    struct Out {
        int16_t  meanX1000[3]   = {0, 0, 0};  ///< sensor units x1000, over every sample this epoch
        uint16_t turnSumDegX10  = 0;          ///< summed between consecutive blocks
        uint16_t turnMaxDegX10  = 0;
        uint8_t  blocks         = 0;          ///< blocks closed this epoch
        bool     any            = false;      ///< false = no samples this epoch
    };

    void add(uint32_t timestampMs, float x, float y, float z)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            return;
        }
        if (mInBlock && timestampMs - mLastTsMs > kMaxGapMs) {
            mInBlock   = false;
            mHavePrev  = false;
        }
        if (!mInBlock) {
            mInBlock      = true;
            mBlockStartMs = timestampMs;
            mBlock[0] = mBlock[1] = mBlock[2] = 0.0f;
            mBlockN = 0;
        }
        mLastTsMs = timestampMs;

        mBlock[0] += x;
        mBlock[1] += y;
        mBlock[2] += z;
        ++mBlockN;
        mEpoch[0] += x;
        mEpoch[1] += y;
        mEpoch[2] += z;
        ++mEpochN;

        if (timestampMs - mBlockStartMs >= kBlockMs) {
            closeBlock();
        }
    }

    Out close()
    {
        Out o;
        if (mEpochN > 0) {
            o.any = true;
            for (int a = 0; a < 3; ++a) {
                o.meanX1000[a] = clampI16(mEpoch[a] / static_cast<float>(mEpochN) * 1000.0f);
            }
        }
        o.turnSumDegX10 = clampU16(mTurnSum * 10.0f);
        o.turnMaxDegX10 = clampU16(mTurnMax * 10.0f);
        o.blocks        = mBlocks;
        mEpoch[0] = mEpoch[1] = mEpoch[2] = 0.0f;
        mEpochN   = 0;
        mTurnSum  = 0.0f;
        mTurnMax  = 0.0f;
        mBlocks   = 0;
        return o;
    }

private:
    void closeBlock()
    {
        const float n = static_cast<float>(mBlockN);
        const float g[3] = {mBlock[0] / n, mBlock[1] / n, mBlock[2] / n};
        if (mHavePrev) {
            const float turn = degreesBetween(mPrev, g);
            mTurnSum += turn;
            if (turn > mTurnMax) {
                mTurnMax = turn;
            }
        }
        mPrev[0]  = g[0];
        mPrev[1]  = g[1];
        mPrev[2]  = g[2];
        mHavePrev = true;
        mInBlock  = false;
        if (mBlocks < 0xFF) {
            ++mBlocks;
        }
    }

    static float degreesBetween(const float a[3], const float b[3])
    {
        const float la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        const float lb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
        if (la <= 0.0f || lb <= 0.0f) {
            return 0.0f;
        }
        float c = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb);
        c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
        return std::acos(c) * 57.2957795f;
    }

    static int16_t clampI16(float v)
    {
        return static_cast<int16_t>(
            std::lround(v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v)));
    }

    static uint16_t clampU16(float v)
    {
        return static_cast<uint16_t>(std::lround(v > 65535.0f ? 65535.0f : (v < 0.0f ? 0.0f : v)));
    }

    bool     mInBlock      = false;
    bool     mHavePrev     = false;
    uint32_t mBlockStartMs = 0;
    uint32_t mLastTsMs     = 0;
    float    mBlock[3]     = {0.0f, 0.0f, 0.0f};
    uint32_t mBlockN       = 0;
    float    mPrev[3]      = {0.0f, 0.0f, 0.0f};

    float    mEpoch[3] = {0.0f, 0.0f, 0.0f};
    uint32_t mEpochN   = 0;
    float    mTurnSum  = 0.0f;
    float    mTurnMax  = 0.0f;
    uint8_t  mBlocks   = 0;
};

} // namespace Slumber

#endif // SLUMBER_TILT_HPP
