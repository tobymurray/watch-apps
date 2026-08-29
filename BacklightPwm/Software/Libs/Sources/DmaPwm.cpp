/**
 ******************************************************************************
 * @file    DmaPwm.cpp
 * @brief   Register-level setup for the timer-driven DMA waveform.
 ******************************************************************************
 *
 * Every address and bit position here comes from ST's own headers, not from
 * inference: `cmsis_device_u5/Include/stm32u5a5xx.h` for bases, offsets and bit
 * positions, and `stm32u5xx_hal_driver/Inc/stm32u5xx_hal_dma.h` for the GPDMA
 * request numbers. Offsets were taken from the "Address offset:" comments the
 * header carries rather than by walking the structs, because a struct walk got
 * `RCC` wrong by an order of magnitude before the comments caught it.
 ******************************************************************************
 */

#include "DmaPwm.hpp"

#define LOG_MODULE_PRX      "DmaPwm"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{

#if defined(SIMULATOR) || !defined(__ARM_ARCH)
constexpr bool kHasRegisters = false;
#else
constexpr bool kHasRegisters = true;
#endif

// -- Addresses ---------------------------------------------------------------

constexpr uint32_t kGpiofBsrr = 0x42021418u;
constexpr uint32_t kBr3       = 1u << (16 + 3); ///< Drive PF3 low: light on.
constexpr uint32_t kBs3       = 1u << 3;        ///< Release PF3: light off.

constexpr uint32_t kRccBase     = 0x46020C00u;
constexpr uint32_t kRccAhb1Enr  = kRccBase + 0x88u; ///< bit 0 GPDMA1EN
constexpr uint32_t kRccApb1Enr1 = kRccBase + 0x9Cu; ///< bit 4 TIM6EN, bit 5 TIM7EN

constexpr uint32_t kTim6Base = 0x40001000u;
constexpr uint32_t kTim7Base = 0x40001400u;

/// Basic-timer register offsets. Only these four are touched.
constexpr uint32_t kTimCr1  = 0x00u; ///< bit 0 CEN
constexpr uint32_t kTimDier = 0x0Cu; ///< bit 8 UDE
constexpr uint32_t kTimEgr  = 0x14u; ///< bit 0 UG
constexpr uint32_t kTimCnt  = 0x24u;
constexpr uint32_t kTimPsc  = 0x28u;
constexpr uint32_t kTimArr  = 0x2Cu;

/// Window for counting waveform passes. Long enough to resolve the wrong end of
/// the range, where a waveform stuck at 10 Hz still shows four passes, and short
/// of the block counter's two thousand at the right end.
///
/// It is also the length of a contiguous busy-wait, which is the reason it is
/// not longer: this app has already rebooted the watch once by not handing the
/// CPU back, and while the liveness watchdog tolerated tens of seconds then,
/// there is no reason to spend more of that budget than the measurement needs.
constexpr uint32_t kWindowMs = 400u;

/// A waveform that has not completed one pass in half a second is not running,
/// whatever the registers say.
constexpr uint32_t kMinHz = 2u;

/// Starting guess for the divider, only ever a starting point: the trim below
/// corrects it from measurement, so being wrong here costs an iteration and
/// nothing else.
constexpr uint32_t kGuessTimerHz = 160000000u;

constexpr uint32_t kGpdma1Base    = 0x40020000u;
constexpr uint32_t kChan0Offset   = 0x50u;
constexpr uint32_t kChanStride    = 0x80u;
constexpr uint32_t kChannelCount  = 16u;

// Channel register offsets, relative to the channel base.
constexpr uint32_t kCfcr = 0x0Cu; ///< bit 8 TCF
constexpr uint32_t kCsr  = 0x10u; ///< bit 0 IDLEF
constexpr uint32_t kCcr  = 0x14u; ///< bit 0 EN, bit 1 RESET
constexpr uint32_t kCtr1 = 0x40u;
constexpr uint32_t kCtr2 = 0x44u;
constexpr uint32_t kCbr1 = 0x48u;
constexpr uint32_t kCsar = 0x4Cu;
constexpr uint32_t kCdar = 0x50u;
constexpr uint32_t kCbr2 = 0x58u;
constexpr uint32_t kCllr = 0x7Cu;

constexpr uint32_t kCcrEn    = 1u << 0;
constexpr uint32_t kCcrReset = 1u << 1;
constexpr uint32_t kCsrIdlef = 1u << 0;

