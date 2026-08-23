#ifndef SERVICE_HPP
#define SERVICE_HPP

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"

class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    virtual ~Service();

    void run();

private:
    void handleSensorData(uint16_t handle, SDK::Sensor::DataBatch &batch);

    static constexpr float    skSamplePeriodMs = 100.0f;
    static constexpr uint32_t skSampleLatency  = 0;

    SDK::Kernel            &mKernel;
    SDK::Sensor::Connection mAccel;
    bool                    mGuiStarted;
};

#endif // SERVICE_HPP
