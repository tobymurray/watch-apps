#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Messages/CommandMessages.hpp"

/**
 * @brief The button half of the doorbell probe.
 *
 * Four buttons, four questions. R1 clicked three times is the gesture the whole
 * investigation is about; the other three isolate one variable each so a run can
 * say which of them the wire frame depended on.
 */
class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    void run();

private:
    /** What the last ring was, which is all the screen colour has to say. */
    enum class Last : uint8_t {
        NONE = 0,
        WITH_FILE,
        NO_FILE,
        VIA_SERVICE,
        BURST,
    };

    void queryDisplayConfig();
    void render();

    void onButton(const SDK::Message::EventButton &btn);
    void countGestureClick(uint32_t now);

    /** Ring from this process. [writeFile] decides whether a mailbox exists first. */
    void ringFromGui(bool writeFile);
    void ringViaService();
    bool writeCommandFile();
    void vibrate(uint8_t effect);

    static constexpr int16_t  kFallbackWidth     = 240;
    static constexpr int16_t  kFallbackHeight    = 240;
    static constexpr uint32_t kMaxPixels         = 240u * 240u;
    static constexpr uint32_t kResponseTimeoutMs = 1000;

    // Three clicks have to land inside this to count as one gesture.
    static constexpr uint32_t kGestureWindowMs = 2000;
    static constexpr uint8_t  kGestureClicks   = 3;

    // Sandbox-relative: no absolute volume path resolves on this device.
    static constexpr const char *kCommandPath = "/command.json";

    SDK::Kernel &mKernel;

    int16_t mWidth      = 0;
    int16_t mHeight     = 0;
    uint8_t mColorDepth = 8;
    bool    mResumed    = false;
    bool    mDirty      = true;

    uint32_t mRings         = 0;
    uint8_t  mPending       = 0;
    uint32_t mWindowStartMs = 0;
    Last     mLast          = Last::NONE;

    uint8_t mFrameBuf[kMaxPixels];
};

#endif // GUI_HPP