/// CTR1: 32-bit source and destination, source increments, destination does not.
constexpr uint32_t kSdwWord = 2u << 0;   ///< SDW_LOG2 = 2 -> 4 bytes
constexpr uint32_t kSinc    = 1u << 3;
constexpr uint32_t kDdwWord = 2u << 16;  ///< DDW_LOG2 = 2 -> 4 bytes
constexpr uint32_t kDinc    = 1u << 19;  ///< left clear: BSRR is one address

/// CTR2: hardware request selection. DREQ stays clear because the request comes
/// from the timer rather than from the destination peripheral, and BREQ stays
/// clear because one request should move one word rather than a whole burst.
constexpr uint32_t kReqSelShift = 0u;

constexpr uint32_t kTim6UpRequest = 4u;
constexpr uint32_t kTim7UpRequest = 5u;

/// CBR1 fields.
constexpr uint32_t kBrcShift  = 16u;
constexpr uint32_t kBrcMax    = 0x7FFu; ///< 11 bits: 2048 block repeats
constexpr uint32_t kBrsdec    = 1u << 30;

constexpr uint32_t kBlockBytes = static_cast<uint32_t>(Pwm::kWaveWords) * 4u;

/// The waveform. Static so its address is fixed and it outlives every call.
/// Aligned to a word because the DMA reads it as 32-bit items.
alignas(4) uint32_t gWave[Pwm::kWaveWords];

#if defined(SIMULATOR) || !defined(__ARM_ARCH)
inline uint32_t rd(uint32_t) { return 0; }
inline void     wr(uint32_t, uint32_t) {}
#else
inline uint32_t rd(uint32_t a)
{
    return *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(a));
}
inline void wr(uint32_t a, uint32_t v)
{
    *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(a)) = v;
}
#endif

uint32_t timerBase(uint8_t index) { return index == 6 ? kTim6Base : kTim7Base; }
uint32_t timerRequest(uint8_t index) { return index == 6 ? kTim6UpRequest : kTim7UpRequest; }
uint32_t timerEnableBit(uint8_t index) { return index == 6 ? (1u << 4) : (1u << 5); }

uint32_t channelBase(uint8_t ch)
{
    return kGpdma1Base + kChan0Offset + static_cast<uint32_t>(ch) * kChanStride;
}

} // namespace

namespace Pwm
{

const char* idleName(Idle i)
{
    switch (i) {
        case Idle::Spin:      return "spin";
        case Idle::Yield:     return "yield";
        case Idle::ShortNaps: return "1ms-naps";
        case Idle::LongSleep: return "long-sleep";
    }
    return "?";
}

const char* dmaStatusName(DmaStatus s)
{
    switch (s) {
        case DmaStatus::Idle:          return "idle";
        case DmaStatus::Running:       return "running";
        case DmaStatus::NoRegisters:   return "no registers on this build";
        case DmaStatus::NoFreeTimer:   return "no free timer";
        case DmaStatus::NoFreeChannel: return "no free DMA channel";
        case DmaStatus::VerifyFailed:  return "register readback mismatch, nothing enabled";
        case DmaStatus::NoTimerClock:  return "the waveform did not advance";
    }
    return "?";
}

void DmaPwm::buildWave(uint8_t dutyPercent)
{
    if (dutyPercent > 100u) {
        dutyPercent = 100u;
    }
    mDuty = dutyPercent;

    // How many of the words are "on". Rounded rather than truncated so that a
    // requested 1 percent produces at least one lit word rather than none.
    const size_t on = (static_cast<size_t>(dutyPercent) * kWaveWords + 50u) / 100u;

    for (size_t i = 0; i < kWaveWords; ++i) {
        gWave[i] = (i < on) ? kBr3 : kBs3;
    }
}

bool DmaPwm::pickTimer()
{
    // A clear enable bit means the kernel is not using that timer. TIM6 and TIM7
    // are basic timers with no output pins, so borrowing one disturbs nothing
    // that could be driving hardware.
    const uint32_t apb1 = rd(kRccApb1Enr1);
    static const uint8_t kCandidates[] = {6u, 7u};
    for (uint8_t idx : kCandidates) {
        if ((apb1 & timerEnableBit(idx)) == 0u) {
            mTimerIndex = idx;
            return true;
        }
    }
    return false;
}

void DmaPwm::rearm()
{
    const uint32_t cb = channelBase(mChannel);
    wr(cb + kCcr, rd(cb + kCcr) & ~kCcrEn);
    wr(cb + kCfcr, 0xFFFFu);
    wr(cb + kCbr1, kBlockBytes | (kBrcMax << kBrcShift) | kBrsdec);
    wr(cb + kCsar, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gWave)));
    wr(cb + kCdar, kGpiofBsrr);
    wr(cb + kCcr, rd(cb + kCcr) | kCcrEn);
}

