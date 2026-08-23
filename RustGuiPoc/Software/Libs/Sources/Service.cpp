#include "Service.hpp"

#include "Commands.hpp"

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometer.hpp"

#define LOG_MODULE_PRX   "RustSvc"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

static constexpr uint32_t kWaitForever = 0xFFFFFFFF;

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mAccel(SDK::Sensor::Type::ACCELEROMETER, skSamplePeriodMs, skSampleLatency)
    , mGuiStarted(false)
{
}

Service::~Service()
{
    mAccel.disconnect();
}

void Service::handleSensorData(uint16_t handle, SDK::Sensor::DataBatch &batch)
{
    if (!mAccel.matchesDriver(handle) || !mGuiStarted || batch.size() == 0) {
        return;
    }

    // Forward every sample, not just the newest. A latency of 2000 ms makes the
    // driver deliver ten per DataBatch, and keeping only the last discarded nine
    // of every ten while still reporting size() honestly.
    for (uint16_t i = 0; i < batch.size(); ++i) {
        SDK::SensorDataParser::Accelerometer parser(batch[i]);

        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!parser.getXYZ(x, y, z)) {
            continue;
        }

        SDK::send_msg<CustomMessage::AccelValues>(
            mKernel, x, y, z, parser.getTimestamp(), batch.size());
    }
}

void Service::run()
{
    LOG_INFO("Started\n");

    if (!mAccel.connect()) {
        LOG_WARNING("Accelerometer connect failed\n");
    }

    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, kWaitForever)) {
            continue;
        }

        switch (msg->getType()) {
            case SDK::MessageType::COMMAND_APP_STOP:
            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                LOG_INFO("Exiting\n");
                mAccel.disconnect();
                mKernel.comm.releaseMessage(msg);
                return;

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                LOG_INFO("GUI is now running\n");
                mGuiStarted = true;
                break;

            case CustomMessage::SET_SENSOR_CONFIG: {
                auto *cfg = static_cast<CustomMessage::SetSensorConfig *>(msg);
                // connect() rejects a parameter change while connected, so the
                // old connection has to go first.
                mAccel.disconnect();
                const bool ok = mAccel.connect(cfg->period_ms, cfg->latency_ms);
                LOG_INFO("sensor cfg period=%u latency=%u -> %s\n",
                         static_cast<unsigned>(cfg->period_ms),
                         static_cast<unsigned>(cfg->latency_ms),
                         ok ? "ok" : "FAILED");
            } break;

            case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
                auto *event = static_cast<SDK::Message::Sensor::EventData *>(msg);
                SDK::Sensor::DataBatch batch(event->data, event->count, event->stride);
                handleSensorData(static_cast<uint16_t>(event->handle), batch);
            } break;

            default:
                msg->setResult(SDK::MessageResult::FAIL);
                break;
        }

        mKernel.comm.releaseMessage(msg);
    }
}
