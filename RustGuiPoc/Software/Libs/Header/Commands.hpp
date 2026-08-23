/**
 ******************************************************************************
 * @file    Commands.hpp
 * @brief   Custom messages between the Service and GUI halves of RustGuiPoc.
 *
 * The GUI process has no sensor access: SDK::Kernel is {sys, log, mem, comm, fs}
 * and nothing more. Sensors are reachable only through SDK::Sensor::Connection,
 * which lives in the Service. So a sensor value on screen has to travel
 *
 *     sensor -> Service -> custom message -> Gui.cpp -> Rust render() -> panel
 *
 * and this header is the message in the middle. Same shape as the SDK's own
 * example (Examples/Apps/HRMonitor/Software/Libs/Header/Commands.hpp): a POD
 * struct deriving MessageBase with an app-local type constant. Types are local
 * to the app's own Service<->GUI pair, so low numbers are fine.
 ******************************************************************************
 */
#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

// Force 4-byte alignment for all message structures.
#pragma pack(push, 4)

namespace CustomMessage {

// Service --> GUI
constexpr SDK::MessageType::Type ACCEL_VALUES = 0x00000001;

/**
 * @brief One accelerometer sample, forwarded to the GUI for display.
 *
 * Axes are in g (SDK::SensorDataParser::Accelerometer's native unit). The GUI
 * decides staleness for itself from its own clock, so nothing here carries a
 * timestamp: a sample is "the newest thing the service has sent", and the GUI
 * notes when it arrived.
 */
struct AccelValues : public SDK::MessageBase {
    float x;
    float y;
    float z;

    AccelValues()
        : SDK::MessageBase(ACCEL_VALUES)
        , x(0.0f)
        , y(0.0f)
        , z(0.0f)
    {}

    AccelValues(float x, float y, float z)
        : AccelValues()
    {
        this->x = x;
        this->y = y;
        this->z = z;
    }
};

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
