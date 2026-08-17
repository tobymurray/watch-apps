/**
 ******************************************************************************
 * @file    DeviceContext.hpp
 * @brief   The context a flash image cannot carry about itself.
 ******************************************************************************
 *
 * A dumped flash image is remarkably self-describing: the kernel and bootloader
 * version strings are in it, so are the build paths they were compiled from, and
 * so is the name of every chip driver class the firmware contains. Someone
 * opening the `.bin` years later can recover all of that with `strings`.
 *
 * What they cannot recover is anything that lives in a **register** rather than
 * in `0x08000000`-`0x08400000`. That is the gap this file exists to close:
 *
 *   - **Which unit this is.** The 96-bit unique device ID. Two watches running
 *     identical firmware produce identical images; nothing in the image says
 *     which one it came off.
 *   - **Which die.** `DBGMCU_IDCODE` gives DEV_ID and the silicon revision. The
 *     firmware does not record the stepping it happened to run on.
 *   - **Whether the read was legitimate and unrestricted.** `CONTROL.nPRIV`,
 *     `MPU_CTRL.ENABLE` and `FLASH_OPTR.TZEN`. These are what make the image
 *     mean anything: an image taken while isolation was active would be
 *     suspect, and there is no way to tell after the fact.
 *   - **Where the kernel actually starts.** `SCB VTOR`, read live. The two-stage
 *     boundary can be *guessed* from the image's structure, but VTOR is the
 *     measurement rather than the inference.
 *   - **The option bytes.** `FLASH_OPTR`, `NSBOOTADD0R`, `NSBOOTADD1R` --
 *     RDP level, TrustZone, dual-bank, boot address. Option bytes are a separate
 *     flash area and are **not** inside the dumped region, so they are lost
 *     unless recorded here.
 *   - **How big the flash is.** So "4 MB" is a measurement rather than an
 *     assumption baked into the region default.
 *
 * Everything here is a **read**. Note in particular that this does *not* walk
 * the MPU region table: doing so requires writing `MPU_RNR` to select each
 * region, which the prior investigation's sweeps did and this app deliberately
 * will not. `MPU_TYPE` and `MPU_CTRL` answer the question that matters (is it on)
 * without writing anything.
 *
 * Reads are ordered safest-first -- system registers, then the ARM System
 * Control Space, then the one peripheral base -- so that if an address ever
 * faults it happens as late as possible. `CONTROL` comes first because it is an
 * `MRS` from a system register and cannot fault at all, which means the log
 * always carries the privilege answer even if something later goes wrong.
 *
 * Host builds get a stub: there is no MPU and no `FLASH_OPTR` on a Linux
 * simulator, and inventing values would be worse than saying so, because zero is
 * the *permissive* value for every isolation field. See `measured`.
 *
 *
 * ## Why a raw register sweep as well
 *
 * The decoded fields above answer "is this image trustworthy and which unit is
 * it". The raw sweep (see `appendSweep`) answers a different question:
 * **what changed between two firmware versions.**
 *
 * Diffing two flash images tells you the code changed. It does not tell you that
 * the new firmware enabled a peripheral, remapped a pin, turned on an interrupt,
 * or -- the one that would end this technique -- switched the MPU on. Those live
 * in RCC, GPIO, NVIC and the option bytes, none of which are in the dumped
 * region. They are readable only while that firmware is running, so a version's
 * register state is unrecoverable the moment it is replaced.
 *
 * So the sweep is written in the same raw, address-labelled form the prior
 * investigation used (`RCC 46020C00: xxxxxxxx ...`), which is both diffable with
 * `diff` and decodable with that investigation's existing Python. Every base
 * here was read successfully on this exact unit by that investigation, which is
 * why they are safe to read and why they are ordered as they are.
 *
 ******************************************************************************
 */

#ifndef DEVICE_CONTEXT_HPP
#define DEVICE_CONTEXT_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "DumpRegion.hpp"

