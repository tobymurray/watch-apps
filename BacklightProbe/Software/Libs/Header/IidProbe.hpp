/**
 ******************************************************************************
 * @file    IidProbe.hpp
 * @brief   Walk the unallocated interface-ID range and see what answers.
 ******************************************************************************
 *
 * `IKIP::IntfID` numbers its interfaces `0x00010000` (System), `0x00020000`
 * (Logger), `0x00030000` (AppMemory), `0x00040000` (AppComm) and then jumps to
 * `0x000B0000` (FileSystem). Six identifiers, `0x00050000` through
 * `0x000A0000`, are unaccounted for in a header that otherwise numbers densely.
 *
 * `IBacklight` is declared in the same SDK and has no ID at all, so the obvious
 * question is whether one of those six returns it. That is Q7.
 *
 * ## Calibrate the expectation downwards before running this
 *
 * The SDK declares at least eight kernel-service-shaped interfaces with no ID:
 * `IBacklight`, `IBuzzer`, `IVibro`, `ISettings`, `ITime`, `IMutex`,
 * `ISemaphore`, `ISensorData`. Eight claimants, six slots. There is no
 * particular reason to expect the backlight to be among them, and the one
 * structural check available argues against reading the gap as an actuator
 * range: if the IID space paralleled the `0x02xx` message space, `0x00010000`
 * would be capabilities rather than System, and it is not.
 *
 * So the honest expected outcome is six null pointers, and that is a result
 * worth having; it closes Q7 and it is exactly the sort of negative that
 * otherwise gets re-investigated a year later by someone who assumes nobody
 * checked.
 *
 * ## It does not call through anything it finds
 *
 * A non-null pointer is logged and nothing else. Calling a virtual on a pointer
 * whose type is a guess is how you turn a curiosity into a HardFault: the vtable
 * layout of whatever actually lives there is unknown, and `IBacklight::on` is at
 * slot 0 only if the thing is an `IBacklight`.
 *
 * The right follow-up is Phase C; recover `queryInterface`'s switch from the
 * firmware image and read off what each accepted ID actually returns, and only
 * then call anything. This probe exists to tell you whether that follow-up is
 * worth doing at all.
 *
 ******************************************************************************
 */

#ifndef IID_PROBE_HPP
#define IID_PROBE_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

namespace IidProbe
{

/// The gap, inclusive: 0x00050000 to 0x000A0000 in steps of 0x00010000.
constexpr uint32_t kFirst = 0x00050000u;
constexpr uint32_t kLast  = 0x000A0000u;
constexpr uint32_t kStep  = 0x00010000u;
constexpr size_t   kCount = ((kLast - kFirst) / kStep) + 1u;

/// What one identifier answered.
struct Answer {
    uint32_t iid     = 0;
    bool     nonNull = false;

    /// The pointer itself, as an integer, so it can be written to the results
    /// file and compared against the kernel's own address space. Never
    /// dereferenced, never called.
    uintptr_t value = 0;
};

struct Result {
    Answer answers[kCount];

    /// How many of the six returned something. Zero closes Q7.
    size_t nonNullCount = 0;

    /// False on a build where `queryInterface` is the simulator's own, which
    /// answers only the five IDs it implements and tells you nothing about the
    /// device kernel.
    bool meaningful = false;
};

/**
 * @brief Query all six unallocated identifiers.
 *
 * Logs each. Calls nothing through any pointer it gets back.
 */
Result run(SDK::Interface::IKIP& kip);

} // namespace IidProbe

#endif // IID_PROBE_HPP
