#ifndef GUI_HPP
#define GUI_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Messages/CommandMessages.hpp"

#include "FrameBuilder.hpp"
#include "Patterns.hpp"
#include "Probe.hpp"
#include "Protocol.hpp"
#include "Recorder.hpp"
#include "Schedule.hpp"
#include "SessionConfig.hpp"
#include "haptic_lab_gui.h"

/// The half that owns the experiment: the protocol, the screens, the buttons,
/// the four clocks and every file a session writes. It plays the patterns
/// itself, because a GUI can send `RequestVibroPlay` and because `EventButton`
/// arrives here with its own timestamp, which the SDK's TouchGFX port discards.
class Gui
{
public:
    explicit Gui(SDK::Kernel &kernel);
    virtual ~Gui() = default;

    void run();

private:
    void queryDisplayConfig();
    void takeCapabilities();
    void holdTheBacklight();
    void loadConfig();
    void buildTheSession();
    void renderAndPush();

    void onTick(uint32_t nowMs);
    void onButton(const SDK::Message::EventButton &button, uint32_t nowMs);
    void onSweepProgress(uint16_t effect, uint16_t total, bool finished, uint16_t samples);

    void applyStep(const Haptic::Step &step, uint32_t nowMs);
    void playPattern(uint8_t pattern, uint16_t trial, uint32_t nowMs);
    void runProbe(uint32_t nowMs);
    void advanceProbeStep(uint32_t nowMs);
    void startSweep();
    void stopSweep();
    void openRun(const char *reason);
    void closeRun(const char *reason);
    void recordEvent(const SDK::Message::EventButton &button, uint32_t nowMs,
                     Haptic::EventDisposition disposition);
    /// Sends one `RequestVibroPlay` and reports back the two clocks only the
    /// caller can know: the tick before the send, and the tick at which the
    /// kernel said it had accepted it.
    bool sendVibro(const Haptic::Note notes[], uint8_t count, uint32_t declaredNotesCount,
                   uint32_t &tickBeforeSend, uint32_t &tickAtAccepted, bool &resultSuccess);
    uint32_t wallClock() const;

    static constexpr int16_t  kFallbackWidth     = 240;
    static constexpr int16_t  kFallbackHeight    = 240;
    static constexpr uint32_t kBytesPerPixel     = 1;
    static constexpr uint32_t kMaxPixels         = 240u * 240u;
    static constexpr uint32_t kResponseTimeoutMs = 1000;
    /// The screen has to be readable for the whole session: the wearer's arm is
    /// down between trials and the wrist-tilt gesture cannot be relied on. Zero
    /// disables auto-off on hardware, which the simulator's mock does not do.
    static constexpr uint32_t kBacklightHoldMs = 0;
    /// How long the probe screen shows that a message went out.
    static constexpr uint32_t kProbeSentMs = 1200;

    SDK::Kernel &mKernel;

    int16_t mWidth   = 0;
    int16_t mHeight  = 0;
    bool    mResumed = false;

    Haptic::SessionConfig mConfig;
    Haptic::Pattern       mPatterns[Haptic::kMaxPatterns] = {};
    uint8_t               mPatternCount                   = 0;
    Haptic::Plan          mPlan {};
    Haptic::Timing        mTiming {};
    Haptic::EffectDurations mDurations = Haptic::EffectDurations::nominal();
    Haptic::RunHeader     mRun {};
    Haptic::SessionMeta   mMeta {};
    Haptic::Recorder      mRecorder;
    Haptic::Session       mSession;
    Haptic::Mode          mMode = Haptic::Mode::Menu;

    Haptic::ProbeCase mProbes[Haptic::kProbeCaseCount] = {};
    uint8_t           mProbeCount                      = 0;
    uint8_t           mProbeIndex                      = 0;
    uint8_t           mProbeStep                       = 0;
    uint32_t          mProbeStepAt                     = 0;
    bool              mProbeRunning                    = false;

    uint16_t mSweepEffect = 0;
    uint16_t mSweepTotal  = 0;

    bool mRunOpen = false;
    /// Whether anything can be written at all, asked once at startup.
    bool mFilesUsable = false;

    uint8_t mFrameBuf[kMaxPixels * kBytesPerPixel];
};

#endif // GUI_HPP
