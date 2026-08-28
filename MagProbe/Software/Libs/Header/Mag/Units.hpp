#ifndef MAG_UNITS_HPP
#define MAG_UNITS_HPP

#include "Mag/Vec3.hpp"

#include <cmath>
#include <cstdint>

namespace Mag {

/// What the delivered numbers might be.
///
/// `MAGNETIC_FIELD` (0x30) ships no parser, and `Docs/SensorsLayer.md` says of
/// the parser-less types only "assume layout known from driver docs". There are
/// no driver docs. The BMM350 datasheet is unsourced, and the part is not even
/// confirmed to be a BMM350: something answers at I2C4/0x14 and a CHIP_ID read
/// did not match (SensorLab Docs/EXPECTED.md). So the units are genuinely
/// unknown, and this app's job is to narrow them rather than to assume one.
///
/// The narrowing is by magnitude. Earth's total field is between about 22 and
/// 67 microtesla everywhere on the surface, so the magnitude of a real reading
/// has to land in a known band once the unit is chosen correctly. The bands
/// below do not overlap, which is what makes the answer a classification rather
/// than a guess.
enum class Units : uint8_t {
    Unknown = 0,   ///< Nothing measured yet.
    NonFinite,     ///< A field was NaN or infinite. A defect, not a unit.
    AllZero,       ///< Delivering, and every axis is exactly zero.
    Gauss,         ///< Magnitude consistent with gauss.
    Microtesla,    ///< Magnitude consistent with microtesla.
    RawCounts,     ///< Too large for either, consistent with unscaled LSBs.
    Unclassified,  ///< Finite, non-zero, and in none of the bands.
};

/// Earth's field, as the widest band that is still a constraint.
///
/// The IGRF total-intensity range over the surface is about 22 to 67 uT. These
/// are deliberately wider than that: a band that excludes a real reading turns
/// a working magnetometer into a "no" verdict, which is the expensive error
/// here. A band that is too wide only fails to narrow.
constexpr float kEarthMinUT = 20.0f;
constexpr float kEarthMaxUT = 75.0f;

/// 1 gauss = 100 microtesla.
constexpr float kGaussPerUT = 0.01f;

/// Consistent with unscaled sensor counts. The lower bound sits well above the
/// microtesla band so the two cannot both match, and the upper bound admits an
/// LSB-per-microtesla scale up to about 1000, which covers every part in this
/// class that has a published figure.
constexpr float kRawCountsMin = 2000.0f;
constexpr float kRawCountsMax = 100000.0f;

/// Classify a magnitude. Takes the magnitude rather than the vector so that a
/// caller who has already subtracted a hard-iron offset can classify the
/// corrected value, which is the one that should land in the band.
inline Units classifyMagnitude(float magnitude)
{
    if (!std::isfinite(magnitude)) {
        return Units::NonFinite;
    }
    if (magnitude == 0.0f) {
        return Units::AllZero;
    }
    if (magnitude >= kEarthMinUT * kGaussPerUT && magnitude <= kEarthMaxUT * kGaussPerUT) {
        return Units::Gauss;
    }
    if (magnitude >= kEarthMinUT && magnitude <= kEarthMaxUT) {
        return Units::Microtesla;
    }
    if (magnitude >= kRawCountsMin && magnitude <= kRawCountsMax) {
        return Units::RawCounts;
    }
    return Units::Unclassified;
}

/// Classify a reading. A non-finite component is reported as such even when the
/// magnitude it produces happens to be finite.
inline Units classify(const Vec3& v)
{
    if (!isFinite(v)) {
        return Units::NonFinite;
    }
    if (v.x == 0.0f && v.y == 0.0f && v.z == 0.0f) {
        return Units::AllZero;
    }
    return classifyMagnitude(norm(v));
}

/// Short label, for a 240 px screen and for a log line.
inline const char* name(Units u)
{
    switch (u) {
        case Units::Unknown:      return "UNKNOWN";
        case Units::NonFinite:    return "NONFINITE";
        case Units::AllZero:      return "ALL ZERO";
        case Units::Gauss:        return "GAUSS?";
        case Units::Microtesla:   return "MICROTESLA?";
        case Units::RawCounts:    return "RAW COUNTS?";
        case Units::Unclassified: return "UNCLASSIFIED";
    }
    return "UNKNOWN";
}

/// A single magnitude in a band is weak evidence: a constant offset with no
/// real field behind it can sit in the band too. What separates them is that a
/// real field's magnitude is very nearly invariant under rotation, so the
/// spread of the magnitude across a slow full rotation is the actual test.
///
/// This holds the spread, and reports it as a fraction of the mean, which is
/// the form the verdict needs and the form that does not depend on the unit.
class MagnitudeSpread {
public:
    void add(float magnitude)
    {
        if (!std::isfinite(magnitude)) {
            ++mNonFinite;
            return;
        }
        if (mCount == 0 || magnitude < mMin) {
            mMin = magnitude;
        }
        if (mCount == 0 || magnitude > mMax) {
            mMax = magnitude;
        }
        mSum += static_cast<double>(magnitude);
        ++mCount;
    }

    uint32_t count() const { return mCount; }
    uint32_t nonFinite() const { return mNonFinite; }
    float    min() const { return mCount ? mMin : 0.0f; }
    float    max() const { return mCount ? mMax : 0.0f; }
    float    mean() const { return mCount ? static_cast<float>(mSum / mCount) : 0.0f; }

    /// (max - min) / mean, or a negative value when there is nothing to report.
    /// A rigid rotation of a real field gives a small number here; a dead axis
    /// or a saturated one gives a large one.
    float spreadFraction() const
    {
        if (mCount < 2) {
            return -1.0f;
        }
        const float m = mean();
        if (!(m > 0.0f)) {
            return -1.0f;
        }
        return (mMax - mMin) / m;
    }

private:
    double   mSum{0.0};
    float    mMin{0.0f};
    float    mMax{0.0f};
    uint32_t mCount{0};
    uint32_t mNonFinite{0};
};

} // namespace Mag

#endif // MAG_UNITS_HPP
