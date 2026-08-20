/**
 ******************************************************************************
 * @file    Commands.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The messages between the Service and the GUI.
 ******************************************************************************
 *
 * The kernel's largest message-pool block is 256 bytes. A message that
 * overflows it is not a compile error by itself -- it silently falls out of
 * the pool at runtime -- so every type here is `#pragma pack`ed and carries a
 * `static_assert`, which is the only thing that catches it.
 *
 * ---------------------------------------------------------------------------
 * A night's epoch strip does not fit one message, and here is what is done
 *
 * A night is up to 960 scoring epochs. Even at one byte each -- and the strip
 * needs a sleep/wake verdict *and* a restfulness level per epoch -- that is
 * four times the largest block available.
 *
 * Three options were considered:
 *
 *   1. **A burst of indexed rows**, as `MapManager` does for its pack roster:
 *      each message carries its own `firstIndex` and `total` so the GUI can
 *      tell a complete burst from a partial one. Costs four or five messages
 *      per repaint.
 *   2. **The GUI reads the night's CSV itself.** No IPC at all, but it puts a
 *      CSV parser and a file handle in the GUI process, gives two processes
 *      opinions about the same file, and makes the screen depend on storage
 *      being readable at the moment somebody opens the app.
 *   3. **Downsample to what the screen can draw.**
 *
 * **Option 3, and it is not a compromise.** The strip is drawn across a
 * 240x240 round panel, so about 200 pixels of usable width: a 960-epoch night
 * cannot be shown at one epoch per pixel whatever the transport does. The
 * downsample is forced by the display, not by the message size.
 *
 * 100 buckets, drawn two pixels wide, is what makes 200 px come out even --
 * and an even division matters more than it sounds, because buckets of
 * alternating width read as a texture the data does not have. At 100 buckets
 * an eight-hour night is about five minutes each, which is the right grain for
 * a shape you glance at.
 *
 * That then fits one message alongside the summary, which is worth having: the
 * screen can never draw half a night. No partial burst to guard against, no
 * sequence numbers, and no question about what to show when a second night
 * arrives mid-burst.
 *
 * The size is asserted rather than reasoned about -- see the bottom of this
 * file. The first attempt used 200 buckets and did not fit.
 *
 * Each bucket is packed into one byte, low nibble sleep/wake and high nibble
 * restfulness, because two parallel arrays would not fit and a night shown
 * without its wake periods is a different picture.
 *
 ******************************************************************************
 */

#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include <cstdint>

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include "Engine/NightSummary.hpp"

#pragma pack(push, 4)

namespace CustomMessage {

/// Service --> GUI: last night, or the night in progress.
constexpr SDK::MessageType::Type SLEEP_REPORT  = 0x00000001;
/// Service --> GUI: one row of the history burst.
constexpr SDK::MessageType::Type SLEEP_HISTORY = 0x00000002;
/// GUI --> Service: send both now, rather than waiting.
constexpr SDK::MessageType::Type SLEEP_REQUEST = 0x00000003;

/// Buckets in the epoch strip. Set by the panel, not by the message size --
/// see the file comment.
constexpr uint16_t kStripBuckets = 100;

/// Pixels each bucket is drawn at. kStripBuckets * this is the strip's width,
/// and the product must divide the panel's usable width evenly or the strip
/// reads as a texture the data does not have.
constexpr int16_t kStripPixelsPerBucket = 2;

/// One bucket: low nibble the sleep/wake verdict, high nibble the restfulness
/// level. Both, because a night drawn without its wake periods is a different
/// picture and two parallel arrays would not fit.
namespace Strip {
    constexpr uint8_t kVerdictMask = 0x0F;
    constexpr uint8_t kBandShift   = 4;

    /// No data at all for this bucket -- outside the night, or every epoch in
    /// it unscorable. Deliberately distinct from "awake", which is a finding.
    constexpr uint8_t kVerdictNone = 0x0F;

    inline uint8_t pack(uint8_t verdict, uint8_t band)
    {
        return static_cast<uint8_t>((verdict & kVerdictMask) |
                                    static_cast<uint8_t>(band << kBandShift));
    }
    inline uint8_t verdict(uint8_t b) { return static_cast<uint8_t>(b & kVerdictMask); }
    inline uint8_t band(uint8_t b)    { return static_cast<uint8_t>(b >> kBandShift); }
}

/// What the service is doing right now, for the screen's first line.
enum class Phase : uint8_t {
    Idle       = 0, ///< Outside the bedtime window, or waiting for stillness.
    Watching   = 1, ///< Inside the window, waiting to settle.
    Recording  = 2, ///< A night is open.
    Reported   = 3, ///< A night closed and is being shown.
};

/**
 * @brief Everything the report screen draws.
 *
 * A plain struct, separate from the message that carries it, because
 * `SDK::MessageBase` deletes copy-assignment -- deliberately, since a message
 * is a pooled block with an identity and copying one would produce a second
 * object claiming the same slot. The GUI has to keep a copy: the message goes
 * back to the pool the moment the handler returns. So the payload is its own
 * type and only the payload is copied.
 */
struct SleepReportData
{
    uint8_t  phase       = static_cast<uint8_t>(Phase::Idle);
    uint8_t  worn        = 0;   ///< Engine::WornVerdict.
    bool     hasSleep    = false;
    uint16_t interruption = 0;  ///< Engine::Interruption bits.

