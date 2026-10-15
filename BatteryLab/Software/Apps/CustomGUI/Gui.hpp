#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "batterylab_gui.h"

class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    void run();

private:
    void queryDisplayConfig();
    void renderAndPush();

    static constexpr int16_t  kFallbackWidth     = 240;
    static constexpr int16_t  kFallbackHeight    = 240;
    static constexpr uint32_t kMaxPixels         = 240u * 240u;
    static constexpr uint32_t kResponseTimeoutMs = 1000;
    static constexpr uint8_t  kMaxBitsPerPixel   = 8;

    SDK::Kernel &mKernel;

    int16_t mWidth         = 0;
    int16_t mHeight        = 0;
    uint8_t mColorDepth    = 8;
    bool    mResumed       = false;
    bool    mDisplayUsable = true;
    bool    mDirty         = true;

    batterylab_state mState{};

    uint8_t mFrameBuf[kMaxPixels];
};

#endif // GUI_HPP
