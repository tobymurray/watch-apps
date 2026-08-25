/**
 ******************************************************************************
 * @file    GpioPorts.hpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Every GPIO port's input register, read and nothing else.
 ******************************************************************************
 *
 * The one place this app touches hardware. It reads eight 32-bit words --
 * `GPIOx_IDR`, the input data register of each port -- and returns them. There
 * is no write anywhere in this file, this app, or any header it includes, and
 * that is not a stylistic preference:
 *
 *   **R1 is on the power line.** The SDK's own button table names SW2 as
 *   `PWR_ON_1V8_L`. Reading `IDR` is inert. Writing that port's `MODER`,
 *   `OTYPER`, `PUPDR` or `BSRR` is not, and getting it wrong is a good deal
 *   worse than a button that stops working.
 *
 * So: `IDR` only, and `const volatile` so the compiler cannot fold, reorder or
 * elide reads whose whole purpose is to happen.
 *
 * ## Where the addresses come from
 *
 * Not from memory, and not from a datasheet read once. Every base below is
 * copied from [`FwDump`](../../../../FwDump)'s `DeviceContext.cpp` sweep table,
 * where each one was **read successfully on this exact watch** by the
 * 2026-07-29 hardware-config investigation and corroborated against RM0456.
 * `IDR` sits at offset `0x10`, inside the twelve words that sweep already
 * reads, so this file reads nothing that unit has not already survived.
 *
 * The recorded sweep also says what to expect. `GPIOH` reads back all ones --
 * absent or unclocked -- and this app treats an all-ones port as absent rather
 * than as sixteen pins that just changed. `GPIOI` is not in that table at all
 * and so is not here: adding it means reading an address nothing has confirmed,
 * which is the one thing this file is careful not to do. If no button turns up
 * on A-H, that is the next thing to try, deliberately and on its own.
 *
 * ## Why it is safe to read a port whose clock may be off
 *
 * Because the recorded sweep read all eight and the watch is still here. An
 * unclocked port reads as ones on this part rather than faulting, which is
 * exactly what `GPIOH` did.
 *
 ******************************************************************************
 */

#ifndef GPIOPORTS_HPP
#define GPIOPORTS_HPP

#include <cstdint>

namespace Probe
{

/// GPIOA..GPIOH. Ports are referred to by index everywhere else in this app;
/// `portName()` is the only thing that turns one back into a letter.
constexpr uint32_t kPortCount = 8;

/// True on a build with real registers behind these addresses. False on a host
/// or simulator build, where `readIdrs()` reports nothing rather than inventing
/// values -- "not measured" and "measured as zero" are opposite conclusions.
extern const bool kHasRegisters;

/// 'A'..'H' for a port index; '?' out of range.
char portName(uint32_t port);

/// The base address of a port, for the log. Zero out of range.
uint32_t portBase(uint32_t port);

/// Read every port's `GPIOx_IDR` into `out`, which must hold `kPortCount`
/// words. False on a build with no registers, leaving `out` untouched.
///
/// The eight reads are deliberately unspaced and unsynchronised: this is a
/// sampler, and the closer together the eight words are taken, the closer they
/// are to being one observation of one instant.
bool readIdrs(uint32_t *out);

} // namespace Probe

#endif // GPIOPORTS_HPP
