/**
 ******************************************************************************
 * @file    Commands.hpp
 * @brief   Message contract between the MapManager service and its GUI.
 ******************************************************************************
 *
 * Two messages travel service -> GUI, on deliberately different cadences:
 *
 *   MapManagerProgress   -- the pack being scanned right now, its byte
 *                           progress and the aggregate counts. Sent
 *                           periodically while a scan runs (throttled, see
 *                           Service.cpp) because "bytes done" changes
 *                           continuously.
 *   MapManagerPackStatus -- a chunk of the roster: up to kRowsPerMessage
 *                           packs, each with its name and where it has got
 *                           to. Sent as a short burst covering the whole
 *                           roster, and only when the roster or a verdict
 *                           actually changes, because that is rare.
 *
 * Splitting them is not premature structure, it is forced. The largest block
 * the kernel's message pool offers is 256 bytes, and a roster of names cannot
 * fit one: at kMaxPackNameLen a single message holds barely three rows, and a
 * watch can easily carry more packs than that.
 *
 * Why chunks rather than one message per pack, which would be simpler: the
 * GUI's incoming custom-message queue holds ten and is drained once per frame
 * at kFrameRate (10Hz), discarding the *oldest* on overflow. A row-per-message
 * burst at kMaxRosterPacks could therefore never be delivered whole, and even
 * a short one loses its head if two bursts land inside the same 100ms frame --
 * which is exactly what happens at boot, when a screenful of cached markers
 * all resolve at once. Six rows per message keeps a full roster inside three,
 * and Service throttles the bursts so they cannot stack up.
 *
 * There is nothing for the GUI to command the service to do -- verification is
 * autonomous from boot, not user-triggered. MapManagerRequest only asks for
 * both of the above to be re-sent, so an opening screen paints immediately
 * instead of waiting for the next tick.
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

// Service --> GUI
constexpr SDK::MessageType::Type MAP_MANAGER_PACK_STATUS = 0x00000003;

// GUI --> Service
constexpr SDK::MessageType::Type MAP_MANAGER_REQUEST = 0x00000002;

/// How many roster rows the GUI keeps. The service sends every pack it
/// tracks; rows past this are dropped by the GUI rather than by the service,
/// so the counts on screen (which come from MapManagerProgress, not from the
/// roster) stay honest even on a watch carrying more packs than this.
constexpr size_t kMaxRosterPacks = 16;

/// Name length a roster row carries. Shorter than kMaxPackNameLen so that six
/// rows fit one message: 31 characters holds the longest name this has met
/// ("previous-20260813161118.rawtiles") and comfortably more than the ~20 a
/// row of the list can actually draw. The progress message still carries the
/// full-length name, so nothing else is narrowed by this.
constexpr size_t kMaxRowNameLen = 32;

/// Rows per MapManagerPackStatus. Six keeps a full kMaxRosterPacks roster
/// inside three messages -- well under the GUI's ten-deep queue, so a burst
/// survives even sharing a frame with a progress snapshot.
constexpr size_t kRowsPerMessage = 6;

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
 * @brief Where one pack has got to.
 *
 * Mirrors PackCrcVerifier::Status, deliberately as a separate enum with fixed
 * numeric values: this one crosses a message boundary, so its values are wire
 * format and may not be renumbered when the verifier's internal enum changes.
 */
enum class PackState : uint8_t {
    Pending    = 0, ///< Tracked, not started -- waiting its turn.
    Scanning   = 1, ///< Being read right now.
    Verified   = 2, ///< CRC matched.
    Mismatched = 3, ///< CRC did not match: the pack is corrupt.
    Unreadable = 4, ///< Could not be opened or read at all.
};

/// One roster entry: a pack, and where it has got to.
struct PackRow {
    char    name[kMaxRowNameLen];
    uint8_t state; ///< A PackState.
};

/**
 * @brief A chunk of the roster.
 *
 * Sent as a burst covering 0..total-1, each message carrying `count` rows
 * starting at `firstIndex`. The GUI can therefore tell a complete roster from
 * a partial one and repaint once at the end rather than once per chunk.
 */
struct MapManagerPackStatus : public SDK::MessageBase {
    uint16_t firstIndex; ///< Roster position of rows[0].
    uint16_t total;      ///< Size of the whole roster this burst describes.
    uint8_t  count;      ///< How many of rows[] are populated.
    PackRow  rows[kRowsPerMessage];

    MapManagerPackStatus()
        : SDK::MessageBase(MAP_MANAGER_PACK_STATUS)
        , firstIndex(0)
        , total(0)
        , count(0)
        , rows{}
    {
    }
};

static_assert(sizeof(MapManagerPackStatus) <= 256,
              "MapManagerPackStatus must fit the largest kernel message pool block");

/**
 * @brief Ask the service to re-send the progress snapshot and the roster.
 */
struct MapManagerRequest : public SDK::MessageBase {
    MapManagerRequest() : SDK::MessageBase(MAP_MANAGER_REQUEST) {}
};

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
