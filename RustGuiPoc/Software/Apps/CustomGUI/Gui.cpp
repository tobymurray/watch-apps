#include "Gui.hpp"

#include <cstdio>

#include "Commands.hpp"

#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Timer/Timer.hpp"

#include "poc_gui.h"

#define LOG_MODULE_PRX   "RustGui"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

static constexpr uint32_t kWaitForever = 0xFFFFFFFF;
// Paths are relative to the app's own directory, which already exists.
static constexpr char     kLogPath[]    = "accel_log.csv";
static constexpr char     kFbDumpPath[] = "fb_dump.bin";

extern "C" void poc_gui_host_panic(const uint8_t *msg, uint32_t len)
{
    LOG_ERROR("Rust panic: %.*s\n", static_cast<int>(len),
              reinterpret_cast<const char *>(msg));
    SDK::KernelProviderGUI::GetInstance().getKernel().sys.exit(1);
    while (true) {
    }
}

Gui::Gui(SDK::Kernel &kernel)
    : mKernel(kernel)
{
}

void Gui::queryDisplayConfig()
{
    auto *cfg = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayConfig>();
    if (cfg) {
        if (mKernel.comm.sendMessage(cfg, kResponseTimeoutMs) &&
            cfg->getResult() == SDK::MessageResult::SUCCESS) {
            mWidth      = cfg->width;
            mHeight     = cfg->height;
            mColorDepth = cfg->colorDepth;
        }
        mKernel.comm.releaseMessage(cfg);
    }

    const bool fitsFramebuffer =
        mWidth > 0 && mHeight > 0 &&
        static_cast<uint32_t>(mWidth) * static_cast<uint32_t>(mHeight) <= kMaxPixels;

    if (!fitsFramebuffer) {
        LOG_WARNING("Display config unusable (%dx%d); falling back to %dx%d\n",
                    mWidth, mHeight, kFallbackWidth, kFallbackHeight);
        mWidth  = kFallbackWidth;
        mHeight = kFallbackHeight;
    }
    LOG_INFO("Display %dx%d @ %ubpp\n", mWidth, mHeight, mColorDepth);
}

void Gui::renderAndPush()
{
    if (!mResumed) {
        return;
    }

    // One clock read. A displayed age and a freshness verdict sampled at
    // different instants can contradict each other on screen, which discredits
    // the diagnostics exactly when they are being read.
    const uint32_t age =
        mHaveSample ? SDK::Timer::elapsed(mKernel.sys.getTimeMs(), mLastSampleMs) : 0;

    mState.sample_age_ms = age;
    mState.valid         = (mHaveSample && age <= kStaleAfterMs) ? 1u : 0u;
    if (age > mState.sample_age_max_ms) {
        mState.sample_age_max_ms = age;
    }

    poc_gui_render(mFrameBuf,
                   kMaxPixels * kBytesPerPixel,
                   static_cast<uint16_t>(mWidth),
                   static_cast<uint16_t>(mHeight),
                   mScreen,
                   &mState);

    auto *upd = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (upd) {
        upd->pBuffer = mFrameBuf;
        mKernel.comm.sendMessage(upd, kResponseTimeoutMs);
        mKernel.comm.releaseMessage(upd);
    }
    ++mState.frames;
}

void Gui::recordSample(uint32_t sensorTsMs, uint16_t batch, float x, float y, float z)
{
    if (mLogFailed) {
        return;
    }

    LogRow &row   = mLogRows[mLogCount++];
    row.sensorTsMs = sensorTsMs;
    row.arrivalMs  = mKernel.sys.getTimeMs();
    row.batch      = batch;
    row.xMg        = static_cast<int16_t>(x * 1000.0f);
    row.yMg        = static_cast<int16_t>(y * 1000.0f);
    row.zMg        = static_cast<int16_t>(z * 1000.0f);

    if (mLogCount == kLogRows) {
        flushLog();
    }
}

void Gui::flushLog()
{
    if (mLogCount == 0 || mLogFailed) {
        mLogCount = 0;
        return;
    }

    if (!mLogFile) {
        mLogFile = mKernel.fs.file(kLogPath);
        // override=true: each run starts a fresh log, so a run is one experiment.
        if (!mLogFile || !mLogFile->open(/*wMode=*/true, /*override=*/true)) {
            LOG_WARNING("accel log: open '%s' failed; logging off\n", kLogPath);
            mLogFile.reset();
            mLogFailed = true;
            mLogCount  = 0;
            return;
        }
        static constexpr char kHeader[] = "seq,sensor_ts_ms,arrival_ms,batch,x_mg,y_mg,z_mg\n";
        size_t written = 0;
        mLogFile->write(kHeader, sizeof(kHeader) - 1, written);
    }

    for (uint32_t i = 0; i < mLogCount; ++i) {
        const LogRow &row = mLogRows[i];
        char line[80];
        const int len = snprintf(line, sizeof(line), "%u,%u,%u,%u,%d,%d,%d\n",
                                 static_cast<unsigned>(mLogSeq++),
                                 static_cast<unsigned>(row.sensorTsMs),
                                 static_cast<unsigned>(row.arrivalMs),
                                 static_cast<unsigned>(row.batch),
                                 static_cast<int>(row.xMg),
                                 static_cast<int>(row.yMg),
                                 static_cast<int>(row.zMg));
        if (len > 0) {
            size_t written = 0;
            mLogFile->write(line, static_cast<size_t>(len), written);
        }
    }
    mLogFile->flush();
    LOG_INFO("accel log: +%u rows (%u total)\n",
             static_cast<unsigned>(mLogCount), static_cast<unsigned>(mLogSeq));
    mLogCount = 0;
}

