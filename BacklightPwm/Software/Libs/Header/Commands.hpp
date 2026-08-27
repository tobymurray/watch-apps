/**
 ******************************************************************************
 * @file    Commands.hpp
 * @brief   Message contract between the BacklightPwm service and its GUI.
 ******************************************************************************
 *
 * The same two-message shape `FwDump` and `BacklightProbe` use: one status
 * snapshot out, one command in.
 *
 * There is no GUI-send path here, unlike `BacklightProbe`. That app needed one
 * because it was testing whether the *kernel* treats the two processes
 * differently. This one writes a register directly, and a register does not care
 * which thread wrote it.
 *
 ******************************************************************************
 */

#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#pragma pack(push, 4)

namespace CustomMessage
{

// Service --> GUI
constexpr SDK::MessageType::Type PWM_STATUS = 0x00000001;

// GUI --> Service
constexpr SDK::MessageType::Type PWM_COMMAND = 0x00000002;

constexpr size_t kLabelMax = 24;

enum class PwmState : uint8_t {
    Idle        = 0, ///< Waiting to be told to start.
    Running     = 1, ///< Climbing the ladder.
    Done        = 2, ///< Finished, pin handed back.
    Refused     = 3, ///< Will not run: no registers, or the cycle counter is dead.

    /// Measuring the core clock. Its own state because it blocks for a tenth of
    /// a second, and the first version of this app sat on READY through that
    /// with no way to tell whether the button had been seen at all.
    Calibrating = 4,
};

enum class Command : uint8_t {
    Start  = 0,
    Stop   = 1, ///< Give the pin back now. The one control that matters mid-run.
    Resend = 2,
};

/**
 * @brief One snapshot of the run.
 *
 * `achievedDuty` is separate from `requestedDuty` on purpose. A busy-wait PWM
 * competing with a message loop does not hit its target exactly, and the gap is
 * the honest measure of how good this technique is. A screen that showed only
 * the request would be reporting an intention as a measurement.
 */
struct PwmStatus : public SDK::MessageBase {
    uint32_t elapsedMs;      ///< In the current rung.
    uint32_t holdMs;         ///< How long this rung lasts.
    uint32_t edges;          ///< Pin writes issued so far, across the whole run.
    uint32_t periods;        ///< Complete PWM periods emitted so far.
    uint32_t cyclesPerUs;    ///< Measured core clock. 0 if calibration failed.

    uint16_t rungIndex;
    uint16_t rungCount;

    uint8_t requestedDuty;
    uint8_t achievedDuty;    ///< From microseconds actually spent on.
    uint8_t state;           ///< A PwmState.

    /// False when the app declined to drive anything. The screen must say so
    /// rather than showing a plausible-looking ladder that never touched a pin.
    bool driving;

    char label[kLabelMax];

    PwmStatus()
        : SDK::MessageBase(PWM_STATUS)
        , elapsedMs(0)
        , holdMs(0)
        , edges(0)
        , periods(0)
        , cyclesPerUs(0)
        , rungIndex(0)
        , rungCount(0)
        , requestedDuty(0)
        , achievedDuty(0)
        , state(static_cast<uint8_t>(PwmState::Idle))
        , driving(false)
        , label{}
    {
    }
};

static_assert(sizeof(PwmStatus) <= 256,
              "PwmStatus must fit the largest kernel message pool block");

struct PwmCommand : public SDK::MessageBase {
    uint8_t command;

    PwmCommand()
        : SDK::MessageBase(PWM_COMMAND)
        , command(static_cast<uint8_t>(Command::Resend))
    {
    }
};

static_assert(sizeof(PwmCommand) <= 256,
              "PwmCommand must fit the largest kernel message pool block");

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
