/**
 ******************************************************************************
 * @file    Commands.hpp
 * @brief   Message contract between the MapManager service and its GUI.
 ******************************************************************************
 *
 * One message carries a full progress snapshot, service to GUI, sent
 * periodically while a scan is in progress (throttled -- see Service.cpp)
 * and once on request so the GUI gets an immediate paint on open instead of
 * waiting for the next tick. There is nothing for the GUI to command the
 * service to do: verification is autonomous from boot, not user-triggered.
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
constexpr SDK::MessageType::Type MAP_MANAGER_PROGRESS = 0x00000001;

// GUI --> Service
constexpr SDK::MessageType::Type MAP_MANAGER_REQUEST = 0x00000002;

/// Longest filename this can carry without truncation -- comfortably above
/// any realistic pack filename (e.g. "athens.rawtiles" is 16 bytes).
constexpr size_t kMaxPackNameLen = 64;

/**
 * @brief Snapshot of the currently-scanning (or most recently finished) pack.
 */
struct MapManagerProgress : public SDK::MessageBase {
    char     packName[kMaxPackNameLen];
    uint64_t bytesDone;
    uint64_t bytesTotal;
    uint32_t elapsedMs;
    uint16_t packsVerified;
    uint16_t packsTotal;
    bool     anyInProgress; ///< false: idle -- nothing scanning right now (no packs found, or all done).

    MapManagerProgress()
        : SDK::MessageBase(MAP_MANAGER_PROGRESS)
        , packName{}
        , bytesDone(0)
        , bytesTotal(0)
        , elapsedMs(0)
        , packsVerified(0)
        , packsTotal(0)
        , anyInProgress(false)
    {
    }
};

// The fixed-size name buffer keeps this inside the 256-byte pool block, the
// largest the kernel offers, so the path stays allocation-free.
static_assert(sizeof(MapManagerProgress) <= 256,
              "MapManagerProgress must fit the largest kernel message pool block");

/**
 * @brief Ask the service to reply with the current progress snapshot.
 */
struct MapManagerRequest : public SDK::MessageBase {
    MapManagerRequest() : SDK::MessageBase(MAP_MANAGER_REQUEST) {}
};

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