void DmaPwm::setArr(uint32_t arr)
{
    if (arr < 1u) {
        arr = 1u;
    }
    if (arr > Pwm::kArrMax) {
        arr = Pwm::kArrMax;
    }
    mArr = arr;
    // ARPE is clear on a basic timer, so this takes effect without an update
    // event and without disturbing the DMA request stream.
    wr(timerBase(mTimerIndex) + kTimArr, arr - 1u);
}

uint32_t DmaPwm::measureHz(Idle idle, uint32_t windowMs, void (*sleepMs)(uint32_t),
                           uint32_t (*nowMs)(), void (*yieldNow)())
{
    if (!kHasRegisters || nowMs == nullptr) {
        return 0u;
    }

    const uint32_t cb = channelBase(mChannel);

    // Start from a full block counter so the window cannot run it out, which
    // would look like a stalled waveform rather than a fast one.
    rearm();

    const uint32_t brcBefore = (rd(cb + kCbr1) >> kBrcShift) & kBrcMax;
    const uint32_t t0        = nowMs();

    switch (idle) {
        case Idle::Spin:
            // Never gives the core up, so this is the hardware's own rate with
            // nothing gated. Every other mode is measured against it.
            while ((nowMs() - t0) < windowMs) {
            }
            break;

        case Idle::Yield:
            if (yieldNow == nullptr) {
                return 0u;
            }
            while ((nowMs() - t0) < windowMs) {
                yieldNow();
            }
            break;

        case Idle::ShortNaps:
            // A kernel may treat a long wait as licence to stop the clocks and a
            // one-millisecond wait as something to idle through. If so this is
            // the mode a dimming app would have to use.
            if (sleepMs == nullptr) {
                return 0u;
            }
            while ((nowMs() - t0) < windowMs) {
                sleepMs(1u);
            }
            break;

        case Idle::LongSleep:
            if (sleepMs == nullptr) {
                return 0u;
            }
            sleepMs(windowMs);
            break;
    }

    const uint32_t elapsed  = nowMs() - t0;
    const uint32_t brcAfter = (rd(cb + kCbr1) >> kBrcShift) & kBrcMax;

    if (elapsed == 0u || brcAfter > brcBefore) {
        // A counter that went up means it was re-armed underneath us, so the
        // window is uninterpretable. Report nothing rather than a wrong number.
        return 0u;
    }

    const uint32_t passes = brcBefore - brcAfter;
    return (passes * 1000u) / elapsed;
}

bool DmaPwm::pickChannel()
{
    // Highest-numbered idle channel, on the theory that a kernel allocating
    // channels starts at zero. Idle means EN clear and IDLEF set.
    for (uint8_t ch = kChannelCount; ch-- > 0;) {
        const uint32_t base = channelBase(ch);
        if ((rd(base + kCcr) & kCcrEn) != 0u) {
            continue;
        }
        if ((rd(base + kCsr) & kCsrIdlef) == 0u) {
            continue;
        }
        mChannel = ch;
        return true;
    }
    return false;
}

bool DmaPwm::verify(uint32_t periodUs) const
{
    (void)periodUs;
    const uint32_t cb = channelBase(mChannel);

    // The one that matters most: a wrong destination writes into memory.
    if (rd(cb + kCdar) != kGpiofBsrr) {
        LOG_INFO("verify: CDAR is %08lX, expected %08lX\n",
                 static_cast<unsigned long>(rd(cb + kCdar)),
                 static_cast<unsigned long>(kGpiofBsrr));
        return false;
    }
    if (rd(cb + kCsar) != reinterpret_cast<uintptr_t>(gWave)) {
        LOG_INFO("verify: CSAR mismatch\n");
        return false;
    }
    if ((rd(cb + kCbr1) & 0xFFFFu) != kBlockBytes) {
        LOG_INFO("verify: BNDT is %lu, expected %lu\n",
                 static_cast<unsigned long>(rd(cb + kCbr1) & 0xFFFFu),
                 static_cast<unsigned long>(kBlockBytes));
        return false;
    }
    if ((rd(cb + kCtr2) & 0x7Fu) != timerRequest(mTimerIndex)) {
        LOG_INFO("verify: REQSEL mismatch\n");
        return false;
    }
    if ((rd(cb + kCtr1) & kDinc) != 0u) {
        LOG_INFO("verify: DINC set, destination would walk through memory\n");
        return false;
    }
    if ((rd(cb + kCcr) & kCcrEn) != 0u) {
        LOG_INFO("verify: channel already enabled before it should be\n");
        return false;
    }
    return true;
}

