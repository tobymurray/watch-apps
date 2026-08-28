#ifndef FMT_HPP
#define FMT_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>

/// Number formatting without floating-point printf.
///
/// The watch's newlib may not link floating-point `printf`, and when it does not
/// the failure is at runtime rather than at link time: `%f` emits nothing at all
/// while every host build and the simulator print it correctly. So a float that
/// reaches the screen through `%f` is a blank space on the device and a correct
/// number everywhere it could have been caught. SensorLab makes this a design
/// rule rather than a test for the same reason (its `Profile/Decimal.hpp`).
///
/// Everything here is integer arithmetic. Nothing in this app formats a float
/// through printf.
namespace Fmt {

/// What a non-finite or out-of-range value renders as. Deliberately not "0",
/// which would read as a measurement.
constexpr const char kNoValue[] = "---";

/// Largest magnitude representable after scaling, leaving headroom inside
/// int32_t for the rounding step.
constexpr int32_t kMaxScaled = 2000000000;

/// Round to a fixed number of decimals and return the scaled integer.
/// False when the value is not finite or does not fit.
inline bool scale(float v, uint8_t decimals, int32_t& out)
{
    if (!std::isfinite(v)) {
        return false;
    }

    float factor = 1.0f;
    for (uint8_t i = 0; i < decimals; ++i) {
        factor *= 10.0f;
    }

    const float scaled = v * factor;
    if (!(std::fabs(scaled) < static_cast<float>(kMaxScaled))) {
        return false;
    }

    out = static_cast<int32_t>(std::lround(scaled));
    return true;
}

/// Write a decimal integer. Returns the number of characters written, not
/// counting the terminator; 0 when it did not fit. Always terminates when
/// `cap` is at least 1.
inline size_t integer(char* buf, size_t cap, int32_t v)
{
    if (buf == nullptr || cap == 0) {
        return 0;
    }

    // Built in a scratch buffer and copied, so a value that does not fit writes
    // nothing rather than a truncated number that reads as a smaller one.
    char    tmp[12];
    size_t  n        = 0;
    bool    negative = v < 0;

    // Negated into a wider type: -2147483648 has no positive counterpart.
    int64_t magnitude = negative ? -static_cast<int64_t>(v) : static_cast<int64_t>(v);

    do {
        tmp[n++] = static_cast<char>('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude > 0 && n < sizeof(tmp));

    const size_t total = n + (negative ? 1u : 0u);
    if (total + 1 > cap) {
        buf[0] = '\0';
        return 0;
    }

    size_t w = 0;
    if (negative) {
        buf[w++] = '-';
    }
    while (n > 0) {
        buf[w++] = tmp[--n];
    }
    buf[w] = '\0';
    return w;
}

/// Write a fixed-point decimal, for example -12.34 at two decimals.
/// Writes `kNoValue` for a non-finite or unrepresentable value, so the caller
/// never has to decide what a missing number looks like.
inline size_t fixed(char* buf, size_t cap, float v, uint8_t decimals)
{
    if (buf == nullptr || cap == 0) {
        return 0;
    }

    auto writeNoValue = [&]() -> size_t {
        const size_t len = sizeof(kNoValue) - 1;
        if (len + 1 > cap) {
            buf[0] = '\0';
            return 0;
        }
        for (size_t i = 0; i < len; ++i) {
            buf[i] = kNoValue[i];
        }
        buf[len] = '\0';
        return len;
    };

    int32_t scaled = 0;
    if (!scale(v, decimals, scaled)) {
        return writeNoValue();
    }

    // The sign comes from the input, not from the rounded result. A value of
    // -0.004 at two decimals rounds to zero, and rendering it as "0.00" would
    // say it is not negative when it is. This is what printf does with "%.2f",
    // and on a diagnostic screen the distinction between "just below zero" and
    // "zero" is the kind of thing the screen exists to show.
    const bool negative = std::signbit(v);

    const int64_t absolute = (scaled < 0) ? -static_cast<int64_t>(scaled)
                                          : static_cast<int64_t>(scaled);

    int32_t divisor = 1;
    for (uint8_t i = 0; i < decimals; ++i) {
        divisor *= 10;
    }

    const int32_t whole = static_cast<int32_t>(absolute / divisor);
    const int32_t frac  = static_cast<int32_t>(absolute % divisor);

    char   scratch[24];
    size_t w = 0;
    if (negative) {
        scratch[w++] = '-';
    }

    const size_t wholeLen = integer(scratch + w, sizeof(scratch) - w, whole);
    if (wholeLen == 0) {
        return writeNoValue();
    }
    w += wholeLen;

    if (decimals > 0) {
        if (w + 1 >= sizeof(scratch)) {
            return writeNoValue();
        }
        scratch[w++] = '.';

        // Leading zeros of the fraction, which `integer()` would drop.
        int32_t place = divisor / 10;
        while (place > 0) {
            if (w + 1 >= sizeof(scratch)) {
                return writeNoValue();
            }
            scratch[w++] = static_cast<char>('0' + ((frac / place) % 10));
            place /= 10;
        }
    }

    if (w + 1 > cap) {
        buf[0] = '\0';
        return 0;
    }
    for (size_t i = 0; i < w; ++i) {
        buf[i] = scratch[i];
    }
    buf[w] = '\0';
    return w;
}

} // namespace Fmt

#endif // FMT_HPP
