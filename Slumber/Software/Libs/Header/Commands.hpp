#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#pragma pack(push, 4)

namespace CustomMessage {

constexpr SDK::MessageType::Type STATUS_REQUEST = 0x00000001;
constexpr SDK::MessageType::Type STATUS         = 0x00000002;
constexpr SDK::MessageType::Type MARK           = 0x00000003;

constexpr uint8_t kNoMark       = 0xFF;
constexpr uint32_t kNoMarkAge   = 0xFFFFFFFFu;
constexpr uint32_t kMarkLabelCap = 24;

/// GUI to service. Sent on open, because the simulator never delivers
/// COMMAND_APP_NOTIF_GUI_RUN to the service (SleepLab's ledger row T5).
struct StatusRequest : public SDK::MessageBase {
    StatusRequest()
        : SDK::MessageBase(STATUS_REQUEST)
    {}
};

/// GUI to service: the wearer says what they are doing from now on.
struct Mark : public SDK::MessageBase {
    uint8_t code;                  ///< index into the GUI's label list
    char    label[kMarkLabelCap];  ///< NUL-terminated, written to the file beside the code

    Mark()
        : SDK::MessageBase(MARK)
        , code(kNoMark)
        , label{}
    {}
};

constexpr uint16_t kUnknown = 0xFFFFu;

/// A rhythmFill for a night whose earlier nights keep no one time.
constexpr uint16_t kRhythmVaries = 0xFFFEu;

enum NightKind : uint8_t {
    kNoNight      = 0,
    kNightSettled = 1,
    kNightSoFar   = 2,  ///< a night that could still grow
    kNightOffWrist = 3,  ///< the rest was a watch on a table
};

/// Last night as the screen shows it.
struct NightView {
    uint8_t  kind            = kNoNight;  ///< a NightKind
    uint16_t restMin         = 0;
    uint16_t onsetMin        = 0;  ///< minute of the local day, in the offset the night was slept in
    uint16_t wakeMin         = 0;
    uint16_t rhythmFill      = kUnknown;  ///< per mille; kUnknown = no usual to score against
    int16_t  rhythmOffsetMin = 0;         ///< bedtime against the usual; + is later
    uint16_t usualBedMin     = kUnknown;  ///< minute of day; kUnknown = none
    uint16_t scoredBedMin    = kUnknown;  ///< minute of day: the usual rhythmOffsetMin is against; kUnknown = none
    uint16_t scoredWakeMin   = kUnknown;  ///< minute of day; kUnknown = none
};

constexpr int16_t kNoBar      = -1;
constexpr size_t  kWeekNights = 7;

/// The main night of each of the seven evenings before tonight, oldest first.
struct WeekView {
    int16_t bed[kWeekNights]  = {kNoBar, kNoBar, kNoBar, kNoBar, kNoBar, kNoBar, kNoBar};  ///< min after local noon
    int16_t wake[kWeekNights] = {kNoBar, kNoBar, kNoBar, kNoBar, kNoBar, kNoBar, kNoBar};  ///< past 1440 after noon
    int16_t usualBed      = kNoBar;  ///< minutes after local noon; kNoBar = none
    int16_t usualWake     = kNoBar;  ///< minutes after local noon, past 1440 for a wake after noon
    uint16_t usualSpreadMin = kUnknown;  ///< Rhythm::Usual::spread; kUnknown = no usual
    uint8_t newestWeekday = 0;       ///< 0 = Sunday
};

/// Tonight's usual bed and wake.
struct TonightView {
    uint16_t bedMin     = kUnknown;  ///< minute of day
    uint16_t wakeMin    = kUnknown;  ///< minute of day
    uint16_t nowMin     = kUnknown;  ///< minute of day; kUnknown = the clock is not set
    uint8_t  nightsToGo = 0;         ///< nights still needed before there can be a usual
    uint8_t  timesVary  = 0;         ///< 1 = enough nights, but they keep no one time
};

/// Service to GUI.
struct Status : public SDK::MessageBase {
    uint32_t serviceUpMin;  ///< minutes since this service process started
    uint32_t rowsToday;     ///< rows in today's file from this process
    uint32_t writeFails;    ///< failed appends since this process started
    uint32_t markAgoMin;    ///< kNoMarkAge = no mark yet
    uint8_t  lastMark;      ///< kNoMark = none
    uint8_t  accOk;         ///< 1 if the last row had accelerometer samples
    uint8_t  hrOk;          ///< 1 if the last row had a heart rate
    NightView   night;
    WeekView    week;
    TonightView tonight;

    Status()
        : SDK::MessageBase(STATUS)
        , serviceUpMin(0)
        , rowsToday(0)
        , writeFails(0)
        , markAgoMin(kNoMarkAge)
        , lastMark(kNoMark)
        , accOk(0)
        , hrOk(0)
        , night{}
        , week{}
        , tonight{}
    {}
};

// The kernel's message pool offers nothing larger than 256 bytes, so a status
// that outgrew it would fail to allocate at runtime rather than at build time.
static_assert(sizeof(Status) <= 256, "Status must fit the largest kernel message pool block");

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