void Gui::closeLog()
{
    flushLog();
    if (mLogFile) {
        mLogFile->close();
        mLogFile.reset();
    }
}

void Gui::dumpFramebuffer()
{
    auto file = mKernel.fs.file(kFbDumpPath);
    if (!file || !file->open(/*wMode=*/true, /*override=*/true)) {
        LOG_WARNING("fb dump: open '%s' failed\n", kFbDumpPath);
        return;
    }

    const size_t bytes = static_cast<size_t>(mWidth) *
                         static_cast<size_t>(mHeight) * kBytesPerPixel;
    size_t written = 0;
    const bool ok = file->write(reinterpret_cast<const char *>(mFrameBuf), bytes, written);
    file->flush();
    file->close();

    LOG_INFO("fb dump: %s screen=%u %dx%d -> %u/%u bytes %s\n",
             kFbDumpPath, static_cast<unsigned>(mScreen), mWidth, mHeight,
             static_cast<unsigned>(written), static_cast<unsigned>(bytes),
             (ok && written == bytes) ? "OK" : "FAIL");
}

void Gui::run()
{
    LOG_INFO("Started\n");

    if (poc_gui_state_size() != sizeof(poc_gui_state)) {
        LOG_ERROR("ABI mismatch: Rust state %u bytes, C++ %u -- stale libpoc_gui.a\n",
                  static_cast<unsigned>(poc_gui_state_size()),
                  static_cast<unsigned>(sizeof(poc_gui_state)));
        mKernel.sys.exit(1);
        return;
    }

    queryDisplayConfig();
    const uint32_t screenCount = poc_gui_screen_count();

    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, kWaitForever)) {
            continue;
        }

        switch (msg->getType()) {

            case SDK::MessageType::COMMAND_APP_STOP:
                closeLog();
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                mKernel.sys.exit(0);
                return;

            case SDK::MessageType::COMMAND_APP_GUI_RESUME:
                mResumed = true;
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            case SDK::MessageType::COMMAND_APP_GUI_SUSPEND:
                mResumed = false;
                flushLog();
                msg->setResult(SDK::MessageResult::SUCCESS);
                break;

            case SDK::MessageType::EVENT_GUI_TICK:
                msg->setResult(SDK::MessageResult::SUCCESS);
                mKernel.comm.releaseMessage(msg);
                renderAndPush();
                continue;

            case CustomMessage::ACCEL_VALUES: {
                auto *accel = static_cast<CustomMessage::AccelValues *>(msg);
                mState.accel_x_g = accel->x_g;
                mState.accel_y_g = accel->y_g;
                mState.accel_z_g = accel->z_g;
                mLastSampleMs    = mKernel.sys.getTimeMs();
                mHaveSample      = true;
                ++mState.samples;
                recordSample(accel->sensor_ts_ms, accel->batch_size,
                             accel->x_g, accel->y_g, accel->z_g);
                msg->setResult(SDK::MessageResult::SUCCESS);
            } break;

            case SDK::MessageType::EVENT_BUTTON: {
                auto *btn = static_cast<SDK::Message::EventButton *>(msg);
                using Id    = SDK::Message::EventButton::Id;
                using Event = SDK::Message::EventButton::Event;

                if (btn->event == Event::CLICK && btn->id == Id::SW4) {
                    LOG_INFO("Back pressed; exiting\n");
                    closeLog();
                    msg->setResult(SDK::MessageResult::SUCCESS);
                    mKernel.comm.releaseMessage(msg);
                    mKernel.sys.exit(0);
                    return;
                }

                if (btn->event == Event::CLICK && btn->id == Id::SW2 && screenCount > 0) {
                    mScreen = (mScreen + 1) % screenCount;
                    renderAndPush();
                } else if (btn->event == Event::LONG_PRESS && btn->id == Id::SW3) {
                    dumpFramebuffer();
                    flushLog();
                }
                msg->setResult(SDK::MessageResult::SUCCESS);
            } break;

            default:
                msg->setResult(SDK::MessageResult::FAIL);
                mKernel.comm.sendResponse(msg);
                break;
        }

        if (msg->getResult() == SDK::MessageResult::PENDING) {
            msg->setResult(SDK::MessageResult::FAIL);
        }
        mKernel.comm.releaseMessage(msg);
    }
}
