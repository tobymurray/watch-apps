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
 * @brief GUI-side mirror of the probe status the service publishes.
 *
 * The status lives here and not in the View because TouchGFX destroys a screen
 * on every transition, and this run outlives any one screen: it is minutes long
 * and deliberately spends part of that with the display blanked.
 *
 * A polled mirror rather than an event-driven one. The service publishes on a
 * fixed interval while the plan runs, so a repeat snapshot carrying the same
 * state is normal and expected rather than a redundant event.
 *
 * ## The one thing this Model does that a mirror would not
 *
 * It can be asked to send a backlight request itself. `REQUEST_BACKLIGHT_SET`
 * sits directly below a block of message types commented "Display control (GUI
 * only)", both shipped callers are services, and nobody has checked whether the
 * comment reaches it. So the plan has one step that has to originate in the GUI
 * process, and the service asks for it through `ProbeStatus::guiSendSeq`.
 *
 * The send is deferred to `tick()` rather than done in the message handler.
 * It blocks for up to its send timeout waiting for the kernel to answer, and
 * blocking inside a custom message handler is blocking on whatever thread the
 * command processor dispatches on. `tick()` is the GUI's own turn, which is
 * where a bounded wait belongs.
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
        uint32_t stepElapsedMs  = 0;
        uint32_t stepDurationMs = 0;
        uint32_t startedAtMs    = 0;

        uint16_t stepIndex    = 0;
        uint16_t stepCount    = 0;
        uint16_t observeSteps = 0;

        CustomMessage::ProbeState state = CustomMessage::ProbeState::Idle;

        uint8_t action     = 0;
        uint8_t lastResult = 0;

        /// Whether the screen must hold still. Obeyed rather than decided here:
        /// the reason lives in the plan, and a repaint is a message to the same
        /// kernel that owns the backlight.
        bool quiet = true;

        bool registersAvailable = false;
        bool logIntact          = true;

        char label[CustomMessage::kLabelMax] = {};

        /// When this snapshot was taken, by the GUI's own clock. The screen
        /// extrapolates the millisecond counter from here, so it can move faster
        /// than the publish rate without ever being derived from the difference
        /// between two snapshots.
        uint32_t receivedAtMs = 0;

        /// False until the first snapshot arrives. "The run has not started" and
        /// "the service has not spoken yet" must not look alike: one of them is
        /// a screen offering a button that will not be heard.
        bool everReceived = false;
    };

    Model();

    void bind(ModelListener* listener) { modelListener = listener; }

    FrontendApplication& application();
    void tick();

    /// Ask the service to run the plan. Fire-and-forget; a second press is
    /// ignored by the service, and the screen declines it locally as well.
    void startProbe();

    /// Exits the GUI. The service is untouched and the run continues: leaving
    /// the screen does not abandon the experiment.
    void exitApp();

    const Status& status() const { return mStatus; }

    /// Milliseconds into the current step, extrapolated to now. What the screen
    /// draws during an OBSERVE step, and the number a video is read against.
    uint32_t liveStepElapsedMs() const;

protected:
    ModelListener* modelListener;

    // Fields required for GUI <-> Service communication
    const SDK::Kernel& mKernel;

    bool   mInvalidate = false;
    Status mStatus{};

    /// A GUI-send the service has asked for and this Model has not done yet.
    /// Zero when there is nothing outstanding.
    uint32_t mPendingSendSeq        = 0;
    uint8_t  mPendingSendBrightness = 0;
    uint32_t mPendingSendAutoOffMs  = 0;
    uint32_t mPendingSendTimeoutMs  = 0;

    /// The last sequence actually acted on, so a re-published snapshot carrying
    /// the same request does not send it twice.
    uint32_t mLastHandledSendSeq = 0;

    // IGuiLifeCycleCallback
    void onStart() override;
    void onResume() override;
    void onSuspend() override;
    void onStop() override;

    // ICustomMessageHandler
    bool customMessageHandler(SDK::MessageBase* msg) override;

    void setCapabilities();

    /// Performs an outstanding GUI-send and reports the outcome back.
    void doPendingGuiSend();
};

#endif // MODEL_HPP
