/**
 ******************************************************************************
 * @file    Service.hpp
 * @brief   Service half of RustGuiPoc: reads the accelerometer and forwards
 *          samples to the GUI.
 *
 * Every .uapp has a Service ELF as well as a GUI ELF, and the split is not
 * cosmetic: the GUI process cannot reach a sensor at all (SDK::Kernel is
 * {sys, log, mem, comm, fs}). SDK::Sensor::Connection only works here. So this
 * half exists to own the sensor and hand samples across the message bus, which
 * is the whole reason the PoC has a real data path rather than a frame counter.
 ******************************************************************************
 */
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

    // 100 ms / no batching: ~10 Hz, delivered as it arrives. Fast enough that a
    // wrist tilt shows up on the panel immediately (which is the point of
    // picking this sensor), slow enough not to flood a 30 Hz GUI tick.
    static constexpr float    skSamplePeriod  = 100.0f;
    static constexpr uint32_t skSampleLatency = 0;

    SDK::Kernel            &mKernel;
    SDK::Sensor::Connection mAccel;
    bool                    mGuiStarted;
};

#endif // SERVICE_HPP
