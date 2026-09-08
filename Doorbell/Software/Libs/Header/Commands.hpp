/**
 ******************************************************************************
 * @file    Commands.hpp
 * @brief   Message contract between the Doorbell GUI and its service.
 ******************************************************************************
 *
 * The GUI owns the buttons, so every ring starts there. Which process actually
 * sends CommandAppNewActivity is the thing under test, so the GUI can either
 * send it itself or hand the job to the service, and the two paths are separate
 * messages rather than a flag: a run has to be able to say which one it took.
 *
 ******************************************************************************
 */

#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#pragma pack(push, 4)

namespace CustomMessage
{

// GUI --> Service
constexpr SDK::MessageType::Type RING_FROM_SERVICE = 0x00000001;

/**
 * @brief Ask the service to send CommandAppNewActivity.
 *
 * [seq] is the ring number the GUI is showing, so the service log and the GUI
 * agree on which press a wire frame belongs to.
 */
struct RingFromService : public SDK::MessageBase {
    uint32_t seq;

    RingFromService()
        : SDK::MessageBase(RING_FROM_SERVICE)
        , seq(0)
    {}
};

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
