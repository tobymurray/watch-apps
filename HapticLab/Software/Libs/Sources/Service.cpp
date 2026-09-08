#include "Service.hpp"

#include <cstdio>
#include <cstring>

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFusionRaw.hpp"

#include "Commands.hpp"
#include "Csv.hpp"
#include "EffectBridge.hpp"
#include "Records.hpp"

#define LOG_MODULE_PRX   "HapticSvc"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace {

/// Short enough that a phase boundary is never more than this late, and long
/// enough that the loop yields rather than spinning: an app thread that never
/// blocks makes the liveness watchdog reboot the watch.
constexpr uint32_t kPollMs = 20;

constexpr char kRawHeader[] = "phase,t_us,ax,ay,az,gx,gy,gz";

/// One effect's raw samples, both repeats appended, beside the run rather than
/// inside it: a file per effect keeps a partial sweep readable effect by effect.
constexpr char kImuLeaf[] = "imu";

} // namespace

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mImu(SDK::Sensor::Type::FUSION_RAW, skSamplePeriodMs, skSampleLatency)
{
}

Service::~Service()
{
    closeRawFile();
    if (mSensorConnected) {
        mImu.disconnect();
    }
}

void Service::startSweep(const char *runId, uint8_t firstEffect, uint8_t lastEffect,
                         uint8_t repeats)
{
    if (mPhase != Phase::Idle) {
        LOG_WARNING("a sweep is already running\n");
        return;
    }

    std::snprintf(mRunId, sizeof(mRunId), "%s", runId != nullptr ? runId : "unknown");
    std::snprintf(mImuDir, sizeof(mImuDir), "%s/%s/%s", Haptic::kRunsDir, mRunId, kImuLeaf);
    // The GUI half made Runs/<id> before it sent this, and mkdir is single
    // level, so this is the only directory left to make.
    if (!mKernel.fs.mkdir(mImuDir)) {
        LOG_ERROR("cannot create %s\n", mImuDir);
        return;
    }

    mFirstEffect = firstEffect >= Haptic::kMinEffect ? firstEffect : Haptic::kMinEffect;
    mLastEffect  = lastEffect <= Haptic::kMaxEffect ? lastEffect : Haptic::kMaxEffect;
    if (mLastEffect < mFirstEffect) {
        mLastEffect = mFirstEffect;
    }
    mRepeats  = repeats > 0 ? repeats : 1;
    mEffect   = mFirstEffect;
    mRepeat   = 0;
    mRawBytes = 0;
    mCapHit   = false;

    if (!mSensorConnected) {
        mSensorConnected = mImu.connect();
        if (!mSensorConnected) {
            LOG_WARNING("FUSION_RAW connect failed; the sweep will record no samples\n");
        }
    }

    LOG_INFO("sweep %u-%u x%u into %s\n", mFirstEffect, mLastEffect, mRepeats, mImuDir);
    beginSettle(mKernel.sys.getTimeMs());
}

void Service::stopSweep(const char *reason)
{
    if (mPhase == Phase::Idle) {
        return;
    }
    LOG_INFO("sweep stopped: %s\n", reason);
    closeRawFile();
    mPhase = Phase::Idle;
    publishProgress(0, /*finished=*/true);
}

void Service::beginSettle(uint32_t nowMs)
{
    mBaseline.reset();
    mPhase      = Phase::Settle;
    mPhaseStart = nowMs;
    if (!mRawFile && !openRawFile()) {
        // No raw file is not a reason to stop: the summary row still carries
        // the numbers, and saying so once is better than a sweep that dies.
        LOG_WARNING("effect %u has no raw file\n", mEffect);
    }
}

void Service::beginCapture(uint32_t nowMs)
{
    mPlayTick = mKernel.sys.getTimeMs();
    mWindow.begin(static_cast<uint64_t>(mPlayTick) * 1000ULL, mBaseline.mean());

    mSendOk        = false;
    mResultSuccess = false;
    auto *msg      = mKernel.comm.allocateMessage<Haptic::Vibro>();
    if (msg != nullptr) {
        Haptic::Note note {};
        note.effect = mEffect;
        Haptic::fillVibroMessage(*msg, &note, 1, 1);
        mSendOk        = mKernel.comm.sendMessage(msg, Haptic::kVibroSendTimeoutMs);
        mAcceptedTick  = mKernel.sys.getTimeMs();
        mResultSuccess = msg->getResult() == SDK::MessageResult::SUCCESS;
        mKernel.comm.releaseMessage(msg);
    } else {
        mAcceptedTick = mPlayTick;
    }

    mPhase      = Phase::Capture;
    mPhaseStart = nowMs;
}