    /// Local minutes past midnight, or -1. Derived by the service, which is
    /// the only half that holds both clocks -- the GUI must never do
    /// wall-clock arithmetic of its own.
    int16_t  asleepAtMin = -1;
    int16_t  wokeAtMin   = -1;

    int32_t  timeInBedMin  = Engine::kAbsent;
    int32_t  totalSleepMin = Engine::kAbsent;
    int32_t  stillInBedMin = Engine::kAbsent;
    int32_t  wasoMin       = Engine::kAbsent;
    int32_t  awakenings    = Engine::kAbsent;
    int32_t  efficiencyPct = Engine::kAbsent;
    int32_t  onsetLatencyMin = Engine::kAbsent;

    int32_t  hrMinX10  = Engine::kAbsent;
    int32_t  hrMeanX10 = Engine::kAbsent;

    /// Deltas against the wearer's own baseline. `*Available` is false until
    /// enough nights exist, and the screen then shows how many more are needed
    /// rather than a zero.
    bool     hrDeltaAvailable  = false;
    int32_t  hrDeltaX10        = Engine::kAbsent;
    bool     effDeltaAvailable = false;
    int32_t  effDeltaPct       = Engine::kAbsent;
    uint16_t baselineNights    = 0;
    uint16_t nightsNeeded      = 0;

    /// Whether the band was computed with heart rate. Shown, because
    /// "movement and heart rate" and "movement" are not the same method.
    bool     bandUsedHr  = false;

    /// Epochs actually in the night, so the GUI can label the strip's span
    /// without recomputing it.
    uint16_t epochs      = 0;
    uint16_t stripUsed   = 0;   ///< Buckets carrying data.
    uint8_t  strip[kStripBuckets] = {};

    /// Which sensor drivers resolved, one letter each: ATMRHXSLCE, upper case
    /// for resolved and lower case for not.
    ///
    /// The same block the Tier 0 probe puts on its screen, and it is here for
    /// the reason `POST-MORTEM.md` gives for keeping the probe alive at all:
    /// **a line you read in the morning is not the same instrument as a block
    /// you read before bed.** The block already existed -- `Service.cpp` builds
    /// it at every launch and writes it to `Debug/sleeplab.log` -- so the only
    /// thing missing was carrying it to the screen, where somebody can act on
    /// it while there is still a night to save. A lower-case letter means
    /// `connect()` was called and there was nothing to subscribe to, which is a
    /// different problem from a sensor that resolved and then said nothing, and
    /// it is the difference between a night worth recording and one that is
    /// already lost.
    char     sensors[12] = {};
};

/// Service --> GUI. The wire type; `data` is the whole of it.
struct SleepReport : public SDK::MessageBase
{
    SleepReportData data {};

    SleepReport() : SDK::MessageBase(SLEEP_REPORT) {}
};

/// Rows of history carried per message. Sized to fit the block with the
/// header, and the history screen shows five at a time.
constexpr uint8_t kHistoryRowsPerMsg = 8;

/**
 * @brief Service --> GUI. History, as an indexed burst.
 *
 * A burst rather than one message, because 28 nights do not fit one block --
 * and unlike the strip there is no display limit forcing a downsample, since
 * every row is individually readable and the list scrolls.
 *
 * Each row carries its own `firstIndex` and `total`, so the GUI can tell a
 * complete burst from a partial one and repaint once at the end rather than
 * once per message. Same contract as `MapManager`'s pack roster.
 */
struct SleepHistory : public SDK::MessageBase
{
    struct Row {
        int32_t startUtcDays;   ///< Days since the epoch. A date is all the
                                ///< list shows, and this halves the row.
        int16_t totalSleepMin;
        int16_t efficiencyPct;
        int16_t hrMinX10;
        uint8_t worn;           ///< Engine::WornVerdict.
        uint8_t interrupted;    ///< Non-zero if any interruption bit was set.
    };

    uint8_t firstIndex = 0;
    uint8_t count      = 0;
    uint8_t total      = 0;   ///< Rows in the whole burst. 0 means "none".
    Row     rows[kHistoryRowsPerMsg] = {};

    SleepHistory() : SDK::MessageBase(SLEEP_HISTORY) {}
};

/// GUI --> Service. Carries nothing; the reply is a report and a history burst.
struct SleepRequest : public SDK::MessageBase
{
    SleepRequest() : SDK::MessageBase(SLEEP_REQUEST) {}
};

static_assert(sizeof(SleepReport)  <= 256, "SleepReport must fit the largest pool block");
static_assert(sizeof(SleepHistory) <= 256, "SleepHistory must fit the largest pool block");
static_assert(sizeof(SleepRequest) <= 256, "SleepRequest must fit the largest pool block");

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
