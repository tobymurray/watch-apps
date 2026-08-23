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
    void flushLog();
    void flushRenderLog();
    void closeLog();

    struct RenderRow {
        uint32_t atMs;
        uint32_t pushMs;
        uint8_t  ok;
    };

    struct LogRow {
        uint32_t sensorTsMs;
        uint32_t arrivalMs;
        uint16_t batch;
        int16_t  xMg;
        int16_t  yMg;
        int16_t  zMg;
    };

    static constexpr uint32_t kStaleAfterMs      = 500;
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