DmaStatus DmaPwm::start(uint8_t dutyPercent, uint32_t periodUs, void (*sleepMs)(uint32_t),
                        uint32_t (*nowMs)(), void (*yieldNow)())
{
    if (!kHasRegisters) {
        mStatus = DmaStatus::NoRegisters;
        return mStatus;
    }

    mSavedAhb1Enr  = rd(kRccAhb1Enr);
    mSavedApb1Enr1 = rd(kRccApb1Enr1);

    if (!pickTimer()) {
        LOG_INFO("no basic timer free: APB1ENR1 = %08lX\n",
                 static_cast<unsigned long>(mSavedApb1Enr1));
        mStatus = DmaStatus::NoFreeTimer;
        return mStatus;
    }

    // Clocks on. Saved above, restored by stop().
    wr(kRccAhb1Enr, mSavedAhb1Enr | 1u);
    wr(kRccApb1Enr1, mSavedApb1Enr1 | timerEnableBit(mTimerIndex));
    mTouchedClocks = true;

    if (!pickChannel()) {
        LOG_INFO("no idle GPDMA channel\n");
        stop();
        mStatus = DmaStatus::NoFreeChannel;
        return mStatus;
    }

    buildWave(dutyPercent);

    const uint32_t tb = timerBase(mTimerIndex);
    const uint32_t cb = channelBase(mChannel);

    // Timer: one update event per waveform word. The divider here is only a
    // starting guess; it is trimmed from measurement once the waveform is
    // actually running, because computing it from a clock is exactly what went
    // wrong twice.
    uint32_t guess = (kGuessTimerHz / 1000000u) * periodUs / kWaveWords;
    if (guess < 1u) {
        guess = 1u;
    }
    if (guess > kArrMax) {
        guess = kArrMax;
    }
    mArr = guess;
    wr(tb + kTimCr1, 0u);
    wr(tb + kTimPsc, 0u);
    wr(tb + kTimArr, mArr - 1u);
    wr(tb + kTimEgr, 1u);                 // UG: latch PSC/ARR
    wr(tb + kTimDier, 1u << 8);           // UDE: update generates a DMA request

    // Channel, still disabled.
    wr(cb + kCcr, kCcrReset);
    wr(cb + kCfcr, 0xFFFFu);              // clear stale flags
    wr(cb + kCtr1, kSdwWord | kSinc | kDdwWord); // DINC deliberately clear
    wr(cb + kCtr2, (timerRequest(mTimerIndex) << kReqSelShift));
    wr(cb + kCbr1, kBlockBytes | (kBrcMax << kBrcShift) | kBrsdec);
    wr(cb + kCsar, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gWave)));
    wr(cb + kCdar, kGpiofBsrr);
    wr(cb + kCbr2, kBlockBytes);          // rewind the source each block repeat
    wr(cb + kCllr, 0u);                   // no linked list
    mConfigured = true;

    if (!verify(periodUs)) {
        stop();
        mStatus = DmaStatus::VerifyFailed;
        return mStatus;
    }

    wr(cb + kCcr, rd(cb + kCcr) | kCcrEn);
    wr(tb + kTimCr1, 1u);                 // CEN

    // The waveform is now running at whatever rate the guess produced. Measure
    // it, twice, and let the difference between the two answer a question no
    // amount of register reading can: whether this keeps running when the core
    // does not.
    mStatus = DmaStatus::Running;

    mFirst.arr = mArr;
    mFirst.hz[static_cast<size_t>(Idle::Spin)] =
        measureHz(Idle::Spin, kWindowMs, sleepMs, nowMs, yieldNow);

    if (mFirst.spin() < kMinHz) {
        LOG_INFO("waveform did not advance at arr %lu\n",
                 static_cast<unsigned long>(mFirst.arr));
        stop();
        mStatus = DmaStatus::NoTimerClock;
        return mStatus;
    }

    // Rate is inversely proportional to the divider, so one multiply lands on
    // the target from any starting point. Done twice because the first landing
    // is only as good as the first measurement's resolution.
    const uint32_t targetHz = periodUs ? (1000000u / periodUs) : 250u;
    uint32_t measured = mFirst.spin();
    for (int pass = 0; pass < 2 && measured >= kMinHz; ++pass) {
        setArr(trimmedArr(mArr, measured, targetHz));
        mFinal.arr = mArr;
        mFinal.hz[static_cast<size_t>(Idle::Spin)] =
            measureHz(Idle::Spin, kWindowMs, sleepMs, nowMs, yieldNow);
        measured = mFinal.spin();
    }

    // Every way of being idle, at the settled divider. Measured here rather than
    // at each trim step because it is a property of the kernel, not of the
    // divider, and one set of windows is enough to read it off.
    static const Idle kOthers[] = {Idle::Yield, Idle::ShortNaps, Idle::LongSleep};
    for (Idle mode : kOthers) {
        mFinal.hz[static_cast<size_t>(mode)] =
            measureHz(mode, kWindowMs, sleepMs, nowMs, yieldNow);
    }

    // Cheapest mode that keeps up, checked from the cheapest end. Spin is the
    // fallback, and having to fall back to it is the finding: it would mean the
    // waveform cannot outlive the core going idle by any means available here.
    mBestIdle = Idle::Spin;
    static const Idle kByCost[] = {Idle::LongSleep, Idle::ShortNaps, Idle::Yield};
    for (Idle mode : kByCost) {
        if (keepsUp(mFinal.hz[static_cast<size_t>(mode)], mFinal.spin())) {
            mBestIdle = mode;
            break;
        }
    }

    LOG_INFO("running: TIM%u ch%u duty %u%% arr %lu, %lu Hz spinning, idle mode %s\n",
             static_cast<unsigned>(mTimerIndex), static_cast<unsigned>(mChannel),
             static_cast<unsigned>(mDuty), static_cast<unsigned long>(mArr),
             static_cast<unsigned long>(mFinal.spin()), idleName(mBestIdle));

    return mStatus;
}

