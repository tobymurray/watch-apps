#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "Barcode.hpp"
#include "barcode_gui.h"

class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    void run();

private:
    void queryDisplayConfig();
    void renderAndPush();
    void cycle(int delta);
    void buildFrame(barcode_gui_frame &out) const;

    uint8_t lastIndex() const;
    void    rememberIndex(uint8_t index);

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

    Barcode::State mState;
    uint8_t        mIndex       = 0;
    bool           mIndexLoaded = false;

    uint8_t mFrameBuf[kMaxPixels * kBytesPerPixel];
};

#endif // GUI_HPP
