#include "Service.hpp"

#include "Commands.hpp"

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometer.hpp"
#include "SDK/SensorLayer/SensorDataView.hpp"
#include "SDK/Timer/Timer.hpp"

#define LOG_MODULE_PRX   "MagSvc"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace {

constexpr uint32_t kWaitForever = 0xFFFFFFFF;

} // namespace

Service::Service(SDK::Kernel& kernel)
    : mKernel(kernel)
    , mMag(SDK::Sensor::Type::MAGNETIC_FIELD, kSensorPeriod, kSensorLatency)
    , mAccel(SDK::Sensor::Type::ACCELEROMETER, kSensorPeriod, kSensorLatency)
{
}

Service::~Service()
{
    disconnectSensors();
}

void Service::connectSensors()
{
    // The return value of connect() is the whole first finding. `SPO2` and
    // `HEART_BEAT` both sit in `SensorTypes.hpp` looking exactly like every
    // other type and both return false here on real hardware, because there is
    // no firmware producer behind them (SleepLab ledger rows S4 and S5). So a
    // false is recorded as a verdict rather than logged as a warning and
    // forgotten.
    mMagResolve = mMag.connect() ? Mag::Resolve::Resolved : Mag::Resolve::NoProducer;
    if (mMagResolve == Mag::Resolve::NoProducer) {
        LOG_WARNING("MAGNETIC_FIELD (0x30) resolved no driver. There is no compass.\n");
    } else {
        LOG_INFO("MAGNETIC_FIELD resolved\n");
    }

    // Tilt compensation cannot work without this, so its own resolve state is
    // reported rather than assumed.
    mAccelResolve = mAccel.connect() ? Mag::Resolve::Resolved : Mag::Resolve::NoProducer;
    if (mAccelResolve == Mag::Resolve::NoProducer) {
        LOG_WARNING("ACCELEROMETER resolved no driver; no tilt compensation\n");
    }
}

void Service::disconnectSensors()
{
    if (mMag.isConnected()) {
        mMag.disconnect();
    }
    if (mAccel.isConnected()) {
        mAccel.disconnect();
    }
}

Mag::Delivery Service::delivery() const
{
    const uint32_t age =
        mHaveField ? SDK::Timer::elapsed(mKernel.sys.getTimeMs(), mLastMagMs) : 0;
    return Mag::classifyDelivery(mMagResolve, mMagSamples, age, kStaleAfterMs);
}

Mag::Vec3 Service::correctedField() const
{
    Mag::Vec3 corrected{};
    if (mHardIron.apply(mRawField, corrected)) {
        return corrected;
    }
    // Refused rather than half-applied: an offset from an incomplete sweep is
    // wrong and looks calibrated.
    return mRawField;
}

void Service::handleMagBatch(SDK::Sensor::DataBatch& batch, uint16_t stride)
{
    ++mMagBatches;

    // The stride comes from the event rather than the batch: `DataBatch` keeps
    // it private and exposes only the field count it derived from it. Recording
    // the raw stride matters because a stride that is not a whole number of
    // fields is the single most interesting frame this app could meet, and the
    // derived field count would have already rounded it away.
    mStride = stride;

    for (uint16_t i = 0; i < batch.size(); ++i) {
        const SDK::Sensor::DataView view = batch[i];

        const uint16_t fields = view.getFieldCount();
        mFieldCount = fields;
        mShape      = Mag::classifyFields(fields);

        // There is no parser for this type, so there is no isDataValid() to
        // ask. A frame too narrow to be three axes is recorded and not read:
        // reading f[2] off a two-field frame is what GpsLocation does wrong
        // (SensorLab Docs/FINDINGS.md section 3) and it reads past the fields
        // the driver actually sent.
        if (mShape != Mag::FrameShape::ThreeAxis && mShape != Mag::FrameShape::Wider) {
            ++mMagSamples;
            mLastMagMs = mKernel.sys.getTimeMs();
            mHaveField = true;
            continue;
        }

        const Mag::Vec3 raw{view.f[0], view.f[1], view.f[2]};

        // Both readings of the same field, because the union does not say which
        // member the driver filled in.
        mRawBits0 = view.u[0];
        mRawInt0  = view.i[0];

        if (!Mag::isFinite(raw)) {
            mSawNonFinite = true;
        }

        mRawField  = raw;
        mHaveField = true;
        mLastMagMs = mKernel.sys.getTimeMs();
        ++mMagSamples;

        const Mag::Vec3 corrected = correctedField();
        mSpread.add(Mag::norm(corrected));

        if (mCalibrating) {
            mHardIron.add(raw);
        }
    }
}

