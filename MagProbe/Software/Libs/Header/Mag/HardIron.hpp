#ifndef MAG_HARD_IRON_HPP
#define MAG_HARD_IRON_HPP

#include "Mag/Vec3.hpp"

#include <cstdint>

namespace Mag {

/// Hard-iron offset estimation by axis extremes.
///
/// A magnetometer on a wrist sits inside a steel case next to a vibration motor
/// and a battery, all of which add a constant vector in the device frame. That
/// offset is usually larger than Earth's field, so an uncorrected heading can be
/// wrong by any amount at all, and the error is not a constant bias: it varies
/// with orientation, which is exactly what makes it look plausible.
///
/// The correction is the centre of the sphere the readings trace out as the
/// device is rotated through every attitude. This takes the midpoint of each
/// axis's observed range, which is the cheapest estimator of that centre and the
/// standard one for a figure-of-eight routine. It assumes the axes are not
/// scaled differently from each other (no soft-iron correction) and that the
/// rotation actually covered both extremes of every axis. The second assumption
/// is checkable, so `quality()` checks it, because an offset taken from a
/// partial rotation is worse than none: it is wrong and it looks calibrated.
///
/// The SDK ships nothing for this. `Libs/Header/SDK/Calibration` is stride and
/// treadmill speed only.
class HardIron {
public:
    /// A span below this fraction of the largest span means that axis was not
    /// swept. Not a physical constant: it is the point at which the midpoint of
    /// a range stops being an estimate of a centre. Deliberately lenient, so
    /// that a sloppy but genuine figure-of-eight passes.
    static constexpr float kMinSpanFraction = 0.40f;

    /// Fewer samples than this and the extremes are noise, whatever they look
    /// like. At the roughly 1 s aggregation the sensor layer imposes, this is a
    /// few tens of seconds of rotation.
    static constexpr uint32_t kMinSamples = 24;

    enum class Quality : uint8_t {
        Empty = 0,      ///< Nothing collected.
        TooFewSamples,  ///< Collected, and not enough to trust extremes.
        Lopsided,       ///< Enough samples, but an axis was never swept.
        Usable,         ///< All three axes swept.
    };

    void reset()
    {
        *this = HardIron{};
    }

    void add(const Vec3& v)
    {
        if (!isFinite(v)) {
            ++mRejected;
            return;
        }

        if (mCount == 0) {
            mMin = v;
            mMax = v;
        } else {
            if (v.x < mMin.x) { mMin.x = v.x; }
            if (v.y < mMin.y) { mMin.y = v.y; }
            if (v.z < mMin.z) { mMin.z = v.z; }
            if (v.x > mMax.x) { mMax.x = v.x; }
            if (v.y > mMax.y) { mMax.y = v.y; }
            if (v.z > mMax.z) { mMax.z = v.z; }
        }
        ++mCount;
    }

    uint32_t samples() const { return mCount; }
    uint32_t rejected() const { return mRejected; }

    /// Per-axis observed range.
    Vec3 spans() const
    {
        if (mCount == 0) {
            return Vec3{};
        }
        return Vec3{mMax.x - mMin.x, mMax.y - mMin.y, mMax.z - mMin.z};
    }

    /// The estimated offset. Zero when nothing has been collected, so an
    /// uncalibrated app subtracts nothing rather than subtracting rubbish.
    Vec3 offsets() const
    {
        if (mCount == 0) {
            return Vec3{};
        }
        return Vec3{(mMax.x + mMin.x) * 0.5f,
                    (mMax.y + mMin.y) * 0.5f,
                    (mMax.z + mMin.z) * 0.5f};
    }

    Quality quality() const
    {
        if (mCount == 0) {
            return Quality::Empty;
        }
        if (mCount < kMinSamples) {
            return Quality::TooFewSamples;
        }

        const Vec3  s       = spans();
        const float largest = (s.x > s.y ? (s.x > s.z ? s.x : s.z)
                                         : (s.y > s.z ? s.y : s.z));
        if (!(largest > 0.0f)) {
            return Quality::Lopsided;
        }

        const float floorSpan = largest * kMinSpanFraction;
        if (s.x < floorSpan || s.y < floorSpan || s.z < floorSpan) {
            return Quality::Lopsided;
        }
        return Quality::Usable;
    }

    /// Apply the offset. Applying an unusable calibration is refused rather than
    /// silently done, so a heading is never quietly built on a bad centre.
    bool apply(const Vec3& raw, Vec3& out) const
    {
        if (quality() != Quality::Usable) {
            return false;
        }
        out = raw - offsets();
        return true;
    }

    static const char* name(Quality q)
    {
        switch (q) {
            case Quality::Empty:         return "NONE";
            case Quality::TooFewSamples: return "TOO FEW";
            case Quality::Lopsided:      return "LOPSIDED";
            case Quality::Usable:        return "USABLE";
        }
        return "NONE";
    }

private:
    Vec3     mMin{};
    Vec3     mMax{};
    uint32_t mCount{0};
    uint32_t mRejected{0};
};

} // namespace Mag

#endif // MAG_HARD_IRON_HPP
