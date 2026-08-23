#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#pragma pack(push, 4)

namespace CustomMessage {

constexpr SDK::MessageType::Type ACCEL_VALUES = 0x00000001;

struct AccelValues : public SDK::MessageBase {
    float x_g;
    float y_g;
    float z_g;

    AccelValues()
        : SDK::MessageBase(ACCEL_VALUES)
        , x_g(0.0f)
        , y_g(0.0f)
        , z_g(0.0f)
    {}

    AccelValues(float x_g, float y_g, float z_g)
        : AccelValues()
    {
        this->x_g = x_g;
        this->y_g = y_g;
        this->z_g = z_g;
    }
};

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