namespace DeviceContext
{

/// Filename written alongside the chunk files and the manifest, so the three
/// travel together as one bundle.
constexpr char kPath[] = "dump_context.txt";

/// The registers, as read.
struct Result {
    // -- Identity -------------------------------------------------------------
    uint32_t cpuid     = 0; ///< CPUID at 0xE000ED00. 0x410FD214 = Cortex-M33 r0p4.
    uint32_t dbgIdcode = 0; ///< DBGMCU_IDCODE at 0xE0044000: DEV_ID[11:0], REV_ID[31:16].
    uint32_t uid[3]    = {}; ///< 96-bit unique device id at 0x0BFA0700. Which watch this is.
    uint32_t flashSizeKb = 0; ///< Low 16 bits of 0x0BFA07A0, in KB.

    // -- Layout ---------------------------------------------------------------
    uint32_t vtor = 0; ///< SCB VTOR at 0xE000ED08: the running kernel's vector table.

    // -- Isolation: whether reading arbitrary memory was permitted ------------
    uint32_t control = 0; ///< bit0 = nPRIV (0 = privileged), bit1 = SPSEL.
    uint32_t mpuType = 0; ///< bits[15:8] = DREGION, how many regions the silicon has.
    uint32_t mpuCtrl = 0; ///< bit0 = ENABLE, bit2 = PRIVDEFENA.
    uint32_t sauCtrl = 0; ///< bit0 = ENABLE. Corroborates TZEN from live core state.
    uint32_t sauType = 0; ///< bits[7:0] = SREGION.

    // -- Option bytes: not inside the dumped region ---------------------------
    uint32_t flashAcr     = 0; ///< 0x40022000: LATENCY[3:0] wait states, bit8 PRFTEN.
    uint32_t flashOptr    = 0; ///< 0x40022040: RDP[7:0], TZEN bit31, DUALBANK bit21.
    uint32_t nsBootAdd0   = 0; ///< 0x40022044.
    uint32_t nsBootAdd1   = 0; ///< 0x40022048.

    /// False on a build with no such registers (the host simulator). Callers
    /// must not present the zeros above as measurements in that case: "not
    /// measured" and "measured as zero" are opposite conclusions here.
    bool measured = false;

    /// Whether all three isolation mechanisms read as inactive, i.e. whether
    /// unrestricted reads should be expected to work. False when not measured.
    bool unrestricted() const
    {
        if (!measured) {
            return false;
        }
        const bool privileged = (control & 1u) == 0u;            // nPRIV == 0
        const bool mpuOff     = (mpuCtrl & 1u) == 0u;            // ENABLE == 0
        const bool tzOff      = (flashOptr & (1u << 31)) == 0u;  // TZEN == 0
        return privileged && mpuOff && tzOff;
    }

    /// RDP level as the reference manual defines it: 0xAA is level 0 (open),
    /// 0xCC is level 2 (permanently locked), anything else is level 1.
    uint8_t rdpByte() const { return static_cast<uint8_t>(flashOptr & 0xFFu); }
};

/// True on builds where these registers exist to be read.
bool available();

/// Reads the registers, or returns a Result with measured == false.
Result read();

/// One decoded line to the log, so a UART capture carries it too.
void log(const Result& result);

/**
 * @brief Write the human-readable context file into the app's own folder.
 *
 * Deliberately written at app start rather than at dump completion: it costs a
 * few register reads and one small file, and it means the context is on disk
 * even for a run that is interrupted, never started, or fails. It is also what
 * makes merely launching the app enough to capture the hardware context.
 *
 * @param uptimeMs Kernel uptime, the nearest thing to a timestamp an app can
 *                 get -- there is no wall-clock API on ISystem. The host knows
 *                 the real date from the file's own mtime.
 * @return Whether the file was written whole.
 */
bool write(const SDK::Kernel& kernel, const Result& result, const DumpRegion& region,
           const char* regionSource, uint32_t uptimeMs);

/**
 * @brief Append the raw register sweep to an already-open context file.
 *
 * Called by write(); separate so the reads and their formatting stay in one
 * place. Streams straight into the file -- see write() for why it is not
 * buffered first. A no-op returning true on host builds.
 */
bool appendSweep(SDK::Interface::IFile& file);

} // namespace DeviceContext

#endif // DEVICE_CONTEXT_HPP
