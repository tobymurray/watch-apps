/**
 ******************************************************************************
 * @file    GpioPorts.cpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Every GPIO port's input register, read and nothing else.
 ******************************************************************************
 */

#include "GpioPorts.hpp"

namespace Probe
{

namespace
{

/// Copied from FwDump's DeviceContext.cpp sweep table; see GpioPorts.hpp for
/// the provenance and for why nothing beyond GPIOH is listed.
constexpr uint32_t kPortBase[kPortCount] = {
    0x42020000u, // GPIOA
    0x42020400u, // GPIOB
    0x42020800u, // GPIOC
    0x42020C00u, // GPIOD
    0x42021000u, // GPIOE
    0x42021400u, // GPIOF
    0x42021800u, // GPIOG
    0x42021C00u, // GPIOH -- read back all ones on this unit in 2026-07-29
};

/// Input data register, within the twelve words FwDump's sweep already reads.
constexpr uint32_t kIdrOffset = 0x10u;

} // namespace

#if defined(SIMULATOR) || !defined(__ARM_ARCH)

const bool kHasRegisters = false;

bool readIdrs(uint32_t *)
{
    return false;
}

#else

const bool kHasRegisters = true;

bool readIdrs(uint32_t *out)
{
    if (out == nullptr) {
        return false;
    }

    for (uint32_t port = 0; port < kPortCount; ++port) {
        const uint32_t address = kPortBase[port] + kIdrOffset;
        out[port] = *reinterpret_cast<const volatile uint32_t *>(
            static_cast<uintptr_t>(address));
    }

    return true;
}

#endif

char portName(uint32_t port)
{
    if (port >= kPortCount) {
        return '?';
    }
    return static_cast<char>('A' + port);
}

uint32_t portBase(uint32_t port)
{
    if (port >= kPortCount) {
        return 0;
    }
    return kPortBase[port];
}

} // namespace Probe
