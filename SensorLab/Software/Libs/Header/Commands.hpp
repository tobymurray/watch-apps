/**
 ******************************************************************************
 * @file    Commands.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The messages between SensorLab's Service and its GUI.
 ******************************************************************************
 *
 * The kernel's largest message-pool block is 256 bytes (ledger row P10) and
 * `MessageBase` is 40 of them on a 64-bit host and 32 on the watch (row P13),
 * so every message here is packed and size-asserted against the ceiling. An
 * oversized message is not a compile error by itself -- it silently falls out
 * of the pool at runtime -- so the static_assert is the only thing that catches
 * it.
 *
 * ---------------------------------------------------------------------------
 * A 37-row roster does not fit one message, and here is the decision
 *
 * Three options were available and the reason for the choice matters more than
 * the choice:
 *
 *  1. **The GUI reads `profile.json` itself.** Rejected. The roster has to be
 *     current while a run is *in progress*, and the profile is written at the
 *     end of one. A screen drawn from the last completed run would show the
 *     previous firmware's answers while the current one was being measured,
 *     which is the single most misleading thing this app could do.
 *
 *  2. **One message per row.** Rejected: 37 pool blocks per refresh, on a
 *     device whose pool the service is also using for sensor batches at 308
 *     batches a minute (ledger row S17).
 *
 *  3. **An indexed burst.** Chosen. Fourteen rows a message, three messages,
 *     each carrying its own start index so the GUI reassembles by position
 *     rather than by arrival order -- which matters because a burst that lost
 *     its middle message must show a gap rather than a shifted roster.
 *
 * SleepLab's history burst is the same pattern and its ledger row T2 is why the
 * indices are explicit: a burst contract that relies on ordering fails silently
 * and looks like data.
 *
 * ---------------------------------------------------------------------------
 * The GUI's queue holds ten, and drops the oldest
 *
 * `SDK::TouchGFXCommandProcessor` keeps app-specific messages in a
 * `FixedQueue<MessageBase*, 10>` and, when it is full, **discards the oldest**
 * with a warning. One publish here is four messages -- a status plus three
 * roster bursts -- so at most two publishes can be in flight before the GUI
 * starts losing the earliest of them.
 *
 * Measured, not assumed: a simulator run produced
 * `TouchGFXCommandProcessor::waitForFrameTick: Queue for custom messages is
 * full` when the GUI's `onStart` and `onResume` each asked for an update inside
 * one frame. The service therefore rate-limits publishing (`kPublishMinGapMs`
 * in Service.cpp) rather than trusting callers to ask politely, because the
 * message that gets dropped is a roster burst and a roster with a missing burst
 * is exactly the silent-partial-data failure this contract's explicit indices
 * exist to prevent.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_COMMANDS_HPP
#define SENSORLAB_COMMANDS_HPP

#include <cstdint>

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#pragma pack(push, 4)

namespace CustomMessage
{

/// Service --> GUI: run state, completeness and the manifest's primary key.
constexpr SDK::MessageType::Type SENSORLAB_STATUS  = 0x00000001;
/// Service --> GUI: fourteen roster rows, indexed.
constexpr SDK::MessageType::Type SENSORLAB_ROSTER  = 0x00000002;
/// GUI --> Service: publish now rather than at the next tick.
constexpr SDK::MessageType::Type SENSORLAB_REQUEST = 0x00000003;
/// GUI --> Service: start or stop a sweep.
constexpr SDK::MessageType::Type SENSORLAB_COMMAND = 0x00000004;

/// What the service is doing. Drawn on the screen, and written into the run
/// manifest so a profile says which phase produced each claim.
enum class Phase : uint8_t
{
    /// Reading settings, resolving the manifest. Nothing measured yet.
    Starting = 0,
    /// Layer 1: the 37-type existence sweep. Seconds.
    Existence,
    /// Layers 2 and 3: subscribed, counting what arrives.
    Liveness,
    /// Layers 4 and 5: a long unattended run accumulating distributions.
    Soak,
    /// Idle with a profile written. The resting state.
    Idle,
    /// The run ended before it finished. Marked, never silently completed.
    Truncated,
};

/// What the GUI can ask for. Deliberately short: this app's buttons start and
/// stop runs and scroll a list, and every additional verb is another way to
/// lose a measurement.
enum class Command : uint8_t
{
    None = 0,
    /// Re-run layer 1 and write the profile. Cheap, seconds, safe any time.
    RunExistenceSweep,
    /// Subscribe every type that resolved and accumulate layers 2-5 until
    /// stopped. This is the run that must not be interrupted by USB.
    StartSoak,
    /// Close the current run as completed and write the profile.
    StopRun,
};

/// One roster row: everything the screen shows about one sensor type.
///
/// Twelve bytes, which is what makes fourteen of them fit a pool block. Every
/// field is a measurement or a verdict -- nothing derived, because a GUI that
/// derived one of these could disagree with the profile.
struct RosterRow
{
    /// Index into `Catalogue::kTypes`, not the type value: the value needs 32
    /// bits and the GUI has the same generated table the service does.
    uint8_t  typeIdx      = 0;

    /// Bit flags. `kResolved` is the distinction that caught two of the
    /// ledger's most consequential rows in two minutes of hardware time, so it
    /// is a bit of its own rather than inferred from a sample count.
    static constexpr uint8_t kResolved      = 1u << 0; ///< RequestDefault gave a handle.
    static constexpr uint8_t kConnected     = 1u << 1; ///< RequestConnect succeeded.
    static constexpr uint8_t kEverDelivered = 1u << 2; ///< At least one sample arrived.
    static constexpr uint8_t kNoParser      = 1u << 3; ///< The SDK ships none.
    static constexpr uint8_t kFrameDiffers  = 1u << 4; ///< Delivered width != parser's.
    static constexpr uint8_t kAsked         = 1u << 5; ///< The run asked for this type.
    uint8_t  flags        = 0;

    /// `Stats::Cadence`, measured rather than assumed.
    uint8_t  cadence      = 0;
    /// Delivered field count, from the batch stride. 0 = never delivered.
    uint8_t  fieldCount   = 0;

    /// Samples a minute, x10, saturating at 65535 (= 6553.5/min). The
    /// accelerometer's measured ~48 Hz is 28 800/min, so this saturates for the
    /// fastest stream -- and the screen shows a rate to one decimal for the slow
    /// ones, which is where a rate is actually ambiguous. The profile carries
    /// the unsaturated figure; this is the glance.
    uint16_t samplesPerMinX10 = 0;

    /// Longest gap between samples, in seconds, saturating. **Never shown
    /// without the rate and never shown as an average**: a sensor delivering
    /// its nominal rate in two bursts an hour apart is not delivering at that
    /// rate.
    uint16_t longestGapS  = 0;

    /// Completeness for this sensor across every layer, 0-100.
    uint8_t  completePct  = 0;
    /// `RequestList`'s handle count. Nobody has ever seen this answer; more
    /// than one driver for a type would be news.
    uint8_t  driverCount  = 0;
    /// Claims for this sensor with verdict REFUTED. Drawn as its own marker,
    /// because a refuted claim is the most valuable row in the document and a
    /// completeness percentage hides it.
    uint8_t  refutedCount = 0;
    uint8_t  reserved     = 0;
};

static_assert(sizeof(RosterRow) == 12, "RosterRow sizing is what makes the burst fit");

/// Rows per burst message. Fourteen: 14 x 12 + 4 header bytes + MessageBase's
/// 40 is 212, inside the 256-byte pool block with room for the block's own
/// bookkeeping.
constexpr uint8_t kRosterRowsPerMsg = 14;

/**
 * @brief Service --> GUI. A slice of the roster, by index.
 */
