/**
 ******************************************************************************
 * @file    DeviceContext.cpp
 * @brief   Reading the context registers, and writing them next to the dump.
 ******************************************************************************
 */

#include "DeviceContext.hpp"

#include <cstdio>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Messages/CommandMessages.hpp"

#define LOG_MODULE_PRX      "Context"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace {

// SIMULATOR is what the SDK's own simulator sources switch on; the service half
// of a simulator build is compiled for the host too, so keying off the
// architecture as well means this stays a stub anywhere that is not a Cortex-M.
#if defined(SIMULATOR) || !defined(__ARM_ARCH)
constexpr bool kHasRegisters = false;
#else
constexpr bool kHasRegisters = true;

/// Absolute addresses, all from the 2026-07-29 hardware-config investigation and
/// corroborated there against RM0456. Read through volatile so the compiler
/// cannot fold, reorder or elide them: these are reads whose whole purpose is to
/// happen.
constexpr uint32_t kCpuid       = 0xE000ED00u;
constexpr uint32_t kVtor        = 0xE000ED08u;
constexpr uint32_t kMpuType     = 0xE000ED90u;
constexpr uint32_t kMpuCtrl     = 0xE000ED94u;
constexpr uint32_t kSauCtrl     = 0xE000EDD0u;
constexpr uint32_t kSauType     = 0xE000EDD4u;
constexpr uint32_t kDbgIdcode   = 0xE0044000u;
constexpr uint32_t kUid         = 0x0BFA0700u;
constexpr uint32_t kFlashSize   = 0x0BFA07A0u;
constexpr uint32_t kFlashAcr    = 0x40022000u;
constexpr uint32_t kFlashOptr   = 0x40022040u;
constexpr uint32_t kNsBootAdd0  = 0x40022044u;
constexpr uint32_t kNsBootAdd1  = 0x40022048u;

inline uint32_t read32(uint32_t address)
{
    return *reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(address));
}
#endif

/// How long to wait for the kernel to answer the system-info request. Short:
/// this runs before the app is usable, so a kernel that does not implement the
/// message must cost a blink rather than a visible stall. Everything it would
/// have told us is also recoverable from the image itself.
constexpr uint32_t kSystemInfoTimeoutMs = 250;

/// One block of the raw sweep: a name, a base, and how many 32-bit words.
struct SweepBlock {
    const char* name;
    uint32_t    base;
    unsigned    words;
};

/// Ordered safest-first: the ARM System Control Space and the STM32U5 system
/// information area before the peripheral bases, and every base below was read
/// successfully on this unit by the 2026-07-29 investigation.
///
/// Chosen for what a version-to-version diff needs to see:
///   RCC   -- which clocks and peripherals the firmware enables.
///   GPIO  -- the pin-mux: MODER/OTYPER/OSPEEDR/PUPDR at words 0-3, AFRL/AFRH at 8-9.
///   NVIC  -- ISER0..7, i.e. which interrupts are on, i.e. which drivers are live.
/// GPIOH read back all-ones (absent or unclocked) last time; recorded anyway,
/// because "still absent" is itself a comparison worth being able to make.
constexpr SweepBlock kSweep[] = {
    // ARM System Control Space first: architectural, identical on every M33.
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
    // Peripherals last. Every base below was read successfully on this unit by
    // the 2026-07-29 investigation; I2C4/5/6 were added by its sweep #7 after
    // RM0456 confirmed they exist on this device group. TIMINGR (word 4 of an
    // I2C block) is the bus speed, BRR (word 3 of a USART) the baud -- both
    // things a firmware update can change without the flash diff showing why.
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

} // namespace

