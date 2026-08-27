/**
 ******************************************************************************
 * @file    Commands.hpp
 * @brief   Message contract between the BacklightProbe service and its GUI.
 ******************************************************************************
 *
 * Two messages, one each way, the same shape FwDump uses:
 *
 *   ProbeStatus  (Service --> GUI); everything the screen draws, in one
 *                snapshot, so it can never render a mixture of two.
 *   ProbeCommand (GUI --> Service); Start, Resend, and the reply to a
 *                GUI-send request.
 *
 * ## Why the GUI ever sends a backlight request
 *
 * `REQUEST_BACKLIGHT_SET` sits immediately below a block of message types
 * commented "Display control (GUI only)". Whether that comment governs the
 * backlight block as well is unverified, and both shipped callers happen to be
 * services, so the question has never been asked. It is cheap to ask: run the
 * same request from the GUI process and compare the result code.
 *
 * The service owns the plan, so when the plan reaches that step the service has
 * to get the GUI to do the sending. That is `ProbeStatus::guiSendSeq`: a
 * sequence number the GUI watches for a change on, plus the parameters. The GUI
 * sends the message and replies with `ProbeCommand::GuiSendResult` carrying what
 * it observed.
 *
 * **The service does not wait for that reply.** It cannot: it is running a
 * timed experiment on a thread that must keep answering the message queue, and a
 * blocking round trip through another process is exactly the kind of thing that
 * trips the app-liveness watchdog. So the request is fired, the plan moves on to
 * an Observe step that gives the reply time to arrive, and the outcome is
 * written into the results file whenever it does. If the GUI is not running,
 * no reply ever comes and the file says so, which is itself the correct record
 * of what happened.
 *
 * The sequence number exists so a stale reply cannot be attributed to a later
 * request. Zero means nothing has been asked.
 *
 ******************************************************************************
 */

#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

// Force 4-byte alignment for all message structures
#pragma pack(push, 4)

namespace CustomMessage
{

// Service --> GUI
constexpr SDK::MessageType::Type PROBE_STATUS = 0x00000001;

// GUI --> Service
constexpr SDK::MessageType::Type PROBE_COMMAND = 0x00000002;

/// Longest step label the screen and the wire carry. The plan's labels are
/// written to fit; anything longer is truncated rather than dropped, because a
/// clipped label still identifies the step and a missing one does not.
constexpr size_t kLabelMax = 48;

/// Where the run has got to.
enum class ProbeState : uint8_t {
    Idle    = 0, ///< Waiting to be told to start.
    Running = 1, ///< Walking the plan.
    Done    = 2, ///< Finished. The files are on disk.
};

/// What the GUI can ask for.
enum class Command : uint8_t {
    Start         = 0, ///< Begin the plan. Ignored if already running.
    Resend        = 1, ///< Re-send the snapshot now, so an opening screen paints.
    GuiSendResult = 2, ///< The reply to a guiSendSeq request.
};

/**
 * @brief One complete snapshot of the run.
 *
 * Carries enough that the GUI recomputes nothing except the millisecond counter,
 * which it must extrapolate between snapshots because the counter has to move
 * faster than the publish rate to be worth filming. That is the one derived
 * value on the screen, and it is derived from `stepElapsedMs` plus the GUI's own
 * tick, never from the difference between two snapshots.
 */
struct ProbeStatus : public SDK::MessageBase {
    uint32_t stepElapsedMs;  ///< In the current step, at the moment of publishing.
    uint32_t stepDurationMs; ///< 0 for steps that are not waits.
    uint32_t startedAtMs;    ///< Kernel uptime when the run began.

    /// Non-zero asks the GUI to send a backlight request itself. The GUI acts
    /// when this differs from the last one it acted on.
    uint32_t guiSendSeq;
    uint32_t guiAutoOffMs;
    uint32_t guiSendTimeoutMs;

    uint16_t stepIndex;
    uint16_t stepCount;

    /// How many Observe steps the plan has, so the screen can say how much of
    /// the answer is going to be on the video rather than in the file.
    uint16_t observeSteps;

    uint8_t guiBrightness;

    uint8_t state;      ///< A ProbeState.
    uint8_t action;     ///< A Probe::Action.
    uint8_t lastResult; ///< SDK::MessageResult of the most recent request.

    /// Whether the screen must hold still. True during a Hold, false during an
    /// Observe. The GUI obeys this rather than deciding for itself, because the
    /// reason for it lives in the plan.
    bool quiet;

    /// False on a build with no peripheral registers, so the screen can say the
    /// run is measuring nothing rather than look identical to a real one.
    bool registersAvailable;

    /// Whether every results-file write so far took the full byte count.
    bool logIntact;

    char label[kLabelMax];

    ProbeStatus()
        : SDK::MessageBase(PROBE_STATUS)
        , stepElapsedMs(0)
        , stepDurationMs(0)
        , startedAtMs(0)
        , guiSendSeq(0)
        , guiAutoOffMs(0)
        , guiSendTimeoutMs(0)
        , stepIndex(0)
        , stepCount(0)
        , observeSteps(0)
        , guiBrightness(0)
        , state(static_cast<uint8_t>(ProbeState::Idle))
        , action(0)
        , lastResult(0)
        , quiet(true)
        , registersAvailable(false)
        , logIntact(true)
        , label{}
    {
    }
};

// The kernel's message pool offers nothing larger than 256 bytes, so a status
// that outgrew it would fail to allocate at runtime rather than at build time.
static_assert(sizeof(ProbeStatus) <= 256,
              "ProbeStatus must fit the largest kernel message pool block");

/**
 * @brief One instruction from the screen, or one reply.
 */
struct ProbeCommand : public SDK::MessageBase {
    /// Echoes ProbeStatus::guiSendSeq. GuiSendResult only.
    uint32_t seq;

    /// How long the GUI's own sendMessage call took. GuiSendResult only.
    uint32_t elapsedMs;

    uint8_t command; ///< A Command.

    /// SDK::MessageResult the GUI observed. GuiSendResult only.
    uint8_t result;

    bool sent;        ///< GuiSendResult only.
    bool allocFailed; ///< GuiSendResult only.
    bool completed;   ///< GuiSendResult only.

    ProbeCommand()
        : SDK::MessageBase(PROBE_COMMAND)
        , seq(0)
        , elapsedMs(0)
        , command(static_cast<uint8_t>(Command::Resend))
        , result(0)
        , sent(false)
        , allocFailed(false)
        , completed(false)
    {
    }
};

static_assert(sizeof(ProbeCommand) <= 256,
              "ProbeCommand must fit the largest kernel message pool block");

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