void Service::handleAccelBatch(SDK::Sensor::DataBatch& batch)
{
    // Only the newest sample matters here: the accelerometer is used as an
    // attitude reference for the heading, not as a signal in its own right, and
    // it arrives at roughly 48 Hz whatever period was requested.
    if (batch.size() == 0) {
        return;
    }

    SDK::SensorDataParser::Accelerometer parser(batch[batch.size() - 1]);

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (!parser.getXYZ(x, y, z)) {
        return;
    }

    mAccelValue  = Mag::Vec3{x, y, z};
    mLastAccelMs = mKernel.sys.getTimeMs();
    mHaveAccel   = true;
}

void Service::handleSensorData(uint16_t handle, SDK::Sensor::DataBatch& batch, uint16_t stride)
{
    // `SDK::Sensor::Connection` truncates the handle to 8 bits
    // (SensorLab Docs/FINDINGS.md section 4), so matchesDriver() compares a
    // narrowed value. With two subscriptions there is nothing here for that to
    // collide with, and the magnetometer is tested first so a collision would
    // show up as magnetometer frames whose field count is the accelerometer's.
    if (mMag.matchesDriver(handle)) {
        handleMagBatch(batch, stride);
    } else if (mAccel.matchesDriver(handle)) {
        handleAccelBatch(batch);
    }
}

void Service::handleControl(uint8_t action)
{
    switch (static_cast<CustomMessage::Action>(action)) {
        case CustomMessage::Action::CalibrationStart:
            mHardIron.reset();
            mCalibrating = true;
            LOG_INFO("Hard-iron sweep started\n");
            break;

        case CustomMessage::Action::CalibrationStop:
            mCalibrating = false;
            LOG_INFO("Hard-iron sweep stopped: %s after %u samples\n",
                     Mag::HardIron::name(mHardIron.quality()),
                     static_cast<unsigned>(mHardIron.samples()));
            break;

        case CustomMessage::Action::CalibrationReset:
            mHardIron.reset();
            mCalibrating = false;
            // The spread is a property of the corrected field, so it means
            // something different after the offset changes.
            mSpread = Mag::MagnitudeSpread{};
            break;

        case CustomMessage::Action::ToggleUpConvention:
            mConvention = (mConvention == Mag::UpConvention::AccelPointsUp)
                              ? Mag::UpConvention::AccelPointsDown
                              : Mag::UpConvention::AccelPointsUp;
            LOG_INFO("Up convention: accel points %s\n",
                     mConvention == Mag::UpConvention::AccelPointsUp ? "up" : "down");
            break;

        case CustomMessage::Action::None:
        default:
            break;
    }

    sendCalibration();
}

void Service::sendStatus()
{
    if (!mGuiStarted) {
        return;
    }

    auto msg = SDK::make_msg<CustomMessage::MagStatus>(mKernel);
    if (!msg) {
        // A message that does not fit the kernel's pool fails here rather than
        // at link time, so the failure is logged rather than assumed away.
        LOG_WARNING("MagStatus allocation failed\n");
        return;
    }

    const uint32_t now = mKernel.sys.getTimeMs();

    // One clock read for both the age and the freshness verdict: sampled at
    // different instants they can contradict each other on screen, which
    // discredits the diagnostics exactly when they are being read.
    const uint32_t magAge =
        mHaveField ? SDK::Timer::elapsed(now, mLastMagMs) : 0;
    const bool accelFresh =
        mHaveAccel && SDK::Timer::elapsed(now, mLastAccelMs) <= kStaleAfterMs;

    const Mag::Vec3 corrected = correctedField();

    msg->samples  = mMagSamples;
    msg->batches  = mMagBatches;
    msg->ageMs    = magAge;
    msg->uptimeMs = now;
    msg->rawBits0 = mRawBits0;
    msg->rawInt0  = mRawInt0;
    msg->rawX     = mRawField.x;
    msg->rawY     = mRawField.y;
    msg->rawZ     = mRawField.z;

    msg->magnitude = mHaveField ? Mag::norm(corrected) : 0.0f;
    msg->spread    = mSpread.spreadFraction();

    msg->fieldCount   = mFieldCount;
    msg->stride       = mStride;
    msg->resolve      = static_cast<uint8_t>(mMagResolve);
    msg->delivery     = static_cast<uint8_t>(delivery());
    msg->shape        = static_cast<uint8_t>(mShape);
    msg->accelResolve = static_cast<uint8_t>(mAccelResolve);
    msg->calQuality   = static_cast<uint8_t>(mHardIron.quality());

    // Classified on the corrected vector, which is the one that should land in
    // a band: an uncorrected reading carries the case's own field as well.
    msg->units = mHaveField ? static_cast<uint8_t>(Mag::classify(corrected))
                            : static_cast<uint8_t>(Mag::Units::Unknown);

    uint8_t flags = 0;
    if (accelFresh) {
        flags |= CustomMessage::StatusFlag::kAccelFresh;
    }
    if (mCalibrating) {
        flags |= CustomMessage::StatusFlag::kCalibrating;
    }
    if (mSawNonFinite) {
        flags |= CustomMessage::StatusFlag::kSawNonFinite;
    }

    // A heading needs a live field and a live attitude reference. Without
    // either, none is sent, and the compass screen says which one is missing
    // rather than drawing a needle from stale numbers.
    if (mHaveField && accelFresh && delivery() == Mag::Delivery::Delivering) {
        const Mag::Result r = Mag::compute(corrected, mAccelValue, mConvention);
        msg->headingDeg     = r.headingDeg;
        msg->dipDeg         = r.dipDeg;
        if (r.valid) {
            flags |= CustomMessage::StatusFlag::kHeadingValid;
        }
        if (r.levelled) {
            flags |= CustomMessage::StatusFlag::kLevelled;
        }
    }

    msg->flags = flags;
    msg.send(0);
}

