/**
 ******************************************************************************
 * @file    PwmLog.hpp
 * @brief   The record: `backlight_pwm.txt`, written as the run happens.
 ******************************************************************************
 *
 * The first two runs of this app produced a reboot and a video, and between them
 * almost nothing durable. The video was salvageable only because someone filmed
 * it; the watch itself kept one line. That is the gap this closes.
 *
 * Everything is written and flushed **as it happens**, never buffered to the end,
 * because the failure mode being chased is a reboot and an unflushed tail is the
 * part of the experiment nobody has.
 *
 * ## What is worth recording that the screen cannot show
 *
 *   - **The measured core clock.** Incidental to this app and useful beyond it:
 *     the 2026-07-29 investigation captured RCC three times and never decoded
 *     MSIRANGE or the PLL, so the clock tree is still an open item in its ledger.
 *     This app has to measure the core frequency to place its edges, so it may as
 *     well write the answer down.
 *   - **Requested against achieved duty**, per rung. The gap is the honest cost
 *     of a busy-wait PWM sharing a thread, and it is the argument for the DMA
 *     waveform.
 *   - **Kernel writes to the pin**, counted per rung. The pin is sampled every
 *     burst and a disagreement with what this app last wrote means the kernel
 *     wrote it. This is the whole content of the contest rungs.
 *   - **Wall-clock timestamps** against kernel uptime, so a video and this file
 *     can be lined up afterwards without guessing.
 *
 ******************************************************************************
 */

#ifndef PWM_LOG_HPP
#define PWM_LOG_HPP

#include <cstdarg>
#include <cstddef>
#include <cstdint>

#include "SDK/Interfaces/IFileSystem.hpp"

#include "DmaPwm.hpp"
#include "PwmPlan.hpp"

namespace Pwm
{

/// What one rung did, gathered as it ran.
struct RungResult {
    size_t   index        = 0;
    uint8_t  requested    = 0;
    uint8_t  achieved     = 0;
    uint32_t periods      = 0;
    uint32_t edges        = 0;
    uint32_t elapsedMs    = 0;
    uint32_t startedAtMs  = 0;

    /// Bursts whose ODR sample disagreed with what this app last wrote, meaning
    /// the kernel had written the pin in between.
    uint32_t kernelWrites = 0;

    /// Uptime of the first such disagreement, or 0. For the contest rungs this is
    /// the moment the kernel acted, which is the number worth having.
    uint32_t firstKernelWriteMs = 0;

    /// Bursts sampled, so kernelWrites has a denominator.
    uint32_t samples = 0;
};

class PwmLog
{
public:
    explicit PwmLog(SDK::Interface::IFile& file);

    /// Preamble: what this file is, and the measured core clock.
    void header(uint32_t uptimeMs, bool driving, uint32_t cyclesPerUs, uint32_t periodUs,
                uint32_t burstUs, size_t rungCount);

    /// What the hardware engine claimed, when it is the one running. Separate
    /// from header() because the software engine has none of it.
    void dmaHeader(uint8_t timerIndex, uint8_t channel, const DmaRates& first,
                   const DmaRates& final);

    /// Why a run declined to drive anything, when it does.
    void refused(const char* why);

    /// One rung beginning, written before it runs so a reboot mid-rung still
    /// names it.
    void rungBegan(size_t index, const Rung& rung, uint32_t atMs);

    /// The same rung's outcome, written as it ends.
    void rungDone(const RungResult& result, const Rung& rung);

    /// Closing summary, with the reading instructions a stranger would need.
    void footer(uint32_t uptimeMs, uint32_t totalPeriods, uint32_t totalEdges,
                uint32_t totalKernelWrites);

    bool intact() const { return mIntact; }

private:
    SDK::Interface::IFile& mFile;
    bool                   mIntact = true;

    void line(const char* format, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;
};

} // namespace Pwm

#endif // PWM_LOG_HPP
