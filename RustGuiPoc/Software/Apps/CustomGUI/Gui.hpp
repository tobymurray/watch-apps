#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>
#include <memory>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Interfaces/IFileSystem.hpp"

#include "poc_gui.h"

class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    void run();

private:
    void queryDisplayConfig();
    void renderAndPush();
    void dumpFramebuffer();

    // Sample arrivals are recorded to a file rather than shown on a 240px
    // screen, so a run can be left going and the timing analysed afterwards.
    void recordSample(uint32_t sensorTsMs, uint16_t batch, float x, float y, float z);
    void recordRender(uint32_t atMs, uint32_t pushMs, bool ok);
    void applySensorConfig(uint32_t index);
    void flushLog();
    void flushRenderLog();
    void closeLog();

    struct RenderRow {
        uint32_t atMs;
        uint32_t pushMs;
        uint8_t  ok;
    };

    struct SensorConfig {
        uint32_t periodMs;
        uint32_t latencyMs;
    };

    // The matrix a run walks. 0 is the baseline; 20 asks whether a latency of 0
    // means "none" or "driver default"; 2000 asks whether latency moves the
    // flush interval at all; the last cell changes the period instead, which the
    // timer model predicts will change drain size and not the interval.
    static constexpr SensorConfig kSensorConfigs[] = {
        {100, 0},
        {100, 20},
        {100, 2000},
        {20, 0},
    };
    static constexpr uint32_t kSensorConfigCount =
        sizeof(kSensorConfigs) / sizeof(kSensorConfigs[0]);

    struct LogRow {
        uint32_t sensorTsMs;
        uint32_t arrivalMs;
        uint16_t seg;
        uint16_t cfg;
        uint16_t batch;
        int16_t  xMg;
        int16_t  yMg;
        int16_t  zMg;
    };

    // The sensor layer aggregates on a ~1 s timer that no app-side period or
    // latency setting moves, and delivery is lossless. Measured worst gaps are
    // 1249 ms at the default config and 2009 ms with a 2 s driver latency, so a
    // gate under that reports complete data as missing.
    static constexpr uint32_t kStaleAfterMs      = 2500;
    static constexpr int16_t  kFallbackWidth     = 240;
    static constexpr int16_t  kFallbackHeight    = 240;
    static constexpr uint32_t kBytesPerPixel     = 1;
    static constexpr uint32_t kMaxPixels         = 240u * 240u;
    static constexpr uint32_t kResponseTimeoutMs = 1000;

    SDK::Kernel &mKernel;

    int16_t  mWidth      = 0;
    int16_t  mHeight     = 0;
    uint8_t  mColorDepth = 8;
    bool     mResumed    = false;
    uint32_t mScreen     = 0;

    poc_gui_state mState{};
    uint32_t      mLastSampleMs = 0;
    bool          mHaveSample   = false;

    // ~51 s of buffer at 10 Hz, so the flush is rare compared with the sample
    // rate it is measuring and cannot itself distort the timing being recorded.
    static constexpr uint32_t kLogRows = 512;
    LogRow                    mLogRows[kLogRows];
    uint32_t                  mLogCount = 0;
    uint32_t                  mLogSeq   = 0;
    uint32_t                  mConfigIndex = 0;
    // Which visit, not which setting: cycling back to a cell must not read
    // as a continuation of the earlier one.
    uint32_t                  mSegment     = 0;
    std::unique_ptr<SDK::Interface::IFile> mLogFile;
    bool                                   mLogFailed = false;

    static constexpr uint32_t kRenderRows = 256;
    RenderRow                 mRenderRows[kRenderRows];
    uint32_t                  mRenderCount = 0;
    std::unique_ptr<SDK::Interface::IFile> mRenderFile;
    bool                                   mRenderFailed = false;

    uint8_t mFrameBuf[kMaxPixels * kBytesPerPixel];
};

#endif // GUI_HPP