void Service::sendCalibration()
{
    if (!mGuiStarted) {
        return;
    }

    auto msg = SDK::make_msg<CustomMessage::MagCalibration>(mKernel);
    if (!msg) {
        LOG_WARNING("MagCalibration allocation failed\n");
        return;
    }

    const Mag::Vec3 offsets = mHardIron.offsets();
    const Mag::Vec3 spans   = mHardIron.spans();

    msg->samples = mHardIron.samples();
    msg->offsetX = offsets.x;
    msg->offsetY = offsets.y;
    msg->offsetZ = offsets.z;
    msg->spanX   = spans.x;
    msg->spanY   = spans.y;
    msg->spanZ   = spans.z;
    msg->quality = static_cast<uint8_t>(mHardIron.quality());

    msg.send(0);
}

void Service::run()
{
    LOG_INFO("Started\n");

    connectSensors();

    while (true) {
        SDK::MessageBase* msg = nullptr;

        // Blocking, so this thread yields whenever there is nothing to do.
        // A zero timeout here would spin, and the liveness watchdog reboots the
        // watch when an app thread does not yield.
        if (!mKernel.comm.getMessage(msg, kStatusPeriodMs)) {
            // A timeout is the repaint tick: it means the queue was quiet for a
            // status period, so the screen is due an update whether or not a
            // sample arrived. Without this, a sensor with no producer would
            // never repaint, and "no producer" is the answer most worth showing.
            sendStatus();
            continue;
        }

        switch (msg->getType()) {
            case SDK::MessageType::COMMAND_APP_STOP:
            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                LOG_INFO("Exiting\n");
                disconnectSensors();
                mKernel.comm.releaseMessage(msg);
                return;

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                LOG_INFO("GUI is now running\n");
                mGuiStarted = true;
                sendCalibration();
                sendStatus();
                break;

            case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
                auto* event = static_cast<SDK::Message::Sensor::EventData*>(msg);
                SDK::Sensor::DataBatch batch(event->data, event->count, event->stride);
                handleSensorData(static_cast<uint16_t>(event->handle), batch, event->stride);
            } break;

            case CustomMessage::MAG_CONTROL: {
                auto* control = static_cast<CustomMessage::MagControl*>(msg);
                handleControl(control->action);
                msg->setResult(SDK::MessageResult::SUCCESS);
            } break;

            default:
                msg->setResult(SDK::MessageResult::FAIL);
                break;
        }

        mKernel.comm.releaseMessage(msg);

        // Rate-limited rather than per-batch: the GUI's queue is ten deep and
        // discards the oldest, so a burst of batches must not become a burst of
        // repaints.
        const uint32_t now = mKernel.sys.getTimeMs();
        if (SDK::Timer::elapsed(now, mLastStatusMs) >= kStatusPeriodMs) {
            mLastStatusMs = now;
            sendStatus();
        }
    }
}