struct SensorRoster : public SDK::MessageBase
{
    /// Index of `rows[0]` in the full roster.
    uint8_t   first = 0;
    /// Rows populated in this message.
    uint8_t   count = 0;
    /// Rows in the full roster, so the GUI can size its list before the last
    /// burst arrives and can tell a missing burst from a short roster.
    uint8_t   total = 0;
    uint8_t   reserved = 0;
    RosterRow rows[kRosterRowsPerMsg] {};

    SensorRoster() : SDK::MessageBase(SENSORLAB_ROSTER) {}
};

/**
 * @brief What the status screen shows. A plain struct, deliberately.
 *
 * `SDK::MessageBase` deletes copy-construction and copy-assignment -- correct
 * behaviour, since a message is a pooled block with an identity and copying one
 * would produce a second object claiming the same slot. SleepLab's ledger row
 * P12 is the build failure that established this, and the fix here is the same:
 * a payload anything wants to **keep** is a separate plain struct.
 *
 * Two things keep one. The GUI, because the message goes back to the kernel's
 * pool the moment the handler returns; and the test harness, which collects a
 * vector of them so an assertion can be made about what a person would have
 * been shown.
 *
 * The firmware and hardware strings are here rather than only in the manifest
 * because they are the profile's primary key: a reader looking at a roster has
 * to be able to see which firmware produced it without opening a file. A
 * profile whose firmware version is unknown cannot be diffed, and a profile
 * that cannot be diffed answers none of the questions this app exists for.
 */
