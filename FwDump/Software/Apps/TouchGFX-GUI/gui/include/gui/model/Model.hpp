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
// App::Config -- application-level constants (timing, frame rate).
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
 * @brief GUI-side mirror of the dump status the service publishes.
 *
 * The status lives here and not in the View for a specific reason: TouchGFX
 * destroys a screen on every transition, so anything a View remembers is lost
 * the moment the user navigates. A dump runs for minutes across screen blanks;
 * its progress has to outlive any one screen.
 *
 * This is a polled mirror rather than an event-driven one. The service sends a
 * fresh snapshot on a fixed interval while a dump runs, because "bytes done"
 * changes continuously -- so unlike most models in this SDK, a repeat message
 * carrying the same state is normal and expected, not a redundant event.
 */
class Model : public touchgfx::UIEventListener,
              public SDK::Interface::IGuiLifeCycleCallback,
              public SDK::Interface::ICustomMessageHandler
{
public:
    /// Plain copy of the snapshot, decoupled from the message's lifetime (the
    /// message goes back to the kernel's pool as soon as
    /// customMessageHandler() returns).
    struct Status {
        uint64_t bytesDone  = 0;
        uint64_t bytesTotal = 0;

        uint32_t elapsedMs  = 0;
        uint32_t etaSec     = 0;
        uint32_t kbPerSec   = 0;
        uint32_t wholeCrc   = 0;
        uint32_t regionBase = 0;
        uint32_t regionSize = 0;
        uint32_t stalledMs  = 0;

        uint16_t chunksDone     = 0;
        uint16_t chunksTotal    = 0;
        uint16_t chunksVerified = 0;
        uint16_t chunksPresent  = 0;
        uint16_t errorChunk     = 0;

        CustomMessage::DumpState state = CustomMessage::DumpState::Idle;
        CustomMessage::DumpError error = CustomMessage::DumpError::None;

        uint8_t configStatus = 0;
        bool    scanComplete = false;

        /// False until the first snapshot arrives. The difference between "the
        /// dump has not started" and "the service has not spoken yet" -- which
        /// must not look alike, since one of them is a screen telling the user
        /// to press a button that will not be heard.
        bool everReceived = false;
    };

    Model();

    void bind(ModelListener* listener) { modelListener = listener; }

    FrontendApplication& application();
    void tick();

    /**
     * @brief Ask the service to begin (or resume) the dump.
     *
     * Fire-and-forget. A second press while a dump runs is ignored by the
     * service, so the screen does not have to guard it -- but it does anyway,
     * because the screen knows its own state and can decline without a round
     * trip.
     */
    void startDump();

    /**
     * @brief Exits the GUI. The service is untouched and keeps dumping --
     *        leaving the screen does not abandon a run in progress.
     */
    void exitApp();

    const Status& status() const { return mStatus; }

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
