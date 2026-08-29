#ifndef MODEL_HPP
#define MODEL_HPP

#include "touchgfx/UIEventListener.hpp"

#include "SDK/Interfaces/ICustomMessageHandler.hpp"
#include "SDK/Interfaces/IGuiLifeCycleCallback.hpp"
#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include <SDK/GUI/Config.hpp>

#include "Commands.hpp"
#include "SendMsg.hpp"

// ---------------------------------------------------------------------------
// App::Config; application-level constants (timing, frame rate).
// Screens include this transitively via Presenter -> ModelListener -> Model.hpp.
// ---------------------------------------------------------------------------
namespace App::Config
{
constexpr uint32_t kFrameRate = SDK::GUI::Config::kFrameRate;
} // namespace App::Config

class FrontendApplication;
class ModelListener;

/**
 * @class Model
 * @brief GUI-side mirror of the PWM status the service publishes.

 * The status lives here and not in the View because TouchGFX destroys a screen
 * on every transition, and this run outlives any one screen.
 *
 * Unlike BacklightProbe's Model, this one never sends a backlight request of its
 * own. That app needed a GUI-side send to test whether the kernel treats the two
 * processes differently; this one writes a register, and a register does not care
 * which thread wrote it.
 */
class Model : public touchgfx::UIEventListener,
              public SDK::Interface::IGuiLifeCycleCallback,
              public SDK::Interface::ICustomMessageHandler
{
public:
    /// Plain copy of the snapshot, decoupled from the message's lifetime: the
    /// message returns to the kernel's pool as soon as customMessageHandler()
    /// returns.
    struct Status {
        uint32_t elapsedMs   = 0;
        uint32_t holdMs      = 0;
        uint32_t edges       = 0;
        uint32_t periods     = 0;
        uint32_t cyclesPerUs  = 0;
        uint32_t waveHz       = 0;
        uint32_t runElapsedMs = 0;
        uint32_t kernelWrites = 0;

        uint16_t rungIndex = 0;
        uint16_t rungCount = 0;

        uint8_t requestedDuty = 0;
        uint8_t pairDuty      = 0;

        /// What the waveform actually achieved, from microseconds spent on. Shown
        /// next to the request rather than instead of it: the gap between the two
        /// is the honest measure of a busy-wait PWM sharing a thread with a
        /// message loop.
        uint8_t achievedDuty = 0;

        CustomMessage::PwmState state = CustomMessage::PwmState::Idle;

        /// False when the app declined to drive. The screen must say so rather
        /// than showing a plausible ladder that never touched a pin.
        bool driving = false;

        char label[CustomMessage::kLabelMax] = {};

        /// When this snapshot was taken, by the GUI's own clock, so elapsed can
        /// be extrapolated forward between publishes rather than derived from
        /// the difference between two of them.
        uint32_t receivedAtMs = 0;

        /// False until the first snapshot arrives, so "not started" and "the
        /// service has not spoken" cannot look alike.
        bool everReceived = false;
    };

    Model();

    void bind(ModelListener* listener) { modelListener = listener; }

    FrontendApplication& application();
    void tick();

    /// Ask the service to climb the ladder.
    void startPwm();

    /// Ask the service to give the pin back now. The one control that matters
    /// mid-run, and the reason R1 does double duty as a stop.
    void stopPwm();

    /// Exits the GUI. The service is untouched and the run continues: leaving
    /// the screen does not abandon the experiment.
    void exitApp();

    const Status& status() const { return mStatus; }

    /// Milliseconds into the current rung, extrapolated to now.
    uint32_t liveElapsedMs() const;

protected:
    ModelListener* modelListener;

    // Fields required for GUI <-> Service communication
    const SDK::Kernel& mKernel;

    bool   mInvalidate = false;
    Status mStatus{};


    // IGuiLifeCycleCallback
    void onStart() override;
    void onResume() override;
    void onSuspend() override;
    void onStop() override;

    // ICustomMessageHandler
    bool customMessageHandler(SDK::MessageBase* msg) override;

    void setCapabilities();
};

#endif // MODEL_HPP