namespace DeviceContext
{

bool available() { return kHasRegisters; }

Result read(const SDK::Kernel& kernel)
{
    Result result;

    // Ask the kernel what firmware it is. A bounded request/response: with a
    // non-zero timeout sendMessage returns only once the kernel has filled the
    // message in place, so this cannot hang -- and a kernel that does not
    // implement it just leaves systemInfoOk false rather than blocking startup.
    // Worth having because it is the one statement of the firmware version that
    // does not require running `strings` over the dump afterwards.
    if (auto* info = kernel.comm.allocateMessage<SDK::Message::RequestSystemInfo>()) {
        if (kernel.comm.sendMessage(info, kSystemInfoTimeoutMs)
                && info->getResult() == SDK::MessageResult::SUCCESS) {
            std::snprintf(result.firmwareVersion, sizeof(result.firmwareVersion), "%s",
                          info->firmwareVersion);
            std::snprintf(result.hardwareVersion, sizeof(result.hardwareVersion), "%s",
                          info->hardwareVersion);
            result.uptimeSeconds = info->uptimeSeconds;
            result.systemInfoOk  = true;
        }
        kernel.comm.releaseMessage(info);
    }

#if defined(SIMULATOR) || !defined(__ARM_ARCH)
    // Nothing to read. measured stays false, which is what callers check --
    // reporting the zero-initialised fields as findings would claim the
    // permissive answer for every isolation field.
#else
    // CONTROL first: an MRS from a system register, which cannot fault however
    // locked down the system is. So the log always carries the privilege answer
    // even if a later memory-mapped read is the thing that goes wrong.
    uint32_t control = 0;
    __asm volatile("MRS %0, CONTROL" : "=r"(control));
    result.control = control;

    // ARM System Control Space: architectural addresses, identical on every
    // Cortex-M33.
    result.cpuid   = read32(kCpuid);
    result.vtor    = read32(kVtor);
    result.mpuType = read32(kMpuType);
    result.mpuCtrl = read32(kMpuCtrl);
    result.sauCtrl = read32(kSauCtrl);
    result.sauType = read32(kSauType);
    result.dbgIdcode = read32(kDbgIdcode);

    // STM32U5 system information area.
    result.uid[0] = read32(kUid + 0);
    result.uid[1] = read32(kUid + 4);
    result.uid[2] = read32(kUid + 8);
    result.flashSizeKb = read32(kFlashSize) & 0xFFFFu;

    // Last: the FLASH peripheral, the only base here that is a family constant
    // rather than architectural.
    result.flashAcr   = read32(kFlashAcr);
    result.flashOptr  = read32(kFlashOptr);
    result.nsBootAdd0 = read32(kNsBootAdd0);
    result.nsBootAdd1 = read32(kNsBootAdd1);

    result.measured = true;
#endif

    return result;
}

void log(const Result& result)
{
    if (!result.measured) {
        LOG_INFO("context not measured on this build (no MPU/FLASH_OPTR to read)\n");
        return;
    }

    LOG_INFO("CONTROL=%08lX nPRIV=%u | MPU_CTRL=%08lX ENABLE=%u | FLASH_OPTR=%08lX RDP=%02X "
             "TZEN=%u | VTOR=%08lX | IDCODE=%08lX | flash=%luKB\n",
             static_cast<unsigned long>(result.control),
             static_cast<unsigned>(result.control & 1u),
             static_cast<unsigned long>(result.mpuCtrl),
             static_cast<unsigned>(result.mpuCtrl & 1u),
             static_cast<unsigned long>(result.flashOptr), result.rdpByte(),
             static_cast<unsigned>((result.flashOptr >> 31) & 1u),
             static_cast<unsigned long>(result.vtor),
             static_cast<unsigned long>(result.dbgIdcode),
             static_cast<unsigned long>(result.flashSizeKb));

    if (result.unrestricted()) {
        LOG_INFO("no isolation active: unrestricted reads expected to work\n");
    } else {
        LOG_INFO("ISOLATION ACTIVE -- reads of kernel flash may fault and end the app\n");
    }
}

bool appendSweep(SDK::Interface::IFile& file)
{
#if defined(SIMULATOR) || !defined(__ARM_ARCH)
    (void)file;
    return true; // Nothing to sweep; write() already recorded measured=N.
#else
    // Four words per line with the line's own address, so a dropped or
    // truncated line is still locatable -- and so two versions' files diff
    // line-for-line.
    char line[96];

    for (const SweepBlock& block : kSweep) {
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
                LOG_INFO("short write during sweep at %08lX\n", static_cast<unsigned long>(at));
                return false;
            }
        }
    }
    return true;
#endif
}

