#ifndef MAG_HEADING_HPP
#define MAG_HEADING_HPP

#include "Mag/Vec3.hpp"

#include <cmath>
#include <cstdint>

namespace Mag {

/// Tilt-compensated heading, and the dip angle that says whether to believe it.
///
/// The naive compass in the SDK's Sensors tutorial is
/// `atan2f(view.f[1], view.f[0])`, which is correct only for a device held
/// exactly level with no hard-iron offset. A wrist is never level. Off level,
/// the vertical component of the field leaks into the horizontal axes, and
/// because Earth's field is steeply inclined at most latitudes that component
/// is the largest one: at 70 degrees of dip it is nearly three times the
/// horizontal, so a few degrees of wrist tilt moves the reported heading by
/// tens of degrees.
///
/// Compensating needs an up vector, which the accelerometer supplies whenever
/// the device is not accelerating. So a heading here is only as good as the
/// assumption that the wrist is momentarily still, and `Result::levelled` says
/// whether that assumption was checked and held.
///
/// The formulation is the vector one rather than the roll/pitch one: it needs no
/// trigonometry beyond the final `atan2`, has no gimbal case at 90 degrees of
/// pitch, and its two intermediate quantities are directly checkable in a test.

/// Which way the accelerometer's vector points when the watch lies face up.
///
/// An accelerometer measuring specific force reports +1 g on the axis pointing
/// up, because it senses the reaction to gravity rather than gravity. Parts in
/// this class normally do that, and RustGuiPoc's fixtures assume it, but nobody
/// has written down a measurement for this watch. One reading with the watch
/// flat on a table settles it, and until then this is a setting rather than a
/// constant.
enum class UpConvention : uint8_t {
    AccelPointsUp = 0,  ///< At rest, the accelerometer vector points up.
    AccelPointsDown,    ///< At rest, it points down. Negate to get up.
};

struct Result {
    /// Degrees clockwise from magnetic north, in [0, 360). The heading of the
    /// device's +Y axis, which is 12 o'clock on the display.
    float headingDeg{0.0f};

    /// Field inclination in degrees, positive when the field points downward
    /// into the ground, which is the northern-hemisphere case. This is the
    /// strongest single plausibility check available without a datasheet: the
    /// dip at a given place is known to a fraction of a degree from any
    /// geomagnetic model, and it is a property of the field rather than of the
    /// scale, so it holds whatever the units turn out to be.
    float dipDeg{0.0f};

    /// The horizontal field strength, in the input's units. Falls toward zero as
    /// the dip approaches vertical, and a heading computed from a near-zero
    /// horizontal component is noise regardless of how the arithmetic went.
    float horizontalStrength{0.0f};

    /// The heading and dip are meaningful.
    bool valid{false};

    /// The accelerometer magnitude was close enough to one gravity that the
    /// device was plausibly at rest. False means the heading was computed
    /// anyway and should not be trusted; it is not suppressed, because a probe
    /// that hides its inputs cannot be argued with.
    bool levelled{false};
};

/// How far the accelerometer magnitude may sit from 1 g and still count as at
/// rest. Wide, because this gates a warning rather than the arithmetic.
constexpr float kRestBandG = 0.25f;

/// Below this fraction of the total field, the horizontal component is too
/// small to define a direction. At 85 degrees of dip the ratio is under 0.09,
/// which is genuinely unusable rather than merely poor.
constexpr float kMinHorizontalFraction = 0.05f;

/// `mag` must already have its hard-iron offset removed. `accel` is in g, in
/// the same device frame.
inline Result compute(const Vec3&  mag,
                      const Vec3&  accel,
                      UpConvention convention = UpConvention::AccelPointsUp)
{
    Result out;

    Vec3 up;
    if (!normalize(accel, up)) {
        return out;
    }
    if (convention == UpConvention::AccelPointsDown) {
        up = Vec3{-up.x, -up.y, -up.z};
    }

    Vec3 field;
    if (!normalize(mag, field)) {
        return out;
    }

    const float accelMag = norm(accel);
    out.levelled = std::fabs(accelMag - 1.0f) <= kRestBandG;

    // Vertical component of the field, and what is left after removing it: the
    // horizontal projection, which points at magnetic north.
    //
    // Worth being precise about what this projection does, because it is not
    // the tilt compensation. Any part of the field parallel to `up` drops out of
    // the final atan2 on its own: `cross(f, u) . u` is zero for it, and
    // `f . u_component` is zero because `forwardHat` is horizontal by
    // construction. The compensation is `up` itself, which is why this needs the
    // accelerometer and why the naive atan2(y, x) cannot be rescued without one.
    // The projection earns its place by making the degenerate case detectable:
    // a field with no horizontal part normalises to nothing, and that is a
    // refusal rather than a heading built out of rounding error.
    const float vertical = dot(field, up);
    const Vec3  north    = Vec3{field.x - up.x * vertical,
                                field.y - up.y * vertical,
                                field.z - up.z * vertical};

    // Dip is defined off the normalised field, so it is a pure angle and does
    // not depend on the unit. Clamped because rounding can push the dot product
    // a hair outside asin's domain.
    const float clamped = (vertical > 1.0f) ? 1.0f : ((vertical < -1.0f) ? -1.0f : vertical);
    out.dipDeg = -std::asin(clamped) * (180.0f / static_cast<float>(M_PI));

    out.horizontalStrength = norm(north) * norm(mag);

    Vec3 northHat;
    if (!normalize(north, northHat)) {
        return out;
    }
    if (norm(north) < kMinHorizontalFraction) {
        // Field is essentially vertical. Dip is still reportable and the heading
        // is not.
        return out;
    }

    // The device's 12 o'clock direction, projected onto the same horizontal
    // plane. Held flat this is just +Y; tilted, it is the part of +Y that lies
    // in the plane the heading is measured in.
    const Vec3  forwardAxis = Vec3{0.0f, 1.0f, 0.0f};
    const float forwardUp   = dot(forwardAxis, up);
    const Vec3  forward     = Vec3{forwardAxis.x - up.x * forwardUp,
                                   forwardAxis.y - up.y * forwardUp,
                                   forwardAxis.z - up.z * forwardUp};

    Vec3 forwardHat;
    if (!normalize(forward, forwardHat)) {
        // Watch is on its edge with 12 o'clock pointing straight up or straight
        // down, so it has no heading to report.
        return out;
    }

    const float sine   = dot(cross(forwardHat, northHat), up);
    const float cosine = dot(northHat, forwardHat);

    float deg = std::atan2(sine, cosine) * (180.0f / static_cast<float>(M_PI));
    if (deg < 0.0f) {
        deg += 360.0f;
    }
    if (deg >= 360.0f) {
        deg -= 360.0f;
    }

    out.headingDeg = deg;
    out.valid      = true;
    return out;
}

/// The eight-point cardinal name, for a screen too small for a rose.
inline const char* cardinal(float headingDeg)
{
    static const char* kNames[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

    if (!std::isfinite(headingDeg)) {
        return "?";
    }

    float d = std::fmod(headingDeg, 360.0f);
    if (d < 0.0f) {
        d += 360.0f;
    }

    const int idx = static_cast<int>((d + 22.5f) / 45.0f) & 7;
    return kNames[idx];
}

} // namespace Mag

#endif // MAG_HEADING_HPP
