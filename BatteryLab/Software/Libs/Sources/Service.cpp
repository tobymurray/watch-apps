#include "Service.hpp"

#include <ctime>

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"

#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryCharging.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryMetrics.hpp"

namespace
{

using BatteryLab::Kind;
using BatteryLab::Sample;
using BatteryLab::scaled;

/// `README.md`, "The experiments", says why 10 s.
constexpr float kProbePeriodMs    = 10000.0f;
constexpr float kChargingPeriodMs = 1000.0f;

uint32_t nowUtc()
{
    const std::time_t now = std::time(nullptr);
    return now > 0 ? static_cast<uint32_t>(now) : 0u;
}

/// Local minus UTC in minutes, from the C library's own view of the zone.
int32_t utcOffsetMin(uint32_t utc)
{
    if (utc == 0) {
        return BatteryLab::kNone;
    }
    const std::time_t t = static_cast<std::time_t>(utc);
    std::tm           local{};
    std::tm           gm{};
    if (localtime_r(&t, &local) == nullptr || gmtime_r(&t, &gm) == nullptr) {
        return BatteryLab::kNone;
    }
    const int dayDiff = (local.tm_year != gm.tm_year) ? (local.tm_year < gm.tm_year ? -1 : 1)
                                                      : (local.tm_yday - gm.tm_yday);
    return static_cast<int32_t>(dayDiff * 1440 + (local.tm_hour - gm.tm_hour) * 60 +
                                (local.tm_min - gm.tm_min));
}

/// `yyyymmdd` of the UTC day, or `00000000` with no clock.
void utcStem(uint32_t utc, char (&out)[9])
{
    std::tm           gm{};
    const std::time_t t = static_cast<std::time_t>(utc);
    const bool        known = utc != 0 && gmtime_r(&t, &gm) != nullptr;
    const unsigned    parts[] = {known ? static_cast<unsigned>(gm.tm_year + 1900) : 0u,
                                 known ? static_cast<unsigned>(gm.tm_mon + 1) : 0u,
                                 known ? static_cast<unsigned>(gm.tm_mday) : 0u};
    const unsigned    widths[] = {4, 2, 2};
    size_t            at = 0;
    for (size_t p = 0; p < 3; ++p) {
        unsigned v = parts[p];
        for (unsigned w = widths[p]; w > 0; --w) {
            out[at + w - 1] = static_cast<char>('0' + v % 10);
            v /= 10;
        }
        at += widths[p];
    }
    out[at] = '\0';
}

} // namespace

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mLog(kernel.fs)
    , mLevel(SDK::Sensor::Type::BATTERY_LEVEL, kProbePeriodMs)
    , mMetrics(SDK::Sensor::Type::BATTERY_METRICS, kProbePeriodMs)
    , mCharging(SDK::Sensor::Type::BATTERY_CHARGING, kChargingPeriodMs)
{
}

Service::~Service()
{
    mLevel.disconnect();
    mMetrics.disconnect();
    mCharging.disconnect();
}

void Service::log(Sample &s)
{
    const uint32_t now = mKernel.sys.getTimeMs();
    s.uptimeMs  = now;
    s.utc       = nowUtc();
    s.utcOffMin = utcOffsetMin(s.utc);
    char stem[9];
    utcStem(s.utc, stem);

    bool ok = mLog.add(s, stem);
    if (now - mStartMs < kEagerMs || now - mLastFlushMs >= kFlushEveryMs || s.kind == Kind::Stop) {
        ok = mLog.flush() && ok;
        mLastFlushMs = now;
    }
    if (!ok) {
        ++mWriteFails;
    }
}

void Service::onSensorData(uint16_t handle, SDK::Sensor::DataBatch &batch)
{
    const uint16_t n = batch.size();
    for (uint16_t i = 0; i < n; i++) {
        Sample s;
        s.sensorTsMs = batch[i].getTimestamp();
        s.fields     = batch[i].getFieldCount();

        if (mLevel.matchesDriver(handle)) {
            s.kind = Kind::Level;
            const SDK::SensorDataParser::BatteryLevel p(batch[i]);
            if (p.isDataValid()) {
                s.pctX1000 = mLatest.pctX1000 = scaled(p.getCharge(), 1000.0f);
            }
        } else if (mMetrics.matchesDriver(handle)) {
            s.kind = Kind::Metrics;
            const SDK::SensorDataParser::BatteryMetrics p(batch[i]);
            // Invalid current reads as 0.0, a value a real reading can take.
            if (p.isDataValid()) {
                s.mv             = mLatest.mv             = scaled(p.getVoltage(), 1000.0f);
                s.maX1000        = mLatest.maX1000        = scaled(p.getCurrent(), 1000.0f);
                s.avgMaX1000     = mLatest.avgMaX1000     = scaled(p.getAverageCurrent(), 1000.0f);
                s.mahX1000       = mLatest.mahX1000       = scaled(p.getCapacity(), 1000.0f);
                s.designMahX1000 = mLatest.designMahX1000 = scaled(p.getDesignCapacity(), 1000.0f);
            }
        } else if (mCharging.matchesDriver(handle)) {
            s.kind = Kind::Charging;
            const SDK::SensorDataParser::BatteryCharging p(batch[i]);
            if (p.isDataValid()) {
                s.connected = mLatest.connected = p.isUsbConnected() ? 1 : 0;
                s.charging  = mLatest.charging  = p.isCharging() ? 1 : 0;
            }
        } else {
            return;
        }
        log(s);
    }
    publishStatus();
}

void Service::publishStatus()
{
    if (!mGuiAttached) {
        return;
    }
    if (auto st = SDK::make_msg<CustomMessage::Status>(mKernel)) {
        st->latest       = mLatest;
        st->lines        = mLog.lines();
        st->writeFails   = mWriteFails;
        st->serviceUpMin = (mKernel.sys.getTimeMs() - mStartMs) / 60000u;
        st.send();
    }
}

void Service::run()
{
    mStartMs     = mKernel.sys.getTimeMs();
    mLastFlushMs = mStartMs;

    Sample boot;
    boot.kind = Kind::Boot;
    log(boot);

    mLevel.connect();
    mMetrics.connect();
    mCharging.connect();

    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, kFlushEveryMs)) {
            if (mKernel.sys.getTimeMs() - mLastFlushMs >= kFlushEveryMs) {
                if (!mLog.flush()) {
                    ++mWriteFails;
                }
                mLastFlushMs = mKernel.sys.getTimeMs();
            }
            continue;
        }

        switch (msg->getType()) {
            case SDK::MessageType::COMMAND_APP_STOP: {
                Sample stop;
                stop.kind = Kind::Stop;
                log(stop);
                mLevel.disconnect();
                mMetrics.disconnect();
                mCharging.disconnect();
                mKernel.comm.releaseMessage(msg);
                return;
            }

            case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
                auto *event = static_cast<SDK::Message::Sensor::EventData *>(msg);
                SDK::Sensor::DataBatch batch(event->data, event->count, event->stride);
                onSensorData(static_cast<uint16_t>(event->handle), batch);
                break;
            }

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
            case CustomMessage::STATUS_REQUEST:
                mGuiAttached = true;
                publishStatus();
                break;

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                mGuiAttached = false;
                break;

            default:
                msg->setResult(SDK::MessageResult::FAIL);
                break;
        }

        mKernel.comm.releaseMessage(msg);
    }
}
