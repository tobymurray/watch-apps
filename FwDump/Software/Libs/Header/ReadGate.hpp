/**
 ******************************************************************************
 * @file    ReadGate.hpp
 * @brief   Whether this firmware lets an app read memory without faulting.
 ******************************************************************************
 *
 * A read here is a pointer dereference, and one the hardware refuses raises a
 * fault this app has no handler for, so the question has to be answered before
 * the first read rather than discovered by the crash. Three registers answer
 * it, each readable only once the one before it says so:
 *
 *   1. CONTROL.nPRIV, by MRS, which cannot fault. An unprivileged thread faults
 *      on any access to the System Control Space, so it cannot read MPU_CTRL
 *      and cannot know whether the MPU guards flash.
 *   2. MPU_CTRL.ENABLE, in the System Control Space, which the MPU never
 *      governs. With the MPU on, which regions it allows is in the region
 *      table, and reading that table needs a write to MPU_RNR.
 *   3. FLASH_OPTR.TZEN, a peripheral read that is safe once 2 says the MPU is
 *      off. With TrustZone on, secure flash faults a non-secure read.
 *
 * The rule is on what makes a read safe, never on a firmware version: SPSEL,
 * PRIVDEFENA and every other bit are ignored, so a firmware that changes them
 * is not refused for it. What each firmware actually read is recorded in the
 * README's firmware table, one row per dump_context.txt.
 *
 * Free of SDK types, so it is tested on the host.
 *
 ******************************************************************************
 */

#ifndef READ_GATE_HPP
#define READ_GATE_HPP

#include <cstdint>

namespace ReadGate
{

enum class Verdict : uint8_t {
    Allowed      = 0,
    Unprivileged = 1,
    MpuEnabled   = 2,
    TrustZone    = 3,
};

constexpr bool privileged(uint32_t control) { return (control & 1u) == 0u; }
constexpr bool mpuEnabled(uint32_t mpuCtrl) { return (mpuCtrl & 1u) != 0u; }
constexpr bool trustZone(uint32_t flashOptr) { return ((flashOptr >> 31) & 1u) != 0u; }

constexpr Verdict decide(uint32_t control, uint32_t mpuCtrl, uint32_t flashOptr)
{
    if (!privileged(control)) {
        return Verdict::Unprivileged;
    }
    if (mpuEnabled(mpuCtrl)) {
        return Verdict::MpuEnabled;
    }
    if (trustZone(flashOptr)) {
        return Verdict::TrustZone;
    }
    return Verdict::Allowed;
}

/// The register bit that decided a refusal, as dump_context.txt names it.
constexpr const char* blockingBit(Verdict verdict)
{
    switch (verdict) {
        case Verdict::Allowed:      return "none";
        case Verdict::Unprivileged: return "CONTROL.nPRIV";
        case Verdict::MpuEnabled:   return "MPU_CTRL.ENABLE";
        case Verdict::TrustZone:    return "FLASH_OPTR.TZEN";
    }
    return "unknown";
}

} // namespace ReadGate

#endif // READ_GATE_HPP
