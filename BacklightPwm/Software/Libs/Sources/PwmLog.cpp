/**
 ******************************************************************************
 * @file    PwmLog.cpp
 * @brief   The exact wording of the record.
 ******************************************************************************
 */

#include "PwmLog.hpp"

#include <cstdio>

namespace Pwm
{

namespace
{
constexpr size_t kLineMax = 200;

const char* askName(KernelAsk ask)
{
    switch (ask) {
        case KernelAsk::Nothing:      return "nothing";
        case KernelAsk::HoldOn:       return "hold-on-10min";
        case KernelAsk::ShortAutoOff: return "on-autooff-2s";
        case KernelAsk::TurnOff:      return "turn-off";
    }
    return "?";
}
} // namespace

PwmLog::PwmLog(SDK::Interface::IFile& file)
    : mFile(file)
{
}

void PwmLog::line(const char* format, ...)
{
    char buffer[kLineMax];

    va_list args;
    va_start(args, format);
    int n = std::vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    va_end(args);

    if (n < 0) {
        mIntact = false;
        return;
    }
    if (static_cast<size_t>(n) > sizeof(buffer) - 2) {
        n = static_cast<int>(sizeof(buffer) - 2);
        mIntact = false;
    }
    buffer[n]     = '\n';
    buffer[n + 1] = '\0';

    const size_t want = static_cast<size_t>(n) + 1u;
    size_t       bw   = 0;
    mFile.write(buffer, want, bw);
    if (bw != want) {
        mIntact = false;
    }
    // Flushed per line. The failure being chased is a reboot, and an unflushed
    // tail is the part of the experiment nobody has.
    mFile.flush();
}

void PwmLog::header(uint32_t uptimeMs, bool driving, uint32_t cyclesPerUs, uint32_t periodUs,
                    uint32_t burstUs, size_t rungCount)
{
    line("# BacklightPwm results");
    line("#");
    line("# This app drives GPIOF PF3 directly, the pin the kernel uses as a plain");
    line("# on/off backlight enable. The ladder below asks for duty cycles the");
    line("# kernel's own REQUEST_BACKLIGHT_SET cannot produce.");
    line("#");
    line("# Compare against BacklightProbe's run at the same six values. There the");
    line("# registers were byte-identical at every brightness; here they should not");
    line("# be, and the light should step.");
    line("PWM start uptime_ms=%lu driving=%c", static_cast<unsigned long>(uptimeMs),
         driving ? 'Y' : 'N');

    // Worth more than this app: the 2026-07-29 investigation captured RCC three
    // times and never decoded the clock tree, so this is a measured answer to a
    // question that was left open there.
    line("PWM core_clock_measured=%lu MHz (cycles_per_us, from DWT_CYCCNT over a busy wait)",
         static_cast<unsigned long>(cyclesPerUs));
    line("PWM period_us=%lu periods_per_burst=%lu rungs=%u", static_cast<unsigned long>(periodUs),
         static_cast<unsigned long>(burstUs), static_cast<unsigned>(rungCount));
    line("#");
    line("# kernel_writes counts bursts where GPIOF ODR disagreed with what this");
    line("# app last wrote. It is sampled just after our own write, so it catches");
    line("# a kernel that is winning the pin, not every kernel write: one landing");
    line("# between two of ours is overwritten within a millisecond and unseen.");
    line("# A zero count therefore means our writes dominate. Whether the light");
    line("# stayed lit through the contest rungs is the answer that counts.");
}

void PwmLog::dmaHeader(uint8_t timerIndex, uint8_t channel, const DmaRates& first,
                       const DmaRates& final)
{
    line("PWM engine=timer+DMA TIM%u GPDMA_ch%u", static_cast<unsigned>(timerIndex),
         static_cast<unsigned>(channel));
    line("PWM rate first  arr=%lu awake=%lu Hz", static_cast<unsigned long>(first.arr),
         static_cast<unsigned long>(first.hzAwake));
    line("PWM rate final  arr=%lu awake=%lu Hz asleep=%lu Hz",
         static_cast<unsigned long>(final.arr), static_cast<unsigned long>(final.hzAwake),
         static_cast<unsigned long>(final.hzAsleep));
    line("#");
    line("# Those rates are counted passes of the waveform buffer over a half");
    line("# second of wall clock, read from the block-repeat counter the DMA");
    line("# keeps in hardware. They are measurements. Two earlier versions of");
    line("# this engine computed the rate from an assumed clock instead and were");
    line("# wrong by fifty times and by twenty five times, both of which reach the");
    line("# eye as a flashing screen rather than a dimmed one.");
    line("#");
    line("# awake against asleep is the question that decides whether any of this");
    line("# is usable. awake spins the core through the window; asleep hands it");
    line("# back for the same wall-clock time. If they agree, the waveform really");
    line("# is autonomous and the CPU costs nothing. If asleep is the lower, the");
    line("# timer or the DMA is gated off while the core sleeps, and no divider");
    line("# fixes that: the light would blink at the kernel's sleep cadence.");
    line("#");
    line("# On this engine 'achieved' is the duty the hardware was COMMANDED to");
    line("# produce, not a measurement: the duty is not counted, only the rate.");
    line("# The software engine's achieved figure is a measurement; this one is");
    line("# not, and they should not be compared.");
}

void PwmLog::refused(const char* why)
{
    line("PWM REFUSED %s", why ? why : "");
    line("PWM Nothing was driven. Nothing below describes hardware.");
}

void PwmLog::rungBegan(size_t index, const Rung& rung, uint32_t atMs)
{
    line("%s", "");
    line("RUNG %02u begin duty=%u hold_ms=%lu kernel_ask=%s at_ms=%lu  %s",
         static_cast<unsigned>(index + 1u), static_cast<unsigned>(rung.duty),
         static_cast<unsigned long>(rung.holdMs), askName(rung.ask),
         static_cast<unsigned long>(atMs), rung.label ? rung.label : "");
}

void PwmLog::rungDone(const RungResult& result, const Rung& rung)
{
    line("RUNG %02u end   requested=%u achieved=%u periods=%lu edges=%lu elapsed_ms=%lu",
         static_cast<unsigned>(result.index + 1u), static_cast<unsigned>(result.requested),
         static_cast<unsigned>(result.achieved), static_cast<unsigned long>(result.periods),
         static_cast<unsigned long>(result.edges), static_cast<unsigned long>(result.elapsedMs));

    line("RUNG %02u pin   kernel_writes=%lu of %lu samples first_at_ms=%lu",
         static_cast<unsigned>(result.index + 1u),
         static_cast<unsigned long>(result.kernelWrites),
         static_cast<unsigned long>(result.samples),
         static_cast<unsigned long>(result.firstKernelWriteMs));

    // Said in words on the rungs that exist to provoke it, so the finding is a
    // sentence rather than a number a reader has to interpret.
    if (rung.ask == KernelAsk::ShortAutoOff || rung.ask == KernelAsk::TurnOff) {
        if (result.kernelWrites == 0u) {
            // Carefully worded. The sample is taken immediately after this app's
            // own write, so a kernel write landing between two of ours is
            // overwritten within a millisecond and never seen. Zero here means
            // this app's writes dominate the pin, which is a real finding; it is
            // NOT evidence that the kernel never wrote it.
            line("RUNG %02u NOTE  no disagreement seen, so this app's writes dominate the pin.",
                 static_cast<unsigned>(result.index + 1u));
            line("RUNG %02u NOTE  Not proof the kernel stayed off it: the sample is taken just",
                 static_cast<unsigned>(result.index + 1u));
            line("RUNG %02u NOTE  after our own write, so a kernel write in between is missed.",
                 static_cast<unsigned>(result.index + 1u));
            line("RUNG %02u NOTE  Whether the light stayed lit is the answer that counts.",
                 static_cast<unsigned>(result.index + 1u));
        } else {
            line("RUNG %02u NOTE  the kernel wrote the pin %lu times: it contests it, and the",
                 static_cast<unsigned>(result.index + 1u),
                 static_cast<unsigned long>(result.kernelWrites));
            line("RUNG %02u NOTE  video says whether the light stayed visible anyway",
                 static_cast<unsigned>(result.index + 1u));
        }
    }
}

void PwmLog::footer(uint32_t uptimeMs, uint32_t totalPeriods, uint32_t totalEdges,
                    uint32_t totalKernelWrites)
{
    line("%s", "");
    line("PWM done uptime_ms=%lu periods=%lu edges=%lu kernel_writes=%lu",
         static_cast<unsigned long>(uptimeMs), static_cast<unsigned long>(totalPeriods),
         static_cast<unsigned long>(totalEdges), static_cast<unsigned long>(totalKernelWrites));
    line("PWM intact=%c", mIntact ? 'Y' : 'N');
    line("PWM The brightness ladder itself is on the video, not here: no app can");
    line("PWM read the light back. This file says what was asked for and what the");
    line("PWM pin did about it.");
}

} // namespace Pwm
