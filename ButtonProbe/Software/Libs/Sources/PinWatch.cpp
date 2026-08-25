/**
 ******************************************************************************
 * @file    PinWatch.cpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Which pins are worth watching, and which of them just moved.
 ******************************************************************************
 */

#include "PinWatch.hpp"

namespace Probe
{

namespace
{

constexpr uint32_t kPinMask = 0x0000FFFFu;

} // namespace

bool plausibleIdr(uint32_t value)
{
    // The top sixteen bits of GPIOx_IDR are reserved and read as zero. A value
    // with any of them set did not come from a port that is answering.
    return (value & ~kPinMask) == 0u;
}

void PinWatch::observe(const uint32_t *snapshot)
{
    if (snapshot == nullptr || mSettled) {
        return;
    }

    for (uint32_t port = 0; port < kPortCount; ++port) {
        const uint32_t value = snapshot[port];
        if (!plausibleIdr(value)) {
            continue;
        }

        mPresent[port] = true;
        mSeenHigh[port] |= value & kPinMask;
        mSeenLow[port]  |= (~value) & kPinMask;
    }
}

uint32_t PinWatch::settle(const uint32_t *snapshot)
{
    if (snapshot == nullptr) {
        return 0;
    }

    observe(snapshot);

    uint32_t watched = 0;

    for (uint32_t port = 0; port < kPortCount; ++port) {
        if (!mPresent[port]) {
            mMask[port] = 0;
            mLast[port] = snapshot[port];
            continue;
        }

        // Seen at both levels means it moved on its own, which means it is a
        // signal rather than a button. Seen at neither means the port never
        // answered, and mPresent has already excluded that.
        const uint32_t moved = mSeenHigh[port] & mSeenLow[port];
        mMask[port] = (~moved) & kPinMask;
        mLast[port] = snapshot[port];

        for (uint32_t pin = 0; pin < kPinsPerPort; ++pin) {
            if ((mMask[port] >> pin) & 1u) {
                ++watched;
            }
        }
    }

    mSettled = true;
    return watched;
}

uint32_t PinWatch::update(const uint32_t *snapshot, Transition *out, uint32_t outMax)
{
    if (!mSettled || snapshot == nullptr || out == nullptr) {
        return 0;
    }

    uint32_t written = 0;

    for (uint32_t port = 0; port < kPortCount; ++port) {
        const uint32_t value = snapshot[port];

        // A port that stops answering mid-run is not sixteen simultaneous
        // transitions. Its previous reading is kept so that if it comes back,
        // the comparison is against the last thing it actually said.
        if (!plausibleIdr(value)) {
            continue;
        }

        const uint32_t changed = (value ^ mLast[port]) & mMask[port];
        mLast[port] = value;

        if (changed == 0u) {
            continue;
        }

        for (uint32_t pin = 0; pin < kPinsPerPort; ++pin) {
            if (((changed >> pin) & 1u) == 0u) {
                continue;
            }
            if (written >= outMax) {
                // The caller's buffer is full. This port's `mLast` has already
                // been advanced, so its remaining bits are lost rather than
                // reported late; ports not yet reached keep their old `mLast`
                // and will report on the next call. Neither is a good outcome,
                // which is why the caller sizes the buffer for every pin on
                // every port and this branch is unreachable in this app.
                return written;
            }
            out[written].port  = static_cast<uint8_t>(port);
            out[written].pin   = static_cast<uint8_t>(pin);
            out[written].level = static_cast<uint8_t>((value >> pin) & 1u);
            ++written;
        }
    }

    return written;
}

uint32_t PinWatch::maskOf(uint32_t port) const
{
    if (port >= kPortCount) {
        return 0;
    }
    return mMask[port];
}

} // namespace Probe
