#ifndef SLUMBER_LOCAL_TIME_HPP
#define SLUMBER_LOCAL_TIME_HPP

#include <cstdint>
#include <cstdio>

namespace Slumber
{

/// Calendar arithmetic without `timegm`, which the watch's newlib lacks.
namespace LocalTime
{

/// Days from 1970-01-01 to a proleptic Gregorian date (Hinnant's algorithm).
inline int32_t daysFromCivil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2) ? 1 : 0;
    const int      era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m > 2 ? m - 3 : m + 9) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return static_cast<int32_t>(era) * 146097 + static_cast<int32_t>(doe) - 719468;
}

inline void civilFromDays(int32_t z, int &y, unsigned &m, unsigned &d)
{
    z += 719468;
    const int32_t  era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp  = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
}

/// Local minus UTC, in minutes, from the fields `localtime_r` gave for `utc`.
inline int32_t offsetMinutes(int64_t utc, int year, unsigned month, unsigned day,
                             unsigned hour, unsigned minute, unsigned second)
{
    const int64_t local = static_cast<int64_t>(daysFromCivil(year, month, day)) * 86400 +
                          hour * 3600 + minute * 60 + second;
    return static_cast<int32_t>((local - utc) / 60);
}

/// `yyyymmdd` of the local day `utc` falls in; `out` holds at least 9 bytes.
inline void dayStem(int64_t utc, int32_t offsetMin, char *out)
{
    const int64_t local = utc + static_cast<int64_t>(offsetMin) * 60;
    int64_t       days  = local / 86400;
    if (local < 0 && local % 86400 != 0) {
        --days;
    }
    int      y = 0;
    unsigned m = 0, d = 0;
    civilFromDays(static_cast<int32_t>(days), y, m, d);
    std::snprintf(out, 9, "%04u%02u%02u", static_cast<unsigned>(y) % 10000u, m % 100u, d % 100u);
}

/// Minutes past local midnight at `utc`.
inline uint16_t minuteOfDay(int64_t utc, int32_t offsetMin)
{
    int64_t s = (utc + static_cast<int64_t>(offsetMin) * 60) % 86400;
    if (s < 0) {
        s += 86400;
    }
    return static_cast<uint16_t>(s / 60);
}

} // namespace LocalTime
} // namespace Slumber

#endif // SLUMBER_LOCAL_TIME_HPP
