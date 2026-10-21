#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Messages/CommandMessages.hpp"

#include "slumber_gui.h"

class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    void run();

private:
    void queryDisplayConfig();
    void renderAndPush();
    void sendMark();
    void move(int step);
    void onButton(SDK::Message::EventButton::Id id);
    void show(uint8_t screen);

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

    bool mMarkPending = false;

    uint8_t  mSequence    = 0;  ///< presses of the picker's sequence made so far
    uint32_t mLastPressMs = 0;

    slumber_state mState{};

    uint8_t mFrameBuf[kMaxPixels];
};

#endif // GUI_HPP
