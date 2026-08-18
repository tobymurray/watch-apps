/**
 ******************************************************************************
 * @file    RawRecorder.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Optional raw accelerometer capture, for developing sample-level work.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * Epochs always, raw never by default
 *
 * The arithmetic decides this, and it is worth writing down because it is the
 * whole argument:
 *
 *   epochs   30 s epochs -> 960 a night, ~48 bytes a row -> **~46 KB a night**,
 *            ~17 MB a decade. Free.
 *   raw      25 Hz -> ~1.1 KiB/s, scaling Squash's measured ~4.3 KiB/s at
 *            100 Hz -> **~31 MB for eight hours**. A week of that is a quarter
 *            of a gigabyte.
 *
 * So epochs are always recorded and raw is off unless asked for. It exists
 * because sample-level work -- a different count derivation, a peak detector,
 * anything that wants what the epoch pipeline threw away -- cannot be developed
 * without sample-level data, and there is no wrist accelerometer corpus of
 * sleep to borrow. Same argument, and the same ordering, as `Squash`: collect
 * before believing.
 *
 * ---------------------------------------------------------------------------
 * Two caps, and a row is only written if its worst case still fits
 *
 * Bytes and minutes, both from settings. Checked *before* the row is written
 * and against the row's worst-case length, so the file never crosses its
 * budget mid-row and never leaves a half row for a parser to match the wrong
 * half of.
 *
 * The caps are self-defence, not device awareness: **the SDK exposes no
 * free-space query**, so headroom on the user volume is unconfirmed. Squash
 * carries the same limitation and the same note.
 *
 * ---------------------------------------------------------------------------
 * The format
 *
 *   t_ms,ax_ug,ay_ug,az_ug
 *   0,-12207,4272,998047
 *
 * `t_ms` is relative to the first recorded sample and comes from the sensor's
 * own timestamps, so the cadence in the file is the sensor's rather than the
 * message loop's -- which is the only way it can be used to measure the
 * delivered rate it was recorded at.
 *
 * Acceleration is in **microgravities, as integers**. Not floats: the MCU's
 * newlib may not link `%f`, and a recorder that silently writes empty fields
 * for its own samples is worse than one that scales -- the same call BeatProbe
 * and Squash's recorder both made. At 1 ug the file is finer than the BMI270's
 * ~244 ug per LSB, so nothing is lost.
 *
 * Not raw LSB, unlike Squash. Squash records LSB because its IMU saturates
 * during real strokes and the clipping is signal; nothing in a sleeping wrist
 * comes near +/-8 g, so there is nothing to preserve.
 *
 ******************************************************************************
 */

#ifndef RAWRECORDER_HPP
#define RAWRECORDER_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

namespace SleepLab
{

/// Where raw captures land: alongside the nights, not inside `Nights/`.
///
/// They are research inputs rather than records of a night, and a directory
/// listing of `Nights/` should show nights. Same separation Squash makes
/// between `Imu/` and `Activity/`.
constexpr char kRawDir[] = "Raw";

/**
 * @brief Streams accelerometer samples to CSV, under two hard caps.
 */
class RawRecorder
{
public:
    /// Longest a row can be: `t_ms` up to ten digits, three signed
    /// microgravity values up to eight digits each, three separators and a
    /// newline. 48 rounds that up with room to spare.
    static constexpr size_t kRowWorstCase = 48;

    /// Rows buffered before a write. One `write()` per sample at 25 Hz would be
    /// 25 filesystem calls a second for eight hours; a buffer this size turns
    /// that into roughly one every five seconds.
    ///
    /// The cost of buffering is that a kill loses up to this many rows, which
    /// for a research capture is an acceptable trade -- unlike the epoch log,
    /// where a lost row is a lost minute of the record and the handle is
    /// therefore closed every time.
    static constexpr size_t kBufferRows = 128;

    explicit RawRecorder(const SDK::Kernel &kernel);
    ~RawRecorder();

    /**
     * @brief Open a capture for the night starting at @p startUtc.
     *
     * @param maxMb   Byte cap. @param maxMin Duration cap.
     * @retval true   The file exists and carries its header.
     */
    bool start(int64_t startUtc, uint16_t maxMb, uint16_t maxMin);

    /// Whether a capture is running and has not hit a cap.
    bool isRecording() const { return mOpen; }

    /**
     * @brief Record one sample.
     *
     * @param timestampMs Sensor's own timestamp.
     * @param x,y,z       Acceleration in g.
     */
    void add(uint32_t timestampMs, float x, float y, float z);

    /// Flush and close. Safe to call when not recording.
    ///
    /// Called on a discarded night as well as a completed one: throwing away
    /// the session does not make the samples less real.
    void stop();

    uint64_t bytesWritten() const { return mBytes; }
    /// Why recording stopped, for the log and the screen. Never null.
    const char *state() const;

private:
    bool flushBuffer();

    const SDK::Kernel &mKernel;

    char     mPath[80] = {};
    bool     mOpen     = false;
    bool     mHitCap   = false;

    uint64_t mBytes    = 0;
    uint64_t mMaxBytes = 0;
    uint32_t mMaxMs    = 0;

    bool     mHaveFirstTs = false;
    uint32_t mFirstTsMs   = 0;

    char   mBuf[kBufferRows * kRowWorstCase] = {};
    size_t mBufLen = 0;
};

} // namespace SleepLab

#endif // RAWRECORDER_HPP
