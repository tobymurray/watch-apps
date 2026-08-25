/**
 ******************************************************************************
 * @file    PinWatch.hpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Which pins are worth watching, and which of them just moved.
 ******************************************************************************
 *
 * The naive version of this app diffs every bit of every port and drowns. Most
 * of those pins are alternate functions -- SPI clocks, I2C lines, a display
 * bus -- toggling far faster than anything can sample them, and a log of their
 * edges is a log of nothing at all.
 *
 * So the pins to watch are not chosen in advance; they are **found**. For half
 * a second at startup, before anybody has touched the watch, every port is
 * sampled as fast as the loop goes round. Any bit seen both high and low in
 * that window is a signal, not a button, and is struck out. What survives is
 * the set of pins that sat still while the watch was busy -- and a button, held
 * at one level by its pull-up until a finger arrives, is exactly a pin that
 * sits still.
 *
 * That is the whole idea, and it is why this app needs no hard-coded pin map
 * and does not care which port the buttons are on.
 *
 * ## What this does not need to be told
 *
 * The recorded 2026-07-29 sweep of this watch happens to narrow it a long way
 * on its own: reading `MODER` and `PUPDR` back, the pins configured as
 * input-with-pull-up are `PA5`, `PD4`, `PD8`, `PD15`, `PG7` and ten on port E.
 * Four of those are the buttons. That list is a **prediction to check the
 * answer against**, not an input to the search -- wiring it in would mean the
 * app could only ever confirm what was already assumed, and would find nothing
 * at all on a watch whose firmware muxed a pin differently.
 *
 * ## Absent ports
 *
 * `GPIOH` read back `FFFFFFFF` in that sweep. A real `IDR` cannot: its top
 * sixteen bits are reserved and read as zero, so *any* value with a high bit
 * set is a port that is not answering rather than sixteen pins that are all
 * high. That is the test used here, and it is exact -- unlike "all ones", which
 * a genuine port with every pin pulled up would also produce.
 *
 ******************************************************************************
 */

#ifndef PINWATCH_HPP
#define PINWATCH_HPP

#include <cstdint>

#include "GpioPorts.hpp"

namespace Probe
{

/// Pins in a GPIO port. The upper half of `IDR` is reserved.
constexpr uint32_t kPinsPerPort = 16;

/// One pin changing level.
struct Transition {
    uint8_t port;   ///< Index into the port table; `portName()` for a letter.
    uint8_t pin;    ///< 0..15.
    uint8_t level;  ///< The level it changed *to*.
};

class PinWatch
{
public:
    /// Fold one snapshot into the calibration. Call repeatedly, for as long as
    /// the calibration window lasts, while nobody is pressing anything.
    void observe(const uint32_t *snapshot);

    /// Close the calibration: every pin that held still becomes a watched pin.
    /// Returns how many pins are being watched, which is worth logging -- zero
    /// means every pin on the watch was busy, which is a broken experiment
    /// rather than an empty result.
    uint32_t settle(const uint32_t *snapshot);

    /// Report watched pins whose level differs from the previous snapshot.
    /// Returns how many transitions were written to `out`, never more than
    /// `outMax`. Does nothing before `settle()`.
    uint32_t update(const uint32_t *snapshot, Transition *out, uint32_t outMax);

    /// The watched pins of one port, as a bitmask. For the log.
    uint32_t maskOf(uint32_t port) const;

    bool settled() const { return mSettled; }

private:
    /// Levels each pin has been seen at during calibration.
    uint32_t mSeenHigh[kPortCount] = {};
    uint32_t mSeenLow[kPortCount]  = {};
    /// Whether a port ever answered with a value a real IDR could hold.
    bool     mPresent[kPortCount]  = {};

    uint32_t mMask[kPortCount] = {};
    uint32_t mLast[kPortCount] = {};

    bool mSettled = false;
};

/// True if `value` could be a reading of a real `GPIOx_IDR`.
bool plausibleIdr(uint32_t value);

} // namespace Probe

#endif // PINWATCH_HPP
