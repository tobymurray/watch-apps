#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    void run();

private:
    void queryDisplayConfig();
    void renderAndPush();
    void move(int step);

    static constexpr uint32_t kPixels            = 240u * 240u;
    static constexpr uint32_t kResponseTimeoutMs = 1000;

    SDK::Kernel &mKernel;

    bool     mResumed       = false;
    bool     mDisplayUsable = false;
    bool     mDirty         = true;
    uint32_t mPage          = 0;

    uint8_t mFrameBuf[kPixels];
};

#endif // GUI_HPP
