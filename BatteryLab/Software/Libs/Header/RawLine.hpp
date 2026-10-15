#ifndef RAW_LINE_HPP
#define RAW_LINE_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace BatteryLab
{

constexpr int32_t kNone = INT32_MIN;  ///< not in this message; written as an empty field

enum class Kind : char {
    Level    = 'L',
    Metrics  = 'E',
    Charging = 'C',
    Boot     = 'B',  ///< this process started
    Stop     = 'S',  ///< this process was told to stop
};

/// One battery message as the parser returned it, scaled to integers.
struct Sample {
    uint32_t uptimeMs       = 0;
    uint32_t sensorTsMs     = 0;
    uint32_t utc            = 0;  ///< s; 0 = clock unknown
    int32_t  utcOffMin      = kNone;
    Kind     kind           = Kind::Boot;
    int32_t  fields         = kNone;  ///< getFieldCount(), logged even when isDataValid() is false
    int32_t  pctX1000       = kNone;
    int32_t  mv             = kNone;
    int32_t  maX1000        = kNone;  ///< sign as the firmware gives it
    int32_t  avgMaX1000     = kNone;
    int32_t  mahX1000       = kNone;  ///< remaining
    int32_t  designMahX1000 = kNone;
    int32_t  connected      = kNone;
    int32_t  charging       = kNone;
};

constexpr char kHeader[] =
    "# BatteryLab raw battery log, schema 1: one row per sensor message. Empty = not in that message.\n"
    "uptime_ms,sensor_ts_ms,utc,utc_off_min,kind,fields,pct_x1000,mv,ma_x1000,avg_ma_x1000,"
    "mah_x1000,design_mah_x1000,connected,charging\n";

/// Three 10-digit unsigned fields, ten 11-character signed ones, the kind, 13 commas, '\n' and NUL.
constexpr size_t kLineCap = 3 * 10 + 10 * 11 + 1 + 13 + 1 + 1;

class Appender
{
public:
    Appender(char *out, size_t cap)
        : mOut(out)
        , mCap(cap)
    {}

    void unsignedField(const char *sep, uint32_t v)
    {
        took(std::snprintf(at(), room(), "%s%lu", sep, static_cast<unsigned long>(v)));
    }

    void field(int32_t v)
    {
        if (v == kNone) {
            took(std::snprintf(at(), room(), ","));
        } else {
            took(std::snprintf(at(), room(), ",%ld", static_cast<long>(v)));
        }
    }

    void letter(char c) { took(std::snprintf(at(), room(), ",%c", c)); }

    /// The length with a newline added, or 0 if anything did not fit.
    size_t finish()
    {
        took(std::snprintf(at(), room(), "\n"));
        return mOk ? mLen : 0;
    }

private:
    char  *at() const { return mOut + mLen; }
    size_t room() const { return mOk ? mCap - mLen : 0; }

    void took(int n)
    {
        mOk = mOk && n >= 0 && mLen + static_cast<size_t>(n) < mCap;
        if (mOk) {
            mLen += static_cast<size_t>(n);
        }
    }

    char  *mOut;
    size_t mCap;
    size_t mLen = 0;
    bool   mOk  = true;
};

/// Writes one row and its newline into `out`; returns its length, or 0 if `cap` is too small.
inline size_t format(const Sample &s, char *out, size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    Appender a(out, cap);
    a.unsignedField("", s.uptimeMs);
    a.unsignedField(",", s.sensorTsMs);
    a.unsignedField(",", s.utc);
    a.field(s.utcOffMin);
    a.letter(static_cast<char>(s.kind));
    for (int32_t v : {s.fields, s.pctX1000, s.mv, s.maX1000, s.avgMaX1000, s.mahX1000,
                      s.designMahX1000, s.connected, s.charging}) {
        a.field(v);
    }
    return a.finish();
}

/// Truncated toward zero; `kNone` past what an `int32_t` holds or for NaN.
inline int32_t scaled(float v, float scale)
{
    const float x = v * scale;
    return (x > -2.0e9f && x < 2.0e9f) ? static_cast<int32_t>(x) : kNone;
}

} // namespace BatteryLab

#endif // RAW_LINE_HPP
