#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#pragma pack(push, 4)

namespace CustomMessage {

constexpr SDK::MessageType::Type ACCEL_VALUES = 0x00000001;

struct AccelValues : public SDK::MessageBase {
    float    x_g;
    float    y_g;
    float    z_g;
    // The driver's own timestamp for this sample, not the moment it arrived, so
    // the true sample interval can be told apart from IPC latency.
    uint32_t sensor_ts_ms;
    uint16_t batch_size;

    AccelValues()
        : SDK::MessageBase(ACCEL_VALUES)
        , x_g(0.0f)
        , y_g(0.0f)
        , z_g(0.0f)
        , sensor_ts_ms(0)
        , batch_size(0)
    {}

    AccelValues(float x_g, float y_g, float z_g, uint32_t sensor_ts_ms, uint16_t batch_size)
        : AccelValues()
    {
        this->x_g          = x_g;
        this->y_g          = y_g;
        this->z_g          = z_g;
        this->sensor_ts_ms = sensor_ts_ms;
        this->batch_size   = batch_size;
    }
};

} // namespace CustomMessage

#pragma pack(pop)

#endif // COMMANDS_HPP