bool write(const SDK::Kernel& kernel, const Result& result, const DumpRegion& region,
           const char* regionSource, uint32_t uptimeMs)
{
    // Built in one buffer and written once. Small enough to be a stack frame on
    // a 10 kB service stack with room to spare, and one write means the file is
    // either whole or absent rather than half a record.
    char text[1600];
    int at = 0;

    // Appends with a running offset, giving up quietly if the buffer fills. A
    // truncated context file is still useful -- unlike the manifest, nothing
    // parses this with a regex that could match half a line.
    auto add = [&](const char* fmt, auto... args) {
        if (at < 0 || static_cast<size_t>(at) >= sizeof(text)) {
            return;
        }
        const int n = std::snprintf(text + at, sizeof(text) - static_cast<size_t>(at), fmt, args...);
        if (n > 0) {
            at += n;
        }
    };

    add("# FwDump context -- what the flash image cannot say about itself.\n");
    add("# Registers are not inside the dumped region, so they are recorded here\n");
    add("# or lost. Written at app start, before any dump.\n");
    add("CTX dumper=FwDump app_version=%s\n", "1.0.0");
    add("CTX region base=%08lX size=%08lX chunk=%08lX subwrite=%08lX nchunks=%u source=%s\n",
        static_cast<unsigned long>(region.base), static_cast<unsigned long>(region.size),
        static_cast<unsigned long>(region.chunk), static_cast<unsigned long>(region.subwrite),
        region.nchunks(), regionSource);
    add("CTX uptime_ms=%lu\n", static_cast<unsigned long>(uptimeMs));

    // The kernel's own account of what it is. Absent rather than blank when it
    // did not answer, so nobody reads an empty string as "version unknown to
    // the kernel" when it means "the kernel was never asked successfully".
    if (result.systemInfoOk) {
        add("CTX kernel firmware=%s hardware=%s uptime_s=%lu\n", result.firmwareVersion,
            result.hardwareVersion, static_cast<unsigned long>(result.uptimeSeconds));
    } else {
        add("CTX kernel firmware=unavailable (no answer to REQUEST_SYSTEM_INFO)\n");
    }

    if (!result.measured) {
        // Says so explicitly rather than emitting zeros: zero is the permissive
        // value for every isolation field, so silent zeros would read as "no
        // isolation" on a build that never looked.
        add("CTX measured=N reason=no-such-registers-on-this-build\n");
    } else {
        add("CTX measured=Y\n");
        add("CTX identity cpuid=%08lX idcode=%08lX dev_id=%03lX rev_id=%04lX flash_kb=%lu\n",
            static_cast<unsigned long>(result.cpuid),
            static_cast<unsigned long>(result.dbgIdcode),
            static_cast<unsigned long>(result.dbgIdcode & 0xFFFu),
            static_cast<unsigned long>((result.dbgIdcode >> 16) & 0xFFFFu),
            static_cast<unsigned long>(result.flashSizeKb));
        // The per-unit serial: the only thing here that says which watch this
        // image came off.
        add("CTX uid=%08lX%08lX%08lX\n", static_cast<unsigned long>(result.uid[2]),
            static_cast<unsigned long>(result.uid[1]),
            static_cast<unsigned long>(result.uid[0]));
        add("CTX vtor=%08lX\n", static_cast<unsigned long>(result.vtor));
        add("CTX isolation control=%08lX nPRIV=%u mpu_ctrl=%08lX mpu_enable=%u dregion=%u "
            "sau_ctrl=%08lX sau_sregion=%u unrestricted=%s\n",
            static_cast<unsigned long>(result.control),
            static_cast<unsigned>(result.control & 1u),
            static_cast<unsigned long>(result.mpuCtrl),
            static_cast<unsigned>(result.mpuCtrl & 1u),
            static_cast<unsigned>((result.mpuType >> 8) & 0xFFu),
            static_cast<unsigned long>(result.sauCtrl),
            static_cast<unsigned>(result.sauType & 0xFFu),
            result.unrestricted() ? "Y" : "N");
        add("CTX option flash_acr=%08lX flash_optr=%08lX rdp=%02X tzen=%u dualbank=%u "
            "nsbootadd0=%08lX nsbootadd1=%08lX\n",
            static_cast<unsigned long>(result.flashAcr),
            static_cast<unsigned long>(result.flashOptr), result.rdpByte(),
            static_cast<unsigned>((result.flashOptr >> 31) & 1u),
            static_cast<unsigned>((result.flashOptr >> 21) & 1u),
            static_cast<unsigned long>(result.nsBootAdd0),
            static_cast<unsigned long>(result.nsBootAdd1));
    }

    if (at <= 0) {
        return false;
    }
    const size_t length = static_cast<size_t>(at);

    std::unique_ptr<SDK::Interface::IFile> f = kernel.fs.file(kPath);
    if (!f || !f->open(true, true)) {
        LOG_INFO("could not open %s\n", kPath);
        return false;
    }

    size_t bw = 0;
    f->write(text, length, bw);
    bool ok = (bw == length);
    if (!ok) {
        LOG_INFO("short write to %s: %u of %u\n", kPath, static_cast<unsigned>(bw),
                 static_cast<unsigned>(length));
    }

    // The raw sweep is streamed line-group by line-group into the same open
    // file rather than accumulated first. It is a few kilobytes -- too much for
    // a stack buffer on a 10 kB service stack, and there is nothing to gain from
    // buffering it: unlike the manifest, nothing parses this with a regex that a
    // partial line could mislead, so a truncated sweep is merely shorter rather
    // than wrong.
    if (ok && result.measured) {
        ok = appendSweep(*f);
    }

    f->flush();
    f->close();
    return ok;
}

} // namespace DeviceContext