struct SensorLabStatusData
{
    uint32_t runId          = 0;
    uint32_t runningMs      = 0;   ///< Uptime since this run opened.
    uint32_t rowsWritten    = 0;   ///< Sample-log rows appended this run.
    uint32_t rowFailures    = 0;
    uint32_t bytesWritten   = 0;
    uint32_t samplesSeen    = 0;   ///< Across every subscribed type.
    uint32_t batchesSeen    = 0;

    uint16_t typesAsked     = 0;
    uint16_t typesResolved  = 0;
    uint16_t typesDelivering = 0;

    /// Claims, over the whole catalogue. Completeness is displayed alongside
    /// every result on every screen: a screen that shows findings without
    /// showing how much is missing reads as finished.
    uint16_t claimsApplicable = 0;
    uint16_t claimsAnswered   = 0;
    uint16_t claimsConfirmed  = 0;
    uint16_t claimsRefuted    = 0;

    uint8_t  phase          = 0;   ///< `Phase`.
    /// 1 charging, 0 not, -1 unknown. On the cable is not a detail: plugging in
    /// terminates every running app, so a soak on the charger records nothing.
    int8_t   charging       = -1;
    int8_t   usb            = -1;
    /// True once the kernel answered `RequestSystemInfo`. Distinguishes "the
    /// kernel does not implement it" from "it said 1.4.0", and only one of
    /// those makes the profile undiffable.
    uint8_t  haveSystemInfo = 0;

    /// From `RequestSystemInfo`. 16 bytes each, the message's own width.
    char     firmware[16] {};
    char     hardware[16] {};

    /// False until the first snapshot lands. "Nothing has been heard yet" and
    /// "the service says nothing is arriving" must not look alike: only one of
    /// them means the instrument is broken.
    bool     everReceived   = false;
};

/// Service --> GUI. The message wrapper around the payload above.
struct SensorLabStatus : public SDK::MessageBase
{
    SensorLabStatusData data {};

    SensorLabStatus() : SDK::MessageBase(SENSORLAB_STATUS) {}
};

/// GUI --> Service. Carries nothing; the reply is a status plus a roster burst.
struct SensorLabRequest : public SDK::MessageBase
{
    SensorLabRequest() : SDK::MessageBase(SENSORLAB_REQUEST) {}
};

/// GUI --> Service. One verb.
struct SensorLabCommand : public SDK::MessageBase
{
    uint8_t command = 0;   ///< `Command`.
    uint8_t reserved[3] {};

    SensorLabCommand() : SDK::MessageBase(SENSORLAB_COMMAND) {}
};

static_assert(sizeof(SensorRoster)     <= 256, "SensorRoster must fit the largest pool block");
static_assert(sizeof(SensorLabStatus)  <= 256, "SensorLabStatus must fit the largest pool block");
static_assert(sizeof(SensorLabRequest) <= 256, "SensorLabRequest must fit the largest pool block");
static_assert(sizeof(SensorLabCommand) <= 256, "SensorLabCommand must fit the largest pool block");

} // namespace CustomMessage

#pragma pack(pop)

#endif // SENSORLAB_COMMANDS_HPP
