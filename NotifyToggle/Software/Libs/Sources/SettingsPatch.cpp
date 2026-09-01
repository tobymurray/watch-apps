#include "SettingsPatch.hpp"

#include <cstring>

namespace SettingsPatch
{

namespace
{

constexpr char kPatternTrue[]  = "\"phone\":{\"notifications\":true}";
constexpr char kPatternFalse[] = "\"phone\":{\"notifications\":false}";
constexpr size_t kPatternTrueLen  = sizeof(kPatternTrue) - 1;
constexpr size_t kPatternFalseLen = sizeof(kPatternFalse) - 1;

/// Counts every non-overlapping occurrence of `needle` in `hay`, and reports
/// the start of the first one. Scans the whole buffer rather than stopping at
/// the first hit, so a caller can require "exactly one" instead of "at least
/// one" -- the difference matters here, because two occurrences would mean
/// this code's understanding of the file is wrong in some way it hasn't
/// noticed, and guessing which one is real is exactly what this module
/// refuses to do.
size_t countOccurrences(const char *hay, size_t hayLen, const char *needle, size_t needleLen,
                         size_t &firstIndex)
{
    size_t count = 0;
    if (needleLen == 0 || needleLen > hayLen) {
        return 0;
    }
    for (size_t i = 0; i + needleLen <= hayLen; ++i) {
        if (std::memcmp(hay + i, needle, needleLen) == 0) {
            if (count == 0) {
                firstIndex = i;
            }
            ++count;
        }
    }
    return count;
}

/// Locates the single notifications literal, whichever value it currently
/// holds. Fails (returns false) unless exactly one of the two patterns is
/// present, and the other is absent -- both present, or either present more
/// than once, means the file isn't shaped the way this code expects.
bool locate(const char *in, size_t inLen, bool &currentEnabled, size_t &matchIndex, size_t &matchLen)
{
    size_t trueIdx = 0, falseIdx = 0;
    const size_t trueCount  = countOccurrences(in, inLen, kPatternTrue, kPatternTrueLen, trueIdx);
    const size_t falseCount = countOccurrences(in, inLen, kPatternFalse, kPatternFalseLen, falseIdx);

    if (trueCount == 1 && falseCount == 0) {
        currentEnabled = true;
        matchIndex = trueIdx;
        matchLen = kPatternTrueLen;
        return true;
    }
    if (falseCount == 1 && trueCount == 0) {
        currentEnabled = false;
        matchIndex = falseIdx;
        matchLen = kPatternFalseLen;
        return true;
    }
    return false;
}

} // namespace

Result readNotificationsFlag(const char *in, size_t inLen, bool &outEnabled)
{
    bool enabled = false;
    size_t matchIndex = 0, matchLen = 0;
    if (!locate(in, inLen, enabled, matchIndex, matchLen)) {
        return Result::NotFound;
    }
    outEnabled = enabled;
    return Result::Ok;
}

Result spliceNotificationsFlag(const char *in, size_t inLen, bool newEnabled,
                                char *out, size_t outCap, size_t &outLen)
{
    bool currentEnabled = false;
    size_t matchIndex = 0, matchLen = 0;
    if (!locate(in, inLen, currentEnabled, matchIndex, matchLen)) {
        return Result::NotFound;
    }

    if (currentEnabled == newEnabled) {
        return Result::AlreadySet;
    }

    const char *replacement = newEnabled ? kPatternTrue : kPatternFalse;
    const size_t replacementLen = newEnabled ? kPatternTrueLen : kPatternFalseLen;

    const size_t tailLen = inLen - (matchIndex + matchLen);
    const size_t newLen = matchIndex + replacementLen + tailLen;
    if (newLen > outCap) {
        return Result::OutputTooSmall;
    }

    // Three plain copies -- the prefix, the replacement literal, and the
    // suffix -- so every byte outside the one literal is carried over from
    // `in` unchanged. Written into `out` only after every check above has
    // passed, so a failure never leaves a partial result in `out`.
    std::memcpy(out, in, matchIndex);
    std::memcpy(out + matchIndex, replacement, replacementLen);
    std::memcpy(out + matchIndex + replacementLen, in + matchIndex + matchLen, tailLen);
    outLen = newLen;
    return Result::Ok;
}

} // namespace SettingsPatch
