/**
 ******************************************************************************
 * @file    SettingsSplice.hpp
 * @brief   Replaces `phone.notifications` in a settings.json buffer, leaving
 *          every other byte alone.
 ******************************************************************************
 *
 * Header-only and free of SDK types so the host tests in `Tests/` can drive it
 * without a kernel.
 *
 * The key is matched at one exact brace depth: `notifications` among the
 * `phone` object's own keys, with `phone` itself found only at the outermost.
 * A key of either name deeper in is not what the kernel parses, and rewriting
 * it would be confirmed rather than caught -- the readback compares the file
 * against the buffer just written.
 ******************************************************************************
 */

#ifndef SETTINGS_SPLICE_HPP
#define SETTINGS_SPLICE_HPP

#include <cstddef>

namespace SettingsSplice
{

enum class Result {
    Ok,
    FieldNotFound,   ///< No `phone` object, or no boolean `notifications` inside it.
    WouldNotFit,     ///< The one-byte true/false delta would overrun the buffer.
};

namespace detail
{

constexpr const char *kTrue  = "true";
constexpr const char *kFalse = "false";
constexpr size_t kTrueLen  = 4;
constexpr size_t kFalseLen = 5;

inline bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
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
/// Steps over nested objects rather than matching inside them, and over a token
/// that turned out to be a string value: only the colon after it says which one
/// it was.
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

} // namespace detail

/// Rewrites `phone.notifications` in `buf` (`len` bytes, `capacity` available)
/// to `newEnabled`, adjusting `len` for the true/false length delta. `buf` is
/// left untouched unless the result is Ok. `valueOffsetOut`, when given,
/// receives the offset of the boolean that was rewritten -- enough for a log
/// line to show the edit landed in the right place without quoting a file
/// that holds the wearer's height, weight and date of birth.
inline Result setNotifications(char *buf, size_t &len, size_t capacity, bool newEnabled,
                               size_t *valueOffsetOut = nullptr)
{
    const char *const end = buf + len;

    const char *const phone = detail::findObjectAtDepth(buf, end, "phone", 5, 1);
    if (phone == nullptr) {
        return Result::FieldNotFound;
    }
    const char *const phoneEnd = detail::objectEnd(phone, end);
    if (phoneEnd == nullptr) {
        return Result::FieldNotFound;
    }
    const char *const found = detail::findValueAtDepth(phone, phoneEnd, "notifications", 13, 1);
    if (found == nullptr) {
        return Result::FieldNotFound;
    }
    char *const value = buf + (found - buf);

    size_t oldLen = 0;
    if (detail::matches(value, phoneEnd, detail::kTrue, detail::kTrueLen)) {
        oldLen = detail::kTrueLen;
    } else if (detail::matches(value, phoneEnd, detail::kFalse, detail::kFalseLen)) {
        oldLen = detail::kFalseLen;
    } else {
        return Result::FieldNotFound;
    }

    if (valueOffsetOut != nullptr) {
        *valueOffsetOut = static_cast<size_t>(value - buf);
    }

    const char *replacement = newEnabled ? detail::kTrue : detail::kFalse;
    const size_t newLen     = newEnabled ? detail::kTrueLen : detail::kFalseLen;

    const size_t resultLen = len - oldLen + newLen;
    if (resultLen > capacity) {
        return Result::WouldNotFit;
    }

    char *const tail       = value + oldLen;
    const size_t tailBytes = static_cast<size_t>(end - tail);
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

} // namespace SettingsSplice

#endif // SETTINGS_SPLICE_HPP