void Service::finishCapture()
{
    writeEffectRow();
    publishProgress(mWindow.samples(), /*finished=*/false);
    advance();
}

void Service::advance()
{
    mRepeat++;
    if (mRepeat < mRepeats) {
        beginSettle(mKernel.sys.getTimeMs());
        return;
    }

    closeRawFile();
    mRepeat = 0;
    if (mEffect >= mLastEffect || mCapHit) {
        mPhase = Phase::Idle;
        publishProgress(0, /*finished=*/true);
        LOG_INFO("sweep done at effect %u%s\n", mEffect, mCapHit ? " (cap reached)" : "");
        return;
    }
    mEffect++;
    beginSettle(mKernel.sys.getTimeMs());
}

void Service::tick(uint32_t nowMs)
{
    switch (mPhase) {
    case Phase::Settle:
        if (Haptic::sinceTick(mPhaseStart, nowMs) >= static_cast<int32_t>(skSettleMs)) {
            beginCapture(nowMs);
        }
        break;

    case Phase::Capture:
        if (Haptic::sinceTick(mPhaseStart, nowMs) >= static_cast<int32_t>(skCaptureMs)) {
            finishCapture();
        }
        break;

    case Phase::Idle:
    case Phase::Count:
        break;
    }
}

bool Service::openRawFile()
{
    closeRawFile();

    char path[96];
    std::snprintf(path, sizeof(path), "%s/e%03u.csv", mImuDir, mEffect);

    mRawFile = mKernel.fs.file(path);
    if (!mRawFile || !mRawFile->open(/*wMode=*/true, /*override=*/true)) {
        mRawFile.reset();
        return false;
    }

    char       line[64];
    const int  length  = std::snprintf(line, sizeof(line), "%s\n", kRawHeader);
    size_t     written = 0;
    if (length > 0) {
        mRawFile->write(line, static_cast<size_t>(length), written);
        mRawBytes += static_cast<uint32_t>(written);
    }
    return true;
}

void Service::closeRawFile()
{
    if (mRawFile) {
        mRawFile->flush();
        mRawFile->close();
        mRawFile.reset();
    }
}

void Service::writeRawRow(const Haptic::ImuSample &sample, const char *phase)
{
    if (!mRawFile) {
        return;
    }
    if (mRawBytes >= skRawByteCap) {
        if (!mCapHit) {
            mCapHit = true;
            LOG_WARNING("raw capture reached its %lu byte cap\n",
                        static_cast<unsigned long>(skRawByteCap));
        }
        return;
    }

    char      line[96];
    const int length = std::snprintf(line, sizeof(line), "%s,%lu,%d,%d,%d,%d,%d,%d\n", phase,
                                     static_cast<unsigned long>(sample.timestampUs), sample.ax,
                                     sample.ay, sample.az, sample.gx, sample.gy, sample.gz);
    if (length <= 0) {
        return;
    }
    size_t written = 0;
    if (mRawFile->write(line, static_cast<size_t>(length), written)) {
        mRawBytes += static_cast<uint32_t>(written);
    }
}

void Service::writeEffectRow()
{
    char path[96];
    std::snprintf(path, sizeof(path), "%s/%s/effects.csv", Haptic::kRunsDir, mRunId);

    std::unique_ptr<SDK::Interface::IFile> handle = mKernel.fs.file(path);
    if (!handle) {
        return;
    }
    const bool fresh = !handle->exist();
    if (!handle->open(/*wMode=*/true, /*override=*/false)) {
        return;
    }
    handle->seek(handle->size());

    char   line[256];
    size_t written = 0;
    if (fresh) {
        const int header = std::snprintf(line, sizeof(line), "%s\n", Haptic::kEffectsHeader);
        if (header > 0) {
            handle->write(line, static_cast<size_t>(header), written);
        }
    }

    const Haptic::NamedEffect *named = Haptic::namedEffect(mEffect);
    const int length = std::snprintf(
        line, sizeof(line), "%u,%s,%u,%s,%s,%u,%ld,%ld,%ld,%lu,%u,%u\n", Haptic::kSchemaVersion,
        mRunId, mEffect, Haptic::effectName(mEffect),
        named != nullptr ? Haptic::familyName(named->family) : "unknown", mRepeat,
        static_cast<long>(mWindow.onsetUs()), static_cast<long>(mWindow.offsetUs()),
        static_cast<long>(mWindow.durationUs()),
        static_cast<unsigned long>(mWindow.meanDelta()), mWindow.samples(),
        Haptic::declaredDurationMs(mEffect));
    if (length > 0) {
        handle->write(line, static_cast<size_t>(length), written);
    }
    handle->flush();
    handle->close();
}

