#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include <cstdint>

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include "RawLine.hpp"

#pragma pack(push, 4)

namespace CustomMessage {

constexpr SDK::MessageType::Type STATUS_REQUEST = 0x00000001;
constexpr SDK::MessageType::Type STATUS         = 0x00000002;

/// GUI to service. Sent on open, because the simulator never delivers
/// COMMAND_APP_NOTIF_GUI_RUN to the service (SleepLab's ledger row T5).
struct StatusRequest : public SDK::MessageBase {
    StatusRequest()
        : SDK::MessageBase(STATUS_REQUEST)
    {}
};

/// The last value of each battery field; `BatteryLab::kNone` until one arrives.
struct Latest {
    int32_t pctX1000       = BatteryLab::kNone;
    int32_t mv             = BatteryLab::kNone;
    int32_t maX1000        = BatteryLab::kNone;
    int32_t avgMaX1000     = BatteryLab::kNone;
    int32_t mahX1000       = BatteryLab::kNone;
    int32_t designMahX1000 = BatteryLab::kNone;
    int32_t connected      = BatteryLab::kNone;
    int32_t charging       = BatteryLab::kNone;
};

/// Service to GUI.
struct Status : public SDK::MessageBase {
    Latest   latest;
    uint32_t lines          = 0;  ///< rows logged by this process
    uint32_t writeFails     = 0;
    uint32_t serviceUpMin   = 0;

    Status()
        : SDK::MessageBase(STATUS)
    {}
};

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
