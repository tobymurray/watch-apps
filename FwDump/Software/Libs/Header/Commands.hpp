/**
 ******************************************************************************
 * @file    Commands.hpp
 * @brief   Message contract between the FwDump service and its GUI.
 ******************************************************************************
 *
 * Two messages, one each way:
 *
 *   FwDumpStatus  (Service --> GUI) -- everything the screen draws, in one
 *                 snapshot. Sent on every state change and otherwise on a
 *                 throttle while a dump runs.
 *   FwDumpCommand (GUI --> Service) -- Start, and a Resend so an opening
 *                 screen paints immediately instead of waiting for the next
 *                 tick.
 *
 * One status message rather than MapManager's two because there is no roster
 * here: the whole state of a dump is a handful of scalars, it fits one 256-byte
 * pool block with room to spare, and sending it whole means the screen can
 * never draw a mixture of two different snapshots.
 *
 * The wire enums below are deliberately separate types from FlashDumper's own
 * State and Error, rather than the same enum shared across the boundary. Their
 * numeric values are wire format: renumbering FlashDumper::Error to add a case
 * in the middle must not silently change what a screen displays, and the
 * translation function in Service.cpp is written as a switch with no default so
 * that adding a case there is a compile error here rather than a wrong label.
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
constexpr SDK::MessageType::Type FWDUMP_STATUS = 0x00000001;

// GUI --> Service
constexpr SDK::MessageType::Type FWDUMP_COMMAND = 0x00000002;

/**
 * @brief Where the dump has got to, as the screen understands it.
 *
 * Note what is *not* here: a paused state. While the kernel has the app
 * stopped -- which is what happens the moment USB is connected -- nothing runs
 * and nothing is drawn, so "paused" can only ever be reported after the fact.
 * FwDumpStatus::stalledMs is how that is done: it says a gap happened, not that
 * one is happening.
 */
enum class DumpState : uint8_t {
    Idle     = 0, ///< Waiting to be told to start.
    Checking = 1, ///< Counting chunk files already on disk.
    Dumping  = 2, ///< Reading, hashing and writing.
    Done     = 3, ///< Finished, verified, safe to plug in.
    Error    = 4, ///< Stopped and will not finish on its own.
};

/// Why a dump stopped. Mirrors FlashDumper::Error as wire format; see the file
/// comment for why it is a separate enum.
enum class DumpError : uint8_t {
    None             = 0,
    BadRegion        = 1, ///< The configured geometry does not tile.
    OpenFailed       = 2, ///< A chunk file could not be opened for writing.
    ShortWrite       = 3, ///< The filesystem accepted fewer bytes than asked.
    VerifyFailed     = 4, ///< A chunk could not be made to read back correctly.
    ManifestFailed   = 5, ///< dump_manifest.txt could not be written.
    ManifestOverflow = 6, ///< The manifest outgrew its buffer.
};

/// What the GUI can ask for. Start is the only real command; the app has
/// exactly one thing to do.
enum class DumpCommand : uint8_t {
    Start  = 0, ///< Begin (or resume) the dump. Ignored while one is running.
    Resend = 1, ///< Re-send the status snapshot now.
};

/**
 * @brief One complete snapshot of the dump.
 *
 * Carries enough that the GUI recomputes nothing: no rates derived from
 * remembered previous samples, no percentages the service could have
 * calculated. A screen that does arithmetic on two snapshots is a screen that
 * shows nonsense when one of them is dropped, and the queue this arrives
 * through discards its oldest entry on overflow.
 */
struct FwDumpStatus : public SDK::MessageBase {
    uint64_t bytesDone;  ///< Of the region, hashed so far this pass.
    uint64_t bytesTotal; ///< Size of the region. Constant.

    uint32_t elapsedMs;  ///< Since the pass began.
    uint32_t etaSec;     ///< Estimate from the observed rate; 0 when not yet meaningful.
    uint32_t kbPerSec;   ///< Observed throughput, for the same reason.
    uint32_t wholeCrc;   ///< whole_image_crc32. Only meaningful on Done.

    uint32_t regionBase; ///< What is being dumped, so the screen can say so.
    uint32_t regionSize;

    /// Milliseconds of wall clock the service lost between two consecutive
    /// slices, or 0 if it has never lost any. Non-zero means something stopped
    /// the app mid-dump -- connecting USB is the expected cause -- and is what
    /// the screen turns into "keep the cable out". Retrospective by nature.
    uint32_t stalledMs;

    uint16_t chunksDone;     ///< Finished this pass: written or re-verified.
    uint16_t chunksTotal;    ///< Chunks in the region.
    uint16_t chunksVerified; ///< Of chunksDone, how many were already correct on disk.
    uint16_t chunksPresent;  ///< Found by the last presence scan.
    uint16_t errorChunk;     ///< Which chunk failed. Only meaningful on Error.

    uint8_t state;        ///< A DumpState.
    uint8_t error;        ///< A DumpError.
    uint8_t configStatus; ///< A DumpConfig::Status, so the screen can flag an ignored config.
    bool    scanComplete; ///< Whether chunksPresent is trustworthy yet.

    FwDumpStatus()
        : SDK::MessageBase(FWDUMP_STATUS)
        , bytesDone(0)
        , bytesTotal(0)
        , elapsedMs(0)
        , etaSec(0)
        , kbPerSec(0)
        , wholeCrc(0)
        , regionBase(0)
        , regionSize(0)
        , stalledMs(0)
        , chunksDone(0)
        , chunksTotal(0)
        , chunksVerified(0)
        , chunksPresent(0)
        , errorChunk(0)
        , state(static_cast<uint8_t>(DumpState::Idle))
        , error(static_cast<uint8_t>(DumpError::None))
        , configStatus(0)
        , scanComplete(false)
    {
    }
};

// The kernel's message pool offers nothing larger than 256 bytes, so a status
// that outgrew it would fail to allocate at runtime rather than at build time.
static_assert(sizeof(FwDumpStatus) <= 256,
              "FwDumpStatus must fit the largest kernel message pool block");

/**
 * @brief One instruction from the screen.
 */
struct FwDumpCommand : public SDK::MessageBase {
    uint8_t command; ///< A DumpCommand.

    FwDumpCommand()
        : SDK::MessageBase(FWDUMP_COMMAND)
        , command(static_cast<uint8_t>(DumpCommand::Resend))
    {
    }
};

static_assert(sizeof(FwDumpCommand) <= 256,
              "FwDumpCommand must fit the largest kernel message pool block");

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
