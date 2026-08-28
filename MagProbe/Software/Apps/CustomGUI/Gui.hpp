#ifndef GUI_HPP
#define GUI_HPP

#include "Canvas.hpp"
#include "Render.hpp"

#include "SDK/Kernel/Kernel.hpp"

#include <cstdint>

/**
 * @brief The GUI half: pixels and buttons, and nothing else.
 *
 * Through the SDK's CustomGUI entry point, which asks only for a class with a
 * kernel constructor and a `run()`. No TouchGFX is linked: this app's screens
 * are rows of text and one needle, and the ~120 files of generated scaffolding
 * a TouchGFX front end needs would be most of the app.
 *
 * A GUI process cannot reach a sensor, so everything drawn here arrives over the
 * message bus from the Service.
 */
class Gui {
public:
    explicit Gui(SDK::Kernel& kernel);
    virtual ~Gui() = default;

    void run();

private:
    static constexpr uint32_t kResponseTimeoutMs = 1000;
    static constexpr int16_t  kFallbackWidth     = 240;
    static constexpr int16_t  kFallbackHeight    = 240;
    static constexpr uint32_t kMaxPixels         = 240u * 240u;

    /// Past this with no status message, the Service is not talking. Several
    /// status periods, so an ordinary gap does not trip it.
    static constexpr uint32_t kServiceStaleMs = 3000;

    void queryDisplayConfig();
    void renderAndPush();
    void sendControl(uint8_t action);
    void handleButton(const SDK::MessageBase* msg);

    SDK::Kernel& mKernel;

    int16_t mWidth      = 0;
    int16_t mHeight     = 0;
    uint8_t mColorDepth = 8;
    bool    mResumed    = false;

    Render::Screen mScreen = Render::Screen::Verdict;
    Render::View   mView{};

    uint32_t mLastStatusMs = 0;
    bool     mHaveStatus   = false;
    uint32_t mFrames       = 0;

    uint8_t mFrameBuf[kMaxPixels];
};

#endif // GUI_HPP
