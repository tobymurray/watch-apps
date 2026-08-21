/**
 ******************************************************************************
 * @file    Decimal.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   A float as two integers, because %f is not trusted on this device.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * Why this exists at all
 *
 * The watch's newlib may not link floating-point `printf` conversions. When it
 * does not, `%f` and `%g` do not fail loudly -- they emit nothing, or garbage,
 * at runtime, on hardware, in a file nobody reads until the next morning. Every
 * app in this repository that writes numbers therefore writes integers:
 * SleepLab's summary JSON casts every value to `int32_t` and puts the scale in
 * the key name (`count_scale_x1e6`, `batt_ma_x10`), and the Sleep Probe's log
 * says outright that "a diagnostic that silently prints empty strings for its
 * own measurements is worse than one that scales by ten".
 *
 * `SDK::JsonStreamWriter::add(key, float)` goes through `snprintf("%g")`, so it
 * inherits the risk. This app does not use it.
 *
 * But a *profiler* cannot use one fixed scale. Its values span station pressure
 * near 101 325 Pa and a recovered accelerometer LSB near 0.000 061 g -- nine
 * orders of magnitude, in the same document, in rows whose scale is not known
 * until the measurement is taken. A single `_x1000` convention would overflow
 * one end and quantise the other to zero.
 *
 * ---------------------------------------------------------------------------
 * So: a mantissa and a decimal exponent
 *
 *     value = mantissa * 10^exponent
 *
 * with `mantissa` an `int32_t` normalised to seven significant digits and
 * `exponent` an `int8_t`. That is integer-only on the wire, exact to float's own
 * ~7.2 decimal digits, and it never overflows or underflows inside float's
 * range. The host tools reconstitute a float; nothing on the device ever
 * formats one.
 *
 * The cost is that `profile.json` is less pleasant to read raw. That is the
 * right trade: `SENSOR-PROFILE.md` is what a person reads, `profile_report.py`
 * renders it, and a number that is occasionally silently blank on hardware
 * would be worse in every way.
 *
 * Non-finite values get their own encoding rather than a number, because a NaN
 * that arrived through a sensor is a finding and one that arrived through a
 * formatter is a bug -- and the profile has to be able to say which.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_DECIMAL_HPP
#define SENSORLAB_DECIMAL_HPP

#include <cstddef>
#include <cstdint>

namespace SensorLab::Profile
{

/// Significant digits kept. Seven: float's own precision is ~7.22 decimal
/// digits, so six would throw information away and eight would invent it.
constexpr int32_t kDecimalDigits = 7;

/// Normalisation window for the mantissa: [10^6, 10^7).
constexpr int32_t kMantissaMin = 1000000;
constexpr int32_t kMantissaMax = 10000000;

/// What a value turned out to be. `Finite` is the ordinary case; the rest are
/// recorded as themselves so a report can distinguish a sensor that produced a
/// NaN from a statistic that could not be computed.
enum class DecimalKind : uint8_t
{
    Finite = 0,
    Zero,
    PosInf,
    NegInf,
    NaN,
};

/// `value = mantissa * 10^exponent`, for `kind == Finite`.
struct Decimal
{
    int32_t     mantissa = 0;
    int8_t      exponent = 0;
    DecimalKind kind     = DecimalKind::Zero;
};

/// The tag written into `profile.json` for a non-finite value.
const char *toString(DecimalKind k);

/**
 * @brief Decompose a float into a mantissa and a decimal exponent.
 *
 * Integer-only on the output side and free of any library call: the only
 * floating-point work is multiply, divide and compare, all of which are
 * hardware instructions on this Cortex-M33's FPU.
 *
 * Exact for every value float can represent, to seven significant digits, and
 * monotonic -- if `a < b` then `decompose(a)` reconstitutes to no more than
 * `decompose(b)` does. That last property is what lets `profile_diff.py`
 * compare two profiles' values without decoding them into floats first.
 */
Decimal decompose(float v);

/// Reconstitute. Present for the host tests, which check the round trip against
/// values with known answers -- and present on the device too, because the
/// roster screen needs a magnitude to draw a bar from.
float recompose(const Decimal &d);

/**
 * @brief Format a decimal as a plain decimal string, integer arithmetic only.
 *
 * Used by the run log's CSV, where a column of `1.2345e-05` is unreadable and a
 * column of mantissa/exponent pairs is unparseable by eye. Never used for
 * `profile.json`, which carries the pair.
 *
 * @return characters written, or 0 when @p out is too small.
 */
constexpr size_t kDecimalStringMax = 24;
size_t format(char *out, size_t outSize, const Decimal &d);

/// Convenience: decompose and format in one call.
size_t format(char *out, size_t outSize, float v);

} // namespace SensorLab::Profile

#endif // SENSORLAB_DECIMAL_HPP