void Service::publishProgress(uint16_t samples, bool finished)
{
    auto *msg = mKernel.comm.allocateMessage<CustomMessage::SweepProgress>();
    if (msg == nullptr) {
        return;
    }
    msg->effect   = mEffect;
    msg->total    = mLastEffect;
    msg->repeat   = mRepeat;
    msg->finished = finished ? 1u : 0u;
    msg->samples  = samples;
    mKernel.comm.sendMessage(msg);
    mKernel.comm.releaseMessage(msg);
}

void Service::handleSensorData(uint16_t handle, SDK::Sensor::DataBatch &batch)
{
    if (!mImu.matchesDriver(handle) || mPhase == Phase::Idle) {
        return;
    }

    // Every sample in the batch, not the newest: keeping only the last one
    // silently discarded nine in ten as soon as a driver latency was set
    // (`RustGuiPoc/Docs/FINDINGS.md`).
    for (uint16_t i = 0; i < batch.size(); i++) {
        SDK::SensorDataParser::FusionRaw parser(batch[i]);
        if (!parser.isDataValid()) {
            continue;
        }
        SDK::SensorDataParser::FusionRaw::Data data {};
        parser.getData(data);

        Haptic::ImuSample sample {};
        sample.timestampUs = batch[i].getTimestampUs();
        sample.ax          = data.accel.x;
        sample.ay          = data.accel.y;
        sample.az          = data.accel.z;
        sample.gx          = data.gyro.x;
        sample.gy          = data.gyro.y;
        sample.gz          = data.gyro.z;

        if (mPhase == Phase::Settle) {
            mBaseline.add(sample);
            writeRawRow(sample, "settle");
        } else {
            mWindow.add(sample);
            writeRawRow(sample, "capture");
        }
    }
}

void Service::run()
{
    LOG_INFO("Started\n");

    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, kPollMs)) {
            tick(mKernel.sys.getTimeMs());
            continue;
        }

        switch (msg->getType()) {
        case SDK::MessageType::COMMAND_APP_STOP:
        case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
            LOG_INFO("Exiting\n");
            stopSweep("the app is stopping");
            if (mSensorConnected) {
                mImu.disconnect();
                mSensorConnected = false;
            }
            msg->setResult(SDK::MessageResult::SUCCESS);
            mKernel.comm.releaseMessage(msg);
            return;

        case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
            auto                  *event = static_cast<SDK::Message::Sensor::EventData *>(msg);
            SDK::Sensor::DataBatch batch(event->data, event->count, event->stride);
            handleSensorData(static_cast<uint16_t>(event->handle), batch);
            msg->setResult(SDK::MessageResult::SUCCESS);
        } break;

        case CustomMessage::SWEEP_START: {
            auto *start = static_cast<CustomMessage::SweepStart *>(msg);
            startSweep(start->runId, start->firstEffect, start->lastEffect, start->repeats);
            msg->setResult(SDK::MessageResult::SUCCESS);
        } break;

        case CustomMessage::SWEEP_STOP:
            stopSweep("the operator asked");
            msg->setResult(SDK::MessageResult::SUCCESS);
            break;

        default:
            msg->setResult(SDK::MessageResult::FAIL);
            break;
        }

        if (msg->getResult() == SDK::MessageResult::PENDING) {
            msg->setResult(SDK::MessageResult::FAIL);
        }
        mKernel.comm.releaseMessage(msg);
        tick(mKernel.sys.getTimeMs());
    }
}
