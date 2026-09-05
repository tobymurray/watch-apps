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
 * Scoped to the `phone` object rather than matching `"notifications"` anywhere
 * in the file: the flag this app owns is `phone.notifications`, and a settings
 * file that grows a second key of that name elsewhere must not be edited in
 * the wrong place.
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

/// Start of the quoted token `name` at or after `begin`, or null. A match is
/// only a candidate: the same characters appear in a string *value* too, and
/// only the colon after them says which one this was.
inline const char *findQuoted(const char *begin, const char *end, const char *name, size_t nameLen)
{
    for (const char *p = begin; p < end; ++p) {
        if (*p != '"' || !matches(p + 1, end, name, nameLen)) {
            continue;
        }
        if (p + 1 + nameLen < end && p[1 + nameLen] == '"') {
            return p;
        }
    }
    return nullptr;
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

/// Start of the value for key `name` within [begin, end), past the colon and
/// any whitespace, or null. Keeps looking past a token that turned out to be a
/// string value rather than a key -- `{"a":"phone","phone":{...}}` has both,
/// and stopping at the first would miss the real one.
inline const char *findValue(const char *begin, const char *end, const char *name, size_t nameLen)
{
    for (const char *search = begin; search < end;) {
        const char *key = findQuoted(search, end, name, nameLen);
        if (key == nullptr) {
            return nullptr;
        }
        const char *p = key + 1 + nameLen + 1;
        while (p < end && isSpace(*p)) {
            ++p;
        }
        if (p < end && *p == ':') {
            ++p;
            while (p < end && isSpace(*p)) {
                ++p;
            }
            return p < end ? p : nullptr;
        }
        search = key + 1;
    }
    return nullptr;
}

/// Start of the object that is the value of key `name`, pointing at its '{'.
/// A `name` whose value is not an object is not searched past: the file is then
/// not the shape this app knows how to edit.
inline const char *findObject(const char *begin, const char *end, const char *name, size_t nameLen)
{
    const char *value = findValue(begin, end, name, nameLen);
    return (value != nullptr && *value == '{') ? value : nullptr;
}

} // namespace detail

/// Reads `phone.notifications` out of `buf` without changing it. False if the
/// file does not have the shape this app understands.
inline bool readNotifications(const char *buf, size_t len, bool &out)
{
    const char *const end = buf + len;

    const char *const phone = detail::findObject(buf, end, "phone", 5);
    if (phone == nullptr) {
        return false;
    }
    const char *const phoneEnd = detail::objectEnd(phone, end);
    if (phoneEnd == nullptr) {
        return false;
    }
    const char *value = detail::findValue(phone, phoneEnd, "notifications", 13);
    if (value == nullptr) {
        return false;
    }
    if (detail::matches(value, phoneEnd, detail::kTrue, detail::kTrueLen)) {
        out = true;
        return true;
    }
    if (detail::matches(value, phoneEnd, detail::kFalse, detail::kFalseLen)) {
        out = false;
        return true;
    }
    return false;
}

/// Reads a top-level unsigned integer field, for cross-checking a live struct
/// against the file the kernel loaded it from. False if absent or not a plain
/// non-negative integer.
inline bool readUnsigned(const char *buf, size_t len, const char *name, size_t nameLen, uint32_t &out)
{
    const char *const end = buf + len;

    const char *value = detail::findValue(buf, end, name, nameLen);
    if (value == nullptr || value >= end || *value < '0' || *value > '9') {
        return false;
    }

    uint32_t acc = 0;
    while (value < end && *value >= '0' && *value <= '9') {
        const uint32_t digit = static_cast<uint32_t>(*value - '0');
        if (acc > (0xFFFFFFFFu - digit) / 10u) {
            return false;
        }
        acc = acc * 10u + digit;
        ++value;
    }
    out = acc;
    return true;
}

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

    const char *const phone = detail::findObject(buf, end, "phone", 5);
    if (phone == nullptr) {
        return Result::FieldNotFound;
    }
    const char *const phoneEnd = detail::objectEnd(phone, end);
    if (phoneEnd == nullptr) {
        return Result::FieldNotFound;
    }
    const char *const found = detail::findValue(phone, phoneEnd, "notifications", 13);
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
