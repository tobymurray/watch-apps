/**
 ******************************************************************************
 * @file    Fix.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Decimal degrees, parsed without trusting the C library.
 ******************************************************************************
 *
 * Hand-rolled rather than `strtod`, for three reasons that all cost a wrong
 * sunrise rather than a crash: `strtod` accepts `1e2`, `inf` and `nan`, it
 * accepts leading whitespace and a trailing tail it simply stops at, and its
 * decimal separator is a property of the C locale rather than of the format.
 * The grammar wanted here is a dozen lines and has none of those doors in it.
 *
 ******************************************************************************
 */

#include "Fix.hpp"

namespace Sun
{

namespace
{

/// Longest value worth reading. A degree with seven decimals is 11 millimetres
/// of precision; anything longer is not a coordinate, it is a mistake or an
/// attempt to see what happens.
constexpr int kMaxChars = 16;

} // namespace

bool parseDegrees(const char *text, double limit, double &out)
{
    if (text == nullptr || *text == '\0') {
        return false;
    }

    int  i        = 0;
    bool negative = false;

    if (text[0] == '+' || text[0] == '-') {
        negative = (text[0] == '-');
        i        = 1;
    }

    double value  = 0.0;
    int    digits = 0;

    for (; text[i] >= '0' && text[i] <= '9'; i++) {
        value = value * 10.0 + static_cast<double>(text[i] - '0');
        digits++;
        if (i >= kMaxChars) {
            return false;
        }
    }

    if (text[i] == '.') {
        i++;
        double scale = 0.1;
        for (; text[i] >= '0' && text[i] <= '9'; i++) {
            value += static_cast<double>(text[i] - '0') * scale;
            scale *= 0.1;
            digits++;
            if (i >= kMaxChars) {
                return false;
            }
        }
    }

    // No digits at all ("-", "."), or anything left over ("45N", "45 ", "1e2",
    // "45,4"): not a number this app will act on.
    if (digits == 0 || text[i] != '\0') {
        return false;
    }

    if (value > limit) {
        return false;
    }

    out = negative ? -value : value;
    return true;
}

} // namespace Sun
