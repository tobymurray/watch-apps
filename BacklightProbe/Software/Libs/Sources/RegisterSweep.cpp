/**
 ******************************************************************************
 * @file    RegisterSweep.cpp
 * @brief   The bases, and the loop that reads them.
 ******************************************************************************
 */

#include "RegisterSweep.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"

#define LOG_MODULE_PRX      "Sweep"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{

// SIMULATOR is what the SDK's own simulator sources switch on; the service half
// of a simulator build is compiled for the host too, so keying off the
// architecture as well keeps this a stub anywhere that is not a Cortex-M.
#if defined(SIMULATOR) || !defined(__ARM_ARCH)
constexpr bool kHasRegisters = false;
#else
constexpr bool kHasRegisters = true;

/// Read through volatile so the compiler cannot fold, reorder or elide these:
/// they are reads whose whole purpose is to happen, at a moment chosen by the
/// caller, and a compiler that hoisted one out of the dark/lit pair would
/// silently produce two identical sweeps and a wrong conclusion.
inline uint32_t read32(uint32_t address)
{
    return *reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(address));
}
#endif

/// One block: a name, a base, and how many 32-bit words.
struct SweepBlock {
    const char* name;
    uint32_t    base;
    unsigned    words;
};

/**
 * The confirmed set. Every base below was read successfully on this exact unit
 * by the 2026-07-29 investigation, and most were corroborated against RM0456
 * there.
 *
 * What each contributes to *this* experiment:
 *
 *   RCC  , which peripheral clocks are enabled. If no timer is clocked, no
 *            timer is driving anything, and Q11 is answered without ever
 *            reading a timer register.
 *   GPIO : twelve words per port, which covers MODER(0), OTYPER(1),
 *            OSPEEDR(2), PUPDR(3), IDR(4), ODR(5), BSRR(6), LCKR(7), AFRL(8),
 *            AFRH(9). ODR says which pin changed; MODER and AFRL/AFRH say
 *            whether that pin is a plain output or an alternate function. That
 *            pair is the coarse answer to "can this hardware dim at all", and
 *            it needs nothing beyond this table.
 *   NVIC , which interrupts are on, i.e. which drivers are live.
 *
 * The I2C, SPI and UART blocks are carried over unchanged. They are not
 * expected to move with the backlight, which is exactly why they are worth
 * keeping: a diff that shows changes scattered across unrelated peripherals is
 * a diff taken while something else was going on, and that is worth knowing
 * before drawing a conclusion from it.
 */
constexpr SweepBlock kConfirmed[] = {
    {"SCB",       0xE000ED00u, 8},
    {"NVIC_ISER", 0xE000E100u, 8},
    {"NVIC_IPR",  0xE000E400u, 32},
    {"RCC",       0x46020C00u, 64},
    {"GPIOA",     0x42020000u, 12},
    {"GPIOB",     0x42020400u, 12},
    {"GPIOC",     0x42020800u, 12},
    {"GPIOD",     0x42020C00u, 12},
    {"GPIOE",     0x42021000u, 12},
    {"GPIOF",     0x42021400u, 12},
    {"GPIOG",     0x42021800u, 12},
    {"GPIOH",     0x42021C00u, 12},
    {"I2C1",      0x40005400u, 8},
    {"I2C2",      0x40005800u, 8},
    {"I2C3",      0x46002800u, 8},
    {"I2C4",      0x40008400u, 8},
    {"I2C5",      0x40009800u, 8},
    {"I2C6",      0x40009C00u, 8},
    {"SPI1",      0x40013000u, 8},
    {"SPI3",      0x46002000u, 8},
    {"USART3",    0x40004800u, 8},
    {"LPUART1",   0x46002400u, 12},
};

/**
 * The timer set. UNCONFIRMED: see RegisterSweep.hpp before enabling it.
 *
 * These are the classic STM32 APB1/APB2 timer bases. They are consistent with
 * the I2C1/SPI1/USART3 bases above, which are the same classic addresses and
 * are confirmed on this part, so the inference is reasonable. It is still an
 * inference, and on this MCU a base that does not decode is a HardFault rather
 * than an error return.
 *
 * Twenty words covers CR1(0), CR2(1), SMCR(2), DIER(3), SR(4), EGR(5),
 * CCMR1(6), CCMR2(7), CCER(8), CNT(9), PSC(10), ARR(11), RCR(12), CCR1(13),
 * CCR2(14), CCR3(15), CCR4(16) on a general-purpose timer. `ARR` and the `CCRx`
 * group are the duty cycle: if one of those tracks the requested brightness,
 * the field is not inert after all and the whole investigation turns over. If
 * they are identical at every rung of the ladder, the field is dead at the
 * register level, which is a far stronger statement than any photograph.
 *
 * LPTIM1 and LPTIM2 are included because a front-light held at a fixed duty is
 * exactly the sort of thing a low-power timer exists for, and a design that used
 * one would leave every general-purpose timer unclocked and look, from RCC
 * alone, like a plain GPIO.
 */
