#ifndef MAG_VEC3_HPP
#define MAG_VEC3_HPP

#include <cmath>

namespace Mag {

/// A three-axis reading in the device frame, in whatever units the driver
/// delivered. Nothing here assumes a unit, because for MAGNETIC_FIELD nobody
/// knows one yet: see Units.hpp.
struct Vec3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

inline Vec3 operator-(const Vec3& a, const Vec3& b)
{
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

inline float dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b)
{
    return Vec3{a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}

inline float norm(const Vec3& v)
{
    return std::sqrt(dot(v, v));
}

/// Every field of the vector is finite. A NaN reaching the trigonometry turns
/// into a heading of NaN, which formats as a plausible-looking number rather
/// than as an obvious fault, so it is rejected at the boundary instead.
inline bool isFinite(const Vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

/// Unit vector, or false when the input is too short to have a direction.
/// kMinNorm is not a physical threshold: it is the point below which the
/// division stops carrying information.
inline bool normalize(const Vec3& v, Vec3& out)
{
    constexpr float kMinNorm = 1e-6f;

    if (!isFinite(v)) {
        return false;
    }

    const float n = norm(v);
    if (!(n > kMinNorm)) {
        return false;
    }

    out = Vec3{v.x / n, v.y / n, v.z / n};
    return true;
}

} // namespace Mag

#endif // MAG_VEC3_HPP