void DmaPwm::setDuty(uint8_t dutyPercent)
{
    // Only the buffer changes. The DMA is reading it continuously, so the new
    // duty takes effect within one pass with no reconfiguration and no glitch
    // beyond a single partly-old pass.
    buildWave(dutyPercent);
}

void DmaPwm::poll()
{
    if (mStatus != DmaStatus::Running || !kHasRegisters) {
        return;
    }

    const uint32_t cb = channelBase(mChannel);

    // Block-repeat is 11 bits, so the transfer eventually completes. Re-arm it
    // when it does. This is the only thing the CPU does while a waveform runs,
    // and it happens roughly once every ten seconds.
    if ((rd(cb + kCsr) & kCsrIdlef) == 0u) {
        return;
    }

    rearm();
    ++mRearms;
}

void DmaPwm::stop()
{
    if (!kHasRegisters) {
        mStatus = DmaStatus::Idle;
        return;
    }

    if (mConfigured && mChannel != 0xFF) {
        const uint32_t cb = channelBase(mChannel);
        wr(cb + kCcr, rd(cb + kCcr) & ~kCcrEn);
        // Wait for the engine to stop before touching anything else. Bounded:
        // this is a hardware idle flag, not a message queue, and it settles in
        // microseconds.
        for (uint32_t i = 0; i < 100000u; ++i) {
            if ((rd(cb + kCsr) & kCsrIdlef) != 0u) {
                break;
            }
        }
        wr(cb + kCcr, kCcrReset);
        wr(cb + kCfcr, 0xFFFFu);
    }

    if (mTimerIndex == 6u || mTimerIndex == 7u) {
        const uint32_t tb = timerBase(mTimerIndex);
        wr(tb + kTimCr1, 0u);
        wr(tb + kTimDier, 0u);
    }

    // The light off, explicitly, before the pin goes back to the kernel: the DMA
    // may have stopped on either half of the waveform.
    wr(kGpiofBsrr, kBs3);

    if (mTouchedClocks) {
        wr(kRccApb1Enr1, mSavedApb1Enr1);
        wr(kRccAhb1Enr, mSavedAhb1Enr);
        mTouchedClocks = false;
    }

    mConfigured = false;
    mChannel    = 0xFF;
    mStatus     = DmaStatus::Idle;
}

} // namespace Pwm
