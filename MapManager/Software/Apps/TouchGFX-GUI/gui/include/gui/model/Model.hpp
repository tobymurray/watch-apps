#ifndef MODEL_HPP
#define MODEL_HPP

#include "touchgfx/UIEventListener.hpp"

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Interfaces/IGuiLifeCycleCallback.hpp"
#include "SDK/Interfaces/ICustomMessageHandler.hpp"
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
 * @brief GUI-side mirror of the progress snapshot the service publishes.
 *
 * Unlike a typical GUI-side mirror in this SDK, this one IS effectively
 * polled at a distance: the service sends a fresh snapshot on a fixed
 * interval (see Service.cpp) rather than only on a discrete state
 * transition, since "bytes done" changes continuously while a scan runs.
 */
class Model : public touchgfx::UIEventListener,
              public SDK::Interface::IGuiLifeCycleCallback,
              public SDK::Interface::ICustomMessageHandler
{
public:
    /// Plain copy of the fields the GUI actually needs, decoupled from the
    /// message's lifetime (the message itself is released back to the
    /// kernel's pool right after customMessageHandler() returns).
    struct Progress {
        char     packName[CustomMessage::kMaxPackNameLen] = {};
        uint64_t bytesDone     = 0;
        uint64_t bytesTotal    = 0;
        uint32_t elapsedMs     = 0;
        uint16_t packsVerified = 0;
        uint16_t packsTotal    = 0;
        bool     anyInProgress = false;
        bool     everReceived  = false; ///< False until the first snapshot arrives.
    };

    Model();

    void bind(ModelListener *listener)
    {
        modelListener = listener;
    }

    FrontendApplication &application();
    void tick();

    /**
     * @brief Exits the application. The service is untouched and keeps
     *        verifying in the background -- leaving the GUI does not pause
     *        or lose any progress.
     */
    void exitApp();

    const Progress &progress() const { return mProgress; }

protected:
    ModelListener* modelListener;

    // Fields required for GUI <-> Service communication
    const SDK::Kernel& mKernel;

    bool     mInvalidate = false;
    Progress mProgress{};

    // IGuiLifeCycleCallback
    void onStart()   override;
    void onResume()  override;
    void onSuspend() override;
    void onStop()    override;

    // ICustomMessageHandler
    bool customMessageHandler(SDK::MessageBase *msg) override;

    void setCapabilities();
};

#endif // MODEL_HPP