constexpr SweepBlock kTimers[] = {
    {"TIM1",      0x40012C00u, 20},
    {"TIM2",      0x40000000u, 20},
    {"TIM3",      0x40000400u, 20},
    {"TIM4",      0x40000800u, 20},
    {"TIM5",      0x40000C00u, 20},
    {"TIM6",      0x40001000u, 20},
    {"TIM7",      0x40001400u, 20},
    {"TIM8",      0x40013400u, 20},
    {"TIM15",     0x40014000u, 20},
    {"TIM16",     0x40014400u, 20},
    {"TIM17",     0x40014800u, 20},
    {"LPTIM1",    0x46004400u, 16},
    {"LPTIM2",    0x40009400u, 16},
};

constexpr size_t kConfirmedCount = sizeof(kConfirmed) / sizeof(kConfirmed[0]);
constexpr size_t kTimerCount     = sizeof(kTimers) / sizeof(kTimers[0]);

/// `sweep_` + label + `.txt`, bounded. Labels come from the plan and are short.
bool buildPath(char* out, size_t outSize, const char* label)
{
    const int n = std::snprintf(out, outSize, "sweep_%s.txt", label ? label : "unnamed");
    return n > 0 && static_cast<size_t>(n) < outSize;
}

#if defined(SIMULATOR) || !defined(__ARM_ARCH)
bool appendBlocks(SDK::Interface::IFile&, const SweepBlock*, size_t)
{
    return true;
}
#else
/**
 * @brief Write one group of blocks, flushing after each.
 *
 * Four words per line with the line's own address, so a truncated line is still
 * locatable and two files diff line for line. Flushed per block rather than at
 * the end: if a later block's base does not decode, the fault is unrecoverable
 * and everything already committed is all the evidence there will be, and the
 * last block named in the file is then the one that killed it.
 */
bool appendBlocks(SDK::Interface::IFile& file, const SweepBlock* blocks, size_t count)
{
    char line[96];

    for (size_t b = 0; b < count; ++b) {
        const SweepBlock& block = blocks[b];

        for (unsigned i = 0; i < block.words; i += 4) {
            const uint32_t at = block.base + i * 4u;
            const int n = std::snprintf(
                line, sizeof(line), "SWP %-9s %08lX: %08lX %08lX %08lX %08lX\n", block.name,
                static_cast<unsigned long>(at), static_cast<unsigned long>(read32(at + 0)),
                static_cast<unsigned long>(read32(at + 4)),
                static_cast<unsigned long>(read32(at + 8)),
                static_cast<unsigned long>(read32(at + 12)));
            if (n <= 0) {
                return false;
            }

            size_t bw = 0;
            file.write(line, static_cast<size_t>(n), bw);
            if (bw != static_cast<size_t>(n)) {
                LOG_INFO("short write at %08lX\n", static_cast<unsigned long>(at));
                return false;
            }
        }

        file.flush();
    }
    return true;
}
#endif

} // namespace

namespace RegisterSweep
{

bool available() { return kHasRegisters; }

size_t confirmedBlockCount() { return kConfirmedCount; }

size_t timerBlockCount() { return kTimerCount; }

bool write(const SDK::Kernel& kernel, const char* label, bool includeTimers)
{
    if (!kHasRegisters) {
        // Says nothing rather than writing zeros. Zero is a perfectly plausible
        // register value, so a file full of them would be indistinguishable from
        // a real sweep of a quiet peripheral, and this app exists to produce
        // evidence, not plausible-looking files.
        LOG_INFO("no registers on this build; %s not written\n", label);
        return false;
    }

    char path[48];
    if (!buildPath(path, sizeof(path), label)) {
        LOG_INFO("label too long: %s\n", label ? label : "(null)");
        return false;
    }

    std::unique_ptr<SDK::Interface::IFile> f = kernel.fs.file(path);
    if (!f || !f->open(true, true)) {
        LOG_INFO("could not open %s\n", path);
        return false;
    }

    // A one-line header naming what this is and what it covers. Prefixed with
    // '#' so it is skipped by anything scanning for SWP lines, and kept to one
    // line so two sweeps still diff cleanly.
    char header[96];
    const int hn = std::snprintf(header, sizeof(header), "# sweep %s blocks=%u%s\n", label,
                                 static_cast<unsigned>(kConfirmedCount + (includeTimers ? kTimerCount : 0)),
                                 includeTimers ? " (incl. UNCONFIRMED timer bases)" : "");
    size_t bw = 0;
    if (hn > 0) {
        f->write(header, static_cast<size_t>(hn), bw);
    }

    bool ok = appendBlocks(*f, kConfirmed, kConfirmedCount);
    if (ok && includeTimers) {
        ok = appendBlocks(*f, kTimers, kTimerCount);
    }

    f->flush();
    f->close();

    LOG_INFO("%s %s\n", path, ok ? "written" : "INCOMPLETE");
    return ok;
}

} // namespace RegisterSweep
