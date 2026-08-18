/**
 ******************************************************************************
 * @file    Commands.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The two messages between the probe's Service and its GUI.
 ******************************************************************************
 *
 * The kernel's largest message-pool block is 256 bytes, so every custom
 * message here is packed and size-asserted against that ceiling. A message
 * that overflows it is not a compile error by itself -- it silently falls out
 * of the pool at runtime -- so the static_assert is the only thing that
 * catches it.
 *
 ******************************************************************************
 */

#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include <cstdint>

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#pragma pack(push, 4)

namespace CustomMessage {

/// Service --> GUI: everything the status screen shows.
constexpr SDK::MessageType::Type PROBE_STATUS  = 0x00000001;
/// GUI --> Service: send one now, rather than waiting for the next row.
constexpr SDK::MessageType::Type PROBE_REQUEST = 0x00000002;

/// Which sensors resolved a driver at connect time. A bitmask rather than
/// thirteen bools because it has to cross an IPC boundary and be legible in a
/// log line as one hex number.
namespace Sub {
    constexpr uint16_t kAccel       = 1u << 0;
    constexpr uint16_t kTouch       = 1u << 1;
    constexpr uint16_t kMotion      = 1u << 2;
    constexpr uint16_t kActivity    = 1u << 3;
    constexpr uint16_t kHr          = 1u << 4;
    constexpr uint16_t kHrEx        = 1u << 5;
    constexpr uint16_t kBeat        = 1u << 6;
    constexpr uint16_t kPpg         = 1u << 7;
    constexpr uint16_t kSpo2        = 1u << 8;
    constexpr uint16_t kSteps       = 1u << 9;
    constexpr uint16_t kBattLevel   = 1u << 10;
    constexpr uint16_t kBattCharge  = 1u << 11;
    constexpr uint16_t kBattMetrics = 1u << 12;
} // namespace Sub

/**
 * @brief Service --> GUI. What the run has managed so far.
 *
 * This screen exists for one moment: the thirty seconds before you go to bed,
 * when the only question is "is this actually going to record anything?". So
 * it carries what answers that -- which sensors resolved, whether rows are
 * reaching storage, and whether the last minute delivered samples -- and not
 * a summary of the night, which is the host script's job.
 */
struct ProbeStatus : public SDK::MessageBase {
    uint32_t rowsWritten   = 0;   ///< Rows this launch has appended.
    uint32_t rowFailures   = 0;   ///< Rows that did not reach storage.
    uint32_t bytesWritten  = 0;   ///< Bytes this launch has appended.
    uint32_t runningMs     = 0;   ///< Uptime since this launch started.

    uint16_t subscribed    = 0;   ///< Bitmask of Sub::*.
    uint16_t hrMode        = 0;   ///< Probe::HrMode, as its underlying value.

    /// Counts from the last completed row, so "nothing is arriving" is visible
    /// without waiting for the file to be read on a host.
    int32_t  lastAccN      = -1;
    int32_t  lastHrN       = -1;
    int32_t  lastTouchWorn = -1;  ///< Worn samples in the last row.
    int32_t  lastTouchN    = -1;  ///< Total touch samples in the last row.

    /// Cumulative across the launch, because both are yes/no questions whose
    /// answer is "did this *ever* happen" rather than "is it happening now".
    uint32_t totalBeatN    = 0;   ///< HEART_BEAT events. Expected: 0.
    uint32_t totalSpo2N    = 0;   ///< SPO2 samples. Expected: unknown.

    int32_t  battPctX10    = -1;
    int8_t   charging      = -1;
    int8_t   usb           = -1;

    ProbeStatus() : SDK::MessageBase(PROBE_STATUS) {}
};

/// GUI --> Service. Carries nothing: the reply is a ProbeStatus.
struct ProbeRequest : public SDK::MessageBase {
    ProbeRequest() : SDK::MessageBase(PROBE_REQUEST) {}
};

static_assert(sizeof(ProbeStatus)  <= 256, "ProbeStatus must fit the largest pool block");
static_assert(sizeof(ProbeRequest) <= 256, "ProbeRequest must fit the largest pool block");

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
