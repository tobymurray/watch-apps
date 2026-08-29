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
constexpr uint32_t kTimPsc  = 0x28u;
constexpr uint32_t kTimArr  = 0x2Cu;

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

const char* dmaStatusName(DmaStatus s)
{
    switch (s) {
        case DmaStatus::Idle:          return "idle";
        case DmaStatus::Running:       return "running";
        case DmaStatus::NoRegisters:   return "no registers on this build";
        case DmaStatus::NoFreeTimer:   return "no free timer";
        case DmaStatus::NoFreeChannel: return "no free DMA channel";
        case DmaStatus::VerifyFailed:  return "register readback mismatch, nothing enabled";
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

DmaStatus DmaPwm::start(uint8_t dutyPercent, uint32_t periodUs)
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

    // Timer: one update event per waveform word. The timer never reaches a pin.
    // Its input clock is not known precisely here, so the divider is derived from
    // the measured core clock by the caller and passed in as periodUs; a basic
    // timer on APB1 runs at the APB1 timer clock, which on this part tracks the
    // core clock. The ARR below is therefore approximate, and the waveform's
    // exact frequency matters far less than its duty: a few percent of frequency
    // error is invisible, where a few percent of duty error is the measurement.
    const uint32_t ticksPerWord = (periodUs * 160u) / kWaveWords; // ~160 MHz
    wr(tb + kTimCr1, 0u);
    wr(tb + kTimPsc, 0u);
    wr(tb + kTimArr, ticksPerWord ? ticksPerWord - 1u : 1u);
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

    LOG_INFO("running: TIM%u, GPDMA ch%u, %u words, duty %u%%\n",
             static_cast<unsigned>(mTimerIndex), static_cast<unsigned>(mChannel),
             static_cast<unsigned>(kWaveWords), static_cast<unsigned>(mDuty));

    mStatus = DmaStatus::Running;
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

    wr(cb + kCcr, rd(cb + kCcr) & ~kCcrEn);
    wr(cb + kCfcr, 0xFFFFu);
    wr(cb + kCbr1, kBlockBytes | (kBrcMax << kBrcShift) | kBrsdec);
    wr(cb + kCsar, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gWave)));
    wr(cb + kCdar, kGpiofBsrr);
    wr(cb + kCcr, rd(cb + kCcr) | kCcrEn);
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
