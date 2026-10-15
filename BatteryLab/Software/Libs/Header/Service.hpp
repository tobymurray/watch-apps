#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"

#include "Commands.hpp"
#include "RawLog.hpp"

/// Logs every battery message the firmware sends, for the device's whole life;
/// `BatteryLab/README.md` says what it is for.
class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    virtual ~Service();

    void run();

private:
    void onSensorData(uint16_t handle, SDK::Sensor::DataBatch &batch);
    void log(BatteryLab::Sample &s);
    void publishStatus();

    /// Rows this soon after a start go straight to disk; `README.md` says why.
    static constexpr uint32_t kEagerMs = 10u * 60u * 1000u;
    /// Bounds what a kill can lose once past the eager window.
    static constexpr uint32_t kFlushEveryMs = 10u * 60u * 1000u;

    SDK::Kernel        &mKernel;
    BatteryLab::RawLog  mLog;

    SDK::Sensor::Connection mLevel;
    SDK::Sensor::Connection mMetrics;
    SDK::Sensor::Connection mCharging;

    CustomMessage::Latest mLatest;
    uint32_t mStartMs    = 0;
    uint32_t mLastFlushMs = 0;
    uint32_t mWriteFails  = 0;
    bool     mGuiAttached = false;
};

#endif // SERVICE_HPP
