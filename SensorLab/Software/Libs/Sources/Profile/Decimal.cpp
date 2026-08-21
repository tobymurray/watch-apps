/**
 ******************************************************************************
 * @file    Decimal.cpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   A float as two integers. Rationale is in the header.
 ******************************************************************************
 */

#include "Profile/Decimal.hpp"

#include <cstdio>

namespace SensorLab::Profile
{

namespace
{

/// Absolute value without <cmath>, which the three builds have already been
/// caught disagreeing about (ledger row P14).
float absf(float v)
{
    return (v < 0.0f) ? -v : v;
}

/// 10^n for n in [0, 9]. Integer, so the digit extraction below never touches a
/// float.
int32_t pow10i(int32_t n)
{
    int32_t r = 1;
    for (int32_t i = 0; i < n; i++) {
        r *= 10;
    }
    return r;
}

/// Guard on the normalisation loops. Float's decimal exponent range is
/// [-45, 38], so 64 is comfortably past any real input and stops a NaN that
/// slipped the classification from spinning.
constexpr int32_t kMaxScaleSteps = 64;

} // namespace

const char *toString(DecimalKind k)
{
    switch (k) {
        case DecimalKind::Finite: return "finite";
        case DecimalKind::Zero:   return "zero";
        case DecimalKind::PosInf: return "+inf";
        case DecimalKind::NegInf: return "-inf";
        case DecimalKind::NaN:    return "nan";
    }
    return "?";
}

Decimal decompose(float v)
{
    Decimal d {};

    // Classification by exponent bits rather than by <cmath>, so this behaves
    // identically in all three builds.
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "float must be 32 bits here");
    __builtin_memcpy(&bits, &v, sizeof(bits));

    const uint32_t exponentBits = (bits >> 23) & 0xFFu;
    const uint32_t mantissaBits = bits & 0x7FFFFFu;
    const bool     negative     = (bits & 0x80000000u) != 0;

    if (exponentBits == 0xFFu) {
        if (mantissaBits != 0u) {
            d.kind = DecimalKind::NaN;
        } else {
            d.kind = negative ? DecimalKind::NegInf : DecimalKind::PosInf;
        }
        return d;
    }

    if (v == 0.0f) {
        d.kind = DecimalKind::Zero;
        return d;
    }

    // Normalise the magnitude into [10^6, 10^7), tracking the exponent. A
    // straight loop rather than a table of powers: this runs once per claim
    // written -- about two thousand times per profile, never on the sample path
    // -- so fifty float divides are not worth an optimisation that could be
    // subtly wrong at the denormal end.
    float   mag  = absf(v);
    int32_t exp  = 0;
    int32_t step = 0;

    while (mag >= static_cast<float>(kMantissaMax) && step < kMaxScaleSteps) {
        mag /= 10.0f;
        exp += 1;
        step++;
    }
    while (mag < static_cast<float>(kMantissaMin) && step < kMaxScaleSteps) {
        mag *= 10.0f;
        exp -= 1;
        step++;
    }

    // Round half away from zero, then re-normalise: the last multiply can leave
    // the value one ulp either side of the window, and 9999999.6 rounds to
    // 10000000.
    int32_t mantissa = static_cast<int32_t>(mag + 0.5f);
    while (mantissa >= kMantissaMax) {
        mantissa /= 10;
        exp += 1;
    }
    if (mantissa == 0) {
        // Only reachable for a denormal so small the scale loop ran out of
        // steps before reaching the window. Reported as zero rather than as a
        // fabricated mantissa: at this magnitude there is no measurement.
        d.kind = DecimalKind::Zero;
        return d;
    }

    // int8 exponent: float's range is 10^-45 to 10^38 and the window shift is
    // six digits, so [-51, 32]. Comfortably inside.
    d.kind     = DecimalKind::Finite;
    d.mantissa = negative ? -mantissa : mantissa;
    d.exponent = static_cast<int8_t>(exp);
    return d;
}

float recompose(const Decimal &d)
{
    switch (d.kind) {
        case DecimalKind::Zero:   return 0.0f;
        case DecimalKind::PosInf: return __builtin_inff();
        case DecimalKind::NegInf: return -__builtin_inff();
        case DecimalKind::NaN:    return __builtin_nanf("");
        case DecimalKind::Finite: break;
    }

    float   v   = static_cast<float>(d.mantissa);
    int32_t exp = d.exponent;
    while (exp > 0) {
        v *= 10.0f;
        exp--;
    }
    while (exp < 0) {
        v /= 10.0f;
        exp++;
    }
    return v;
}

size_t format(char *out, size_t outSize, const Decimal &d)
{
    if (out == nullptr || outSize == 0) {
        return 0;
    }
    out[0] = '\0';

    if (d.kind != DecimalKind::Finite) {
        const int n = std::snprintf(out, outSize, "%s", toString(d.kind));
        return (n > 0 && static_cast<size_t>(n) < outSize)
                   ? static_cast<size_t>(n) : 0;
    }

    const bool    negative = d.mantissa < 0;
    const int32_t mag      = negative ? -d.mantissa : d.mantissa;

    // Seven mantissa digits, most significant first. Integer division only.
    char digits[kDecimalDigits + 1];
    for (int32_t i = 0; i < kDecimalDigits; i++) {
        digits[i] = static_cast<char>(
            '0' + ((mag / pow10i(kDecimalDigits - 1 - i)) % 10));
    }
    digits[kDecimalDigits] = '\0';

    // Where the decimal point falls relative to the first digit. Positive means
    // that many digits precede it.
    const int32_t pointAt = kDecimalDigits + d.exponent;

    // Trim trailing zeros, but never past the decimal point: 20.400000 becomes
    // 20.4 while 20400000 keeps its zeros because they are magnitude, not
    // precision.
    int32_t significant = kDecimalDigits;
    while (significant > 1 && significant > pointAt
           && digits[significant - 1] == '0') {
        significant--;
    }

    char   body[kDecimalStringMax] {};
    size_t n    = 0;
    const size_t cap = sizeof(body) - 1;

    if (pointAt <= 0) {
        // 0.000...digits -- a value smaller than one.
        if (n < cap) { body[n++] = '0'; }
        if (n < cap) { body[n++] = '.'; }
        for (int32_t i = 0; i < -pointAt && n < cap; i++) {
            body[n++] = '0';
        }
        for (int32_t i = 0; i < significant && n < cap; i++) {
            body[n++] = digits[i];
        }
    } else if (pointAt >= significant) {
        // An integer: significant digits then magnitude zeros.
        for (int32_t i = 0; i < significant && n < cap; i++) {
            body[n++] = digits[i];
        }
        for (int32_t i = significant; i < pointAt && n < cap; i++) {
            body[n++] = '0';
        }
    } else {
        for (int32_t i = 0; i < significant && n < cap; i++) {
            if (i == pointAt) {
                body[n++] = '.';
                if (n >= cap) { break; }
            }
            body[n++] = digits[i];
        }
    }
    body[n] = '\0';

    const int written = std::snprintf(out, outSize, "%s%s",
                                      negative ? "-" : "", body);
    if (written <= 0 || static_cast<size_t>(written) >= outSize) {
        out[0] = '\0';
        return 0;
    }
    return static_cast<size_t>(written);
}

size_t format(char *out, size_t outSize, float v)
{
    return format(out, outSize, decompose(v));
}

} // namespace SensorLab::Profile
