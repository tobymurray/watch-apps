#ifndef SLUMBER_EPOCH_ROW_HPP
#define SLUMBER_EPOCH_ROW_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace Slumber
{

/// One 30 s row of `Days/<yyyymmdd>.csv`; -1 means not measured, never 0.
///
/// `Slumber/README.md` says which question each column answers.
struct EpochRow {
    static constexpr int kSchema = 2;

    uint32_t uptimeMs   = 0;
    int64_t  utc        = -1;
    int32_t  utcOffMin  = -1;  ///< local minus UTC; -1 also when the clock is unknown
    uint32_t spanMs     = 0;
    uint32_t lateMs     = 0;  ///< how far past its slot the loop woke
    uint32_t slotsLost  = 0;

    uint16_t accSamples  = 0;
    uint16_t accBatches  = 0;
    int32_t  accMaxGapMs = -1;
    uint32_t count       = 0;
    uint32_t countAxis[3] = {0, 0, 0};
    uint32_t peak        = 0;

    /// m/s^2 x1000, not mg. Measured: a watch at rest on a table read a
    /// magnitude of about 9,900 here; one near 1,000 would mean the sensor is in g.
    int32_t  gravX1000[3] = {-1, -1, -1};  ///< -1 with no samples
    uint16_t turnSumDegX10 = 0;
    uint16_t turnMaxDegX10 = 0;
    uint8_t  tiltBlocks    = 0;

    int32_t  steps       = -1;
    uint16_t motion      = 0;
    uint16_t sigMotion   = 0;

    int32_t  hrMeanX10  = -1;
    int32_t  hrMinX10   = -1;
    int32_t  hrMaxX10   = -1;
    int32_t  hrN        = -1;  ///< readings; -1 = HEART_RATE not connected, 0 = connected and silent
    int32_t  hrTrustX10 = -1;

    int32_t  tempX10 = -1;  ///< degrees C x10; -1 also for a real -0.1
    uint16_t tempN   = 0;

    int32_t battPctX10   = -1;
    int32_t battMv       = -1;
    int32_t battMaX10    = -1;
    int32_t battAvgMaX10 = -1;
    int32_t battMah      = -1;
    int32_t charging     = -1;

    uint16_t wakes      = 0;
    uint16_t msgs       = 0;
    int32_t  mark       = -1;  ///< code of a mark made in this epoch
    uint32_t writeFails = 0;   ///< since the service started

    static constexpr const char *kHeader =
        "uptime_ms,utc,utc_off_min,span_ms,late_ms,slots_lost,"
        "acc_samples,acc_batches,acc_max_gap_ms,count,count_x,count_y,count_z,peak,"
        "grav_x,grav_y,grav_z,turn_sum_dx10,turn_max_dx10,tilt_blocks,"
        "steps,motion,sig_motion,"
        "hr_mean_x10,hr_min_x10,hr_max_x10,hr_n,hr_trust_x10,"
        "temp_x10,temp_n,"
        "batt_pct_x10,batt_mv,batt_ma_x10,batt_avg_ma_x10,batt_mah,charging,"
        "wakes,msgs,mark,write_fails\n";

    /// Bytes written, or 0 if `cap` was too small for the whole row.
    size_t format(char *out, size_t cap) const
    {
        const int n = std::snprintf(
            out, cap,
            "%lu,%ld,%ld,%lu,%lu,%lu,"
            "%u,%u,%ld,%lu,%lu,%lu,%lu,%lu,"
            "%ld,%ld,%ld,%u,%u,%u,"
            "%ld,%u,%u,"
            "%ld,%ld,%ld,%ld,%ld,"
            "%ld,%u,"
            "%ld,%ld,%ld,%ld,%ld,%ld,"
            "%u,%u,%ld,%lu\n",
            ul(uptimeMs), static_cast<long>(utc), l(utcOffMin), ul(spanMs), ul(lateMs),
            ul(slotsLost),
            u(accSamples), u(accBatches), l(accMaxGapMs), ul(count), ul(countAxis[0]),
            ul(countAxis[1]), ul(countAxis[2]), ul(peak),
            l(gravX1000[0]), l(gravX1000[1]), l(gravX1000[2]), u(turnSumDegX10), u(turnMaxDegX10),
            u(tiltBlocks),
            l(steps), u(motion), u(sigMotion),
            l(hrMeanX10), l(hrMinX10), l(hrMaxX10), l(hrN), l(hrTrustX10),
            l(tempX10), u(tempN),
            l(battPctX10), l(battMv), l(battMaX10), l(battAvgMaX10), l(battMah), l(charging),
            u(wakes), u(msgs), l(mark), ul(writeFails));
        return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
    }

private:
    static unsigned long ul(uint32_t v) { return static_cast<unsigned long>(v); }
    static long          l(int32_t v) { return static_cast<long>(v); }
    static unsigned      u(uint32_t v) { return static_cast<unsigned>(v); }
};

} // namespace Slumber

#endif // SLUMBER_EPOCH_ROW_HPP
