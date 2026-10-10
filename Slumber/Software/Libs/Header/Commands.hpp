#ifndef COMMANDS_HPP
#define COMMANDS_HPP

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

/// Service to GUI.
struct Status : public SDK::MessageBase {
    uint32_t serviceUpMin;  ///< minutes since this service process started
    uint32_t rowsToday;     ///< rows in today's file from this process
    uint32_t writeFails;    ///< failed appends since this process started
    uint32_t markAgoMin;    ///< kNoMarkAge = no mark yet
    uint8_t  lastMark;      ///< kNoMark = none
    uint8_t  accOk;         ///< 1 if the last row had accelerometer samples
    uint8_t  hrOk;          ///< 1 if the last row had a heart rate

    Status()
        : SDK::MessageBase(STATUS)
        , serviceUpMin(0)
        , rowsToday(0)
        , writeFails(0)
        , markAgoMin(kNoMarkAge)
        , lastMark(kNoMark)
        , accOk(0)
        , hrOk(0)
    {}
};

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
