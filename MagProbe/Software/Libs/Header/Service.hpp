#ifndef SERVICE_HPP
#define SERVICE_HPP

#include "Mag/Frame.hpp"
#include "Mag/HardIron.hpp"
#include "Mag/Heading.hpp"
#include "Mag/Units.hpp"
#include "Mag/Vec3.hpp"

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"

#include <cstdint>

/**
 * @brief The half that can reach the sensor.
 *
 * Subscribes to `MAGNETIC_FIELD` (0x30) and `ACCELEROMETER` (0x10), and answers
 * one question: can this watch be a compass?
 *
 * The magnetometer ships no parser, so nothing here calls one. Frames are read
 * through `SDK::Sensor::DataView`, whose field count is whatever the driver
 * sent, and both the float and the integer readings of the first field are
 * reported because the union does not say which member was filled in and for
 * this type nobody has established it.
 */
class Service {
public:
    explicit Service(SDK::Kernel& kernel);
    ~Service();

    void run();

private:
    /// How often the Service repaints the GUI. The GUI's queue is ten deep and
    /// discards the oldest, so this is a rate the GUI can drain rather than the
    /// rate samples arrive at.
    static constexpr uint32_t kStatusPeriodMs = 200;

    /// Past this, a sample is not "now" any more. Well above the sensor layer's
    /// ~1 s aggregation timer, because this separates stopped from slow and
    /// nothing finer.
    static constexpr uint32_t kStaleAfterMs = 3000;

    /// Asked for, not expected: the accelerometer demonstrably ignores it
    /// (SleepLab ledger row S3). Zero means "whatever the driver does", which is
    /// the honest request for a type whose behaviour is being measured.
    static constexpr float    kSensorPeriod  = 0.0f;
    static constexpr uint32_t kSensorLatency = 0;

    void connectSensors();
    void disconnectSensors();

    void handleSensorData(uint16_t handle, SDK::Sensor::DataBatch& batch, uint16_t stride);
    void handleMagBatch(SDK::Sensor::DataBatch& batch, uint16_t stride);
    void handleAccelBatch(SDK::Sensor::DataBatch& batch);
    void handleControl(uint8_t action);

    void sendStatus();
    void sendCalibration();

    Mag::Delivery delivery() const;
    Mag::Vec3     correctedField() const;

    SDK::Kernel& mKernel;

    SDK::Sensor::Connection mMag;
    SDK::Sensor::Connection mAccel;

    Mag::Resolve mMagResolve{Mag::Resolve::NotAsked};
    Mag::Resolve mAccelResolve{Mag::Resolve::NotAsked};

    // What the magnetometer has said.
    Mag::Vec3           mRawField{};
    uint32_t            mRawBits0{0};
    int32_t             mRawInt0{0};
    uint16_t            mFieldCount{0};
    uint16_t            mStride{0};
    uint32_t            mMagSamples{0};
    uint32_t            mMagBatches{0};
    uint32_t            mLastMagMs{0};
    bool                mHaveField{false};
    bool                mSawNonFinite{false};
    Mag::FrameShape     mShape{Mag::FrameShape::Unknown};
    Mag::MagnitudeSpread mSpread{};

    // What the accelerometer has said.
    Mag::Vec3 mAccelValue{};
    uint32_t  mLastAccelMs{0};
    bool      mHaveAccel{false};

    Mag::HardIron    mHardIron{};
    bool             mCalibrating{false};
    Mag::UpConvention mConvention{Mag::UpConvention::AccelPointsUp};

    uint32_t mLastStatusMs{0};
    bool     mGuiStarted{false};
};

#endif // SERVICE_HPP
