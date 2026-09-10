/**
 ******************************************************************************
 * @file    SettingsSplice.hpp
 * @brief   Replaces one field in a settings.json buffer, leaving every other
 *          byte alone.
 ******************************************************************************
 *
 * Header-only and free of SDK types so the host tests in `Tests/` can drive it
 * without a kernel.
 *
 * A key is matched at one exact brace depth: a top-level key among the
 * outermost object's own, a nested one among its named parent's own, with the
 * parent found only at the outermost. A key of the same name deeper in is not
 * the one the kernel parses, and rewriting it would be confirmed rather than
 * caught -- the readback compares the file against the buffer just written.
 *
 * The value never reaches this file typed. A caller formats it into a JSON
 * token and this replaces a token of a declared shape with it, so widening the
 * set of editable fields does not widen any signature here.
 ******************************************************************************
 */

#ifndef SETTINGS_SPLICE_HPP
#define SETTINGS_SPLICE_HPP

#include <cstddef>
#include <cstdint>

namespace SettingsSplice
{

enum class Result {
    Ok,
    FieldNotFound,   ///< The key is absent, or its value is not one this knows how to rewrite.
    WouldNotFit,     ///< The length delta between old and new value would overrun the buffer.
};

/// The shape a field's existing value must already have. Refusing an
/// unexpected shape is what keeps this from rewriting a file it does not
/// understand: `setUnits` refused a third spelling long before there was a
/// name for the idea.
enum class JsonShape {
    Boolean,       ///< `true` or `false`.
    UnitsToken,    ///< `"metric"` or `"imperial"`, quotes included.
    Number,        ///< `-?[0-9]+(\.[0-9]+)?`.
    NumberArray6,  ///< `[` then exactly six of Number `]`.
};

/// Which key, and inside which enclosing object. `outer` is null for a key of
/// the outermost object itself.
struct KeyPath {
    const char *outer;
    const char *leaf;
};

namespace detail
{

constexpr const char *kTrue  = "true";
constexpr const char *kFalse = "false";
constexpr size_t kTrueLen  = 4;
constexpr size_t kFalseLen = 5;

/// Quoted, because that is what gets spliced: the quotes move with the token.
constexpr const char *kMetric   = "\"metric\"";
constexpr const char *kImperial = "\"imperial\"";
constexpr size_t kMetricLen   = 8;
constexpr size_t kImperialLen = 10;

/// How many values `JsonShape::NumberArray6` holds.
constexpr size_t kZoneCount = 6;

constexpr size_t constexprStrlen(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

inline bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

inline bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

inline bool matches(const char *p, const char *end, const char *needle, size_t needleLen)
{
    if (static_cast<size_t>(end - p) < needleLen) {
        return false;
    }
    for (size_t i = 0; i < needleLen; ++i) {
        if (p[i] != needle[i]) {
            return false;
        }
    }
    return true;
}

/// True if `p` is where a JSON value is allowed to stop: at a separator, or at
/// whitespace with a separator behind it.
///
/// End of buffer is not: a value that runs to the last byte read is a truncated
/// file, and rewriting one would commit a shorter truncation.
///
/// Nor is whitespace on its own, which is what makes `{"height":1 0}` a refusal
/// rather than a rewrite of the `1`. Both of those leave a token behind that
/// this app did not write and the kernel's own reader would not agree with, and
/// the commit's readback confirms either -- it compares the file against the
/// buffer just written. The same rule is what tells `false` from `falsey`; a
/// prefix match alone rewrote the latter to `truey`.
inline bool valueTerminates(const char *p, const char *end)
{
    while (p < end && isSpace(*p)) {
        ++p;
    }
    return p < end && (*p == ',' || *p == '}' || *p == ']');
}

/// End of the object that opens at `open` (which must point at '{'), one past
/// its closing brace, or null if the braces never balance. Skips braces and
/// escapes inside strings so a value can contain either.
inline const char *objectEnd(const char *open, const char *end)
{
    int depth = 0;
    bool inString = false;
    for (const char *p = open; p < end; ++p) {
        if (inString) {
            if (*p == '\\') {
                ++p;
            } else if (*p == '"') {
                inString = false;
            }
            continue;
        }
        if (*p == '"') {
            inString = true;
        } else if (*p == '{') {
            ++depth;
        } else if (*p == '}') {
            if (--depth == 0) {
                return p + 1;
            }
        }
    }
    return nullptr;
}

/// Start of the value for key `name` at brace depth `wantDepth` -- 1 being the
/// keys of the outermost object -- past the colon and any whitespace, or null.
/// Steps over nested objects rather than matching inside them, and over a
/// token that turned out to be a string value: only the colon after it says
/// which one it was.
inline const char *findValueAtDepth(const char *begin, const char *end, const char *name,
                                    size_t nameLen, int wantDepth)
{
    int depth = 0;
    bool inString = false;
    for (const char *p = begin; p < end; ++p) {
        if (inString) {
            if (*p == '\\') {
                ++p;
            } else if (*p == '"') {
                inString = false;
            }
            continue;
        }
        if (*p == '{' || *p == '[') {
            ++depth;
            continue;
        }
        if (*p == '}' || *p == ']') {
            --depth;
            continue;
        }
        if (*p != '"') {
            continue;
        }

        if (depth == wantDepth && matches(p + 1, end, name, nameLen) &&
            p + 1 + nameLen < end && p[1 + nameLen] == '"') {
            const char *q = p + 1 + nameLen + 1;
            while (q < end && isSpace(*q)) {
                ++q;
            }
            if (q < end && *q == ':') {
                ++q;
                while (q < end && isSpace(*q)) {
                    ++q;
                }
                return q < end ? q : nullptr;
            }
        }
        // Not the key: step over the rest of this string token.
        inString = true;
    }
    return nullptr;
}

/// Start of the object that is the value of key `name` at brace depth
/// `wantDepth`, pointing at its '{'. A `name` whose value is not an object is
/// not searched past: the file is then not the shape this app knows how to edit.
inline const char *findObjectAtDepth(const char *begin, const char *end, const char *name,
                                     size_t nameLen, int wantDepth)
{
    const char *value = findValueAtDepth(begin, end, name, nameLen, wantDepth);
    return (value != nullptr && *value == '{') ? value : nullptr;
}

/// One past a `JsonShape::Number` starting at `p`, or null.
inline const char *numberEnd(const char *p, const char *end)
{
    const char *q = p;
    if (q < end && *q == '-') {
        ++q;
    }
    const char *digits = q;
    while (q < end && isDigit(*q)) {
        ++q;
    }
    if (q == digits) {
        return nullptr;
    }
    if (q < end && *q == '.') {
        ++q;
        const char *fraction = q;
        while (q < end && isDigit(*q)) {
            ++q;
        }
        if (q == fraction) {
            return nullptr;
        }
    }
    return q;
}

/// One past a `JsonShape::NumberArray6` starting at `p`, or null. Exactly six
/// values: five would leave the kernel's sixth byte at whatever it held, and
/// seven is a file written by something this does not know.
inline const char *numberArrayEnd(const char *p, const char *end)
{
    if (p >= end || *p != '[') {
        return nullptr;
    }
    const char *q = p + 1;
    for (size_t i = 0; i < kZoneCount; ++i) {
        while (q < end && isSpace(*q)) {
            ++q;
        }
        q = numberEnd(q, end);
        if (q == nullptr) {
            return nullptr;
        }
        while (q < end && isSpace(*q)) {
            ++q;
        }
        const bool last = (i + 1 == kZoneCount);
        if (q >= end || *q != (last ? ']' : ',')) {
            return nullptr;
        }
        ++q;
    }
    return q;
}

/// The length of the value of shape `shape` at `value`, or 0 if what is there
/// is not that shape. `end` bounds the enclosing object, not the buffer.
inline size_t tokenLength(JsonShape shape, const char *value, const char *end)
{
    const char *stop = nullptr;
    switch (shape) {
        case JsonShape::Boolean:
            if (matches(value, end, kTrue, kTrueLen)) {
                stop = value + kTrueLen;
            } else if (matches(value, end, kFalse, kFalseLen)) {
                stop = value + kFalseLen;
            }
            break;
        case JsonShape::UnitsToken:
            if (matches(value, end, kMetric, kMetricLen)) {
                stop = value + kMetricLen;
            } else if (matches(value, end, kImperial, kImperialLen)) {
                stop = value + kImperialLen;
            }
            break;
        case JsonShape::Number:
            stop = numberEnd(value, end);
            break;
        case JsonShape::NumberArray6:
            stop = numberArrayEnd(value, end);
            break;
    }
    if (stop == nullptr || !valueTerminates(stop, end)) {
        return 0;
    }
    return static_cast<size_t>(stop - value);
}

/// Overwrites the `oldLen` bytes at `value` with `newLen` bytes of
/// `replacement`, shifting the rest of the buffer to suit and adjusting `len`.
/// `buf` is left untouched unless the result is Ok.
inline Result replaceToken(char *buf, size_t &len, size_t capacity, char *value,
                           size_t oldLen, const char *replacement, size_t newLen)
{
    const size_t resultLen = len - oldLen + newLen;
    if (resultLen > capacity) {
        return Result::WouldNotFit;
    }

    char *const tail       = value + oldLen;
    const size_t tailBytes = static_cast<size_t>((buf + len) - tail);
    for (size_t i = 0; i < tailBytes; ++i) {
        const size_t from = newLen > oldLen ? tailBytes - 1 - i : i;
        (value + newLen)[from] = tail[from];
    }
    for (size_t i = 0; i < newLen; ++i) {
        value[i] = replacement[i];
    }

    len = resultLen;
    return Result::Ok;
}

} // namespace detail

/// Longest token any formatter here produces: `[255,255,255,255,255,255]`.
constexpr size_t kMaxTokenBytes = 26;

/// Renders a value as the JSON token that replaces the old one. Each returns
/// the bytes written, or 0 if it would not fit -- so a caller that ignores the
/// length cannot splice a half-written number.
namespace format
{

/// Decimal, no sign, no padding. `snprintf` is not used anywhere on this path:
/// it drags libstdc++'s exception runtime into an `-fno-exceptions` app, which
/// measured 10,036 bytes of `.uapp` on NotifyToggle.
inline size_t unsignedDecimal(char *out, size_t capacity, uint32_t value)
{
    char digits[10];
    size_t n = 0;
    do {
        digits[n++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);

    if (n > capacity) {
        return 0;
    }
    for (size_t i = 0; i < n; ++i) {
        out[i] = digits[n - 1 - i];
    }
    return n;
}

/// The six values the watch's own ladder holds for a maximum of `maxHr`:
/// 50/60/70/80/90/100% of it, rounded half up.
///
/// FIRMWARE: this is the kernel's rule, not one chosen here. Kernel 1.4.0's
/// settings constructor at `0x080abbb4` stores the literal
/// `[95,114,133,152,171,190]` into the six bytes at `structBase+0x10`, which is
/// exactly those percentages of 190. `SettingsZoneLadder.MatchesTheFirmwareDefault`
/// is that claim as a test; a firmware whose default ladder is a different set
/// of percentages falsifies it.
inline size_t zoneLadder(char *out, size_t capacity, uint8_t maxHr)
{
    // Built aside and copied, so `out` is left as it was unless the whole
    // ladder fits -- the same contract every splice here has, and the reason a
    // caller that ignores the length cannot splice half a ladder.
    char work[kMaxTokenBytes];
    const char *const closers = ",,,,,]";
    size_t n = 0;
    work[n++] = '[';
    for (size_t i = 0; i < detail::kZoneCount; ++i) {
        const uint32_t percent = 50u + 10u * static_cast<uint32_t>(i);
        const uint32_t value   = (static_cast<uint32_t>(maxHr) * percent + 50u) / 100u;
        const size_t wrote     = unsignedDecimal(work + n, sizeof(work) - n - 1, value);
        if (wrote == 0) {
            return 0;
        }
        n += wrote;
        work[n++] = closers[i];
    }
    if (n > capacity) {
        return 0;
    }
    for (size_t i = 0; i < n; ++i) {
        out[i] = work[i];
    }
    return n;
}

} // namespace format

/// Rewrites `key`'s value in `buf` (`len` bytes, `capacity` available) with
/// `newToken`, provided what is there now is `shape`. `buf` is left untouched
/// unless the result is Ok. `valueOffsetOut`, when given, receives the offset
/// of the token that was rewritten -- enough for a log line to show the edit
/// landed in the right place without quoting a file that holds the wearer's
/// height, weight and date of birth.
inline Result replaceValue(char *buf, size_t &len, size_t capacity, const KeyPath &key,
                           JsonShape shape, const char *newToken, size_t newTokenLen,
                           size_t *valueOffsetOut = nullptr)
{
    if (buf == nullptr || key.leaf == nullptr || newToken == nullptr || newTokenLen == 0) {
        return Result::FieldNotFound;
    }

    const char *const bufEnd = buf + len;
    const char *searchBegin  = buf;
    const char *searchEnd    = bufEnd;

    if (key.outer != nullptr) {
        const char *const outer = detail::findObjectAtDepth(
            buf, bufEnd, key.outer, detail::constexprStrlen(key.outer), 1);
        if (outer == nullptr) {
            return Result::FieldNotFound;
        }
        const char *const outerEnd = detail::objectEnd(outer, bufEnd);
        if (outerEnd == nullptr) {
            return Result::FieldNotFound;
        }
        searchBegin = outer;
        searchEnd   = outerEnd;
    }

    const char *const found = detail::findValueAtDepth(
        searchBegin, searchEnd, key.leaf, detail::constexprStrlen(key.leaf), 1);
    if (found == nullptr) {
        return Result::FieldNotFound;
    }

    const size_t oldLen = detail::tokenLength(shape, found, searchEnd);
    if (oldLen == 0) {
        return Result::FieldNotFound;
    }

    char *const value = buf + (found - buf);
    if (valueOffsetOut != nullptr) {
        *valueOffsetOut = static_cast<size_t>(value - buf);
    }
    return detail::replaceToken(buf, len, capacity, value, oldLen, newToken, newTokenLen);
}

/// Rewrites `phone.notifications` to `newEnabled`, adjusting `len` for the
/// true/false length delta.
inline Result setNotifications(char *buf, size_t &len, size_t capacity, bool newEnabled,
                               size_t *valueOffsetOut = nullptr)
{
    const char *const token = newEnabled ? detail::kTrue : detail::kFalse;
    const size_t tokenLen   = newEnabled ? detail::kTrueLen : detail::kFalseLen;
    return replaceValue(buf, len, capacity, KeyPath{"phone", "notifications"},
                        JsonShape::Boolean, token, tokenLen, valueOffsetOut);
}

/// Rewrites the top-level `units` string to `"imperial"` or `"metric"`,
/// adjusting `len` for the two-byte length delta.
///
/// A `units` value that is neither of those two tokens is `FieldNotFound`: the
/// firmware's parser recognises exactly these two spellings, and a file
/// carrying a third is one this does not understand well enough to edit.
inline Result setUnits(char *buf, size_t &len, size_t capacity, bool imperial,
                       size_t *valueOffsetOut = nullptr)
{
    const char *const token = imperial ? detail::kImperial : detail::kMetric;
    const size_t tokenLen   = imperial ? detail::kImperialLen : detail::kMetricLen;
    return replaceValue(buf, len, capacity, KeyPath{nullptr, "units"}, JsonShape::UnitsToken,
                        token, tokenLen, valueOffsetOut);
}

/// Rewrites `key`'s number to `value`. A fractional value already there is
/// replaced by a whole one: the parser reads `weight` with `strtod`, so a
/// wearer's phone may have written `90.5`, and refusing that token would make
/// the field uneditable rather than safe.
inline Result setUnsigned(char *buf, size_t &len, size_t capacity, const KeyPath &key,
                          uint32_t value, size_t *valueOffsetOut = nullptr)
{
    char token[kMaxTokenBytes];
    const size_t tokenLen = format::unsignedDecimal(token, sizeof(token), value);
    if (tokenLen == 0) {
        return Result::FieldNotFound;
    }
    return replaceValue(buf, len, capacity, key, JsonShape::Number, token, tokenLen,
                        valueOffsetOut);
}

/// Rewrites the whole top-level `heartRateZones` array to the ladder the watch
/// itself would hold for a maximum of `maxHr`.
///
/// The array is replaced whole because the six values are one setting: five
/// correct floors and one stale maximum is not a state any wearer chose, and a
/// per-element splice can leave exactly that behind between two commits.
inline Result setZoneLadder(char *buf, size_t &len, size_t capacity, uint8_t maxHr,
                            size_t *valueOffsetOut = nullptr)
{
    char token[kMaxTokenBytes];
    const size_t tokenLen = format::zoneLadder(token, sizeof(token), maxHr);
    if (tokenLen == 0) {
        return Result::FieldNotFound;
    }
    return replaceValue(buf, len, capacity, KeyPath{nullptr, "heartRateZones"},
                        JsonShape::NumberArray6, token, tokenLen, valueOffsetOut);
}

} // namespace SettingsSplice

#endif // SETTINGS_SPLICE_HPP
