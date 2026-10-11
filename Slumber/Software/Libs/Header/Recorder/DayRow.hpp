#ifndef SLUMBER_DAY_ROW_HPP
#define SLUMBER_DAY_ROW_HPP

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "EpochRow.hpp"
#include "SleepPeriods.hpp"

/// Reading `Days/<yyyymmdd>.csv` back: free of the SDK, so the watch's restart
/// and the desk's tools parse a day file with the same code.
namespace Slumber
{
namespace DayRow
{

/// Bytes the widest row formats to, with its newline.
constexpr size_t kRowCap = 320;

template <typename T>
T clampTo(int32_t v, uint32_t max)
{
    return v <= 0 ? T{0} : (static_cast<uint32_t>(v) >= max ? static_cast<T>(max) : static_cast<T>(v));
}

/// The epoch a row describes: the row's `utc` is when it closed, an epoch is
/// keyed by when it opened.
inline SleepPeriods::Epoch toEpoch(int64_t utc, uint32_t count, int32_t steps, int32_t hrN,
                                   int32_t hrTrustX10)
{
    SleepPeriods::Epoch e;
    e.utc        = static_cast<uint32_t>(utc) - SleepPeriods::kEpochS;
    e.count      = count;
    e.steps      = clampTo<uint16_t>(steps, 0xFFFFu);
    e.hasHr      = hrN > 0;
    e.hrAsked    = hrN >= 0;
    e.hrTrustX10 = e.hasHr ? clampTo<uint8_t>(hrTrustX10, 0xFFu) : 0;
    return e;
}

/// Index of `name` among EpochRow's columns, or -1.
inline int column(const char *name)
{
    const size_t len = std::strlen(name);
    int          at  = 0;
    for (const char *p = EpochRow::kHeader; *p != '\0' && *p != '\n'; ++at) {
        const char *end = p + std::strcspn(p, ",\n");
        if (static_cast<size_t>(end - p) == len && std::strncmp(p, name, len) == 0) {
            return at;
        }
        p = *end == ',' ? end + 1 : end;
    }
    return -1;
}

inline int columnCount()
{
    int n = 1;
    for (const char *p = EpochRow::kHeader; *p != '\0'; ++p) {
        n += *p == ',' ? 1 : 0;
    }
    return n;
}

/// Assembles a day file's bytes, from after its header, into epochs; a row cut
/// short by a crash, or one too long to be a row, is skipped.
class Reader
{
public:
    /// True when `c` completed a row, now in `e`.
    bool take(char c, SleepPeriods::Epoch &e)
    {
        if (c != '\n') {
            if (mLen < sizeof(mLine) - 1) {
                mLine[mLen++] = c;
            } else {
                mOverflow = true;
            }
            return false;
        }
        mLine[mLen]      = '\0';
        const bool whole = !mOverflow;
        mLen             = 0;
        mOverflow        = false;
        return whole && parse(e);
    }

private:
    bool parse(SleepPeriods::Epoch &e) const
    {
        int64_t     v[5] = {};
        int         at   = 0;
        const char *p    = mLine;
        for (;; ++at) {
            char         *end = nullptr;
            const int64_t n   = std::strtoll(p, &end, 10);
            if (end == p || (*end != ',' && *end != '\0')) {
                return false;
            }
            for (int k = 0; k < 5; ++k) {
                if (at == mWant[k]) {
                    v[k] = n;
                }
            }
            if (*end == '\0') {
                break;
            }
            p = end + 1;
        }
        if (at + 1 != mColumns || v[0] <= 0) {
            return false;
        }
        e = toEpoch(v[0], static_cast<uint32_t>(v[1]), static_cast<int32_t>(v[2]),
                    static_cast<int32_t>(v[3]), static_cast<int32_t>(v[4]));
        return true;
    }

    const int mWant[5] = {column("utc"), column("count"), column("steps"), column("hr_n"),
                          column("hr_trust_x10")};
    const int mColumns = columnCount();
    char      mLine[kRowCap];
    size_t    mLen      = 0;
    bool      mOverflow = false;
};

} // namespace DayRow
} // namespace Slumber

#endif // SLUMBER_DAY_ROW_HPP
