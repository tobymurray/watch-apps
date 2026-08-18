#ifndef MODEL_HPP
#define MODEL_HPP

#include "touchgfx/UIEventListener.hpp"

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Interfaces/IGuiLifeCycleCallback.hpp"
#include "SDK/Interfaces/ICustomMessageHandler.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include <SDK/GUI/Config.hpp>

#include "Commands.hpp"

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
 * @brief GUI-side mirror of the probe's status.
 *
 * The service publishes one of these when the GUI attaches and again after
 * every row it writes, and nothing in between -- so this holds the last
 * snapshot rather than polling for one. A minute between updates is the right
 * cadence for a screen that answers "is this recording?": faster would cost
 * battery for the eight hours nobody is looking.
 */
class Model : public touchgfx::UIEventListener,
              public SDK::Interface::IGuiLifeCycleCallback,
              public SDK::Interface::ICustomMessageHandler
{
public:
    /// Plain copy of the message's fields, decoupled from its lifetime -- the
    /// message goes back to the kernel's pool the moment the handler returns.
    struct Status {
        uint32_t rowsWritten   = 0;
        uint32_t rowFailures   = 0;
        uint32_t bytesWritten  = 0;
        uint32_t runningMs     = 0;
        uint16_t subscribed    = 0;
        uint16_t hrMode        = 0;
        int32_t  lastAccN      = -1;
        int32_t  lastHrN       = -1;
        int32_t  lastTouchWorn = -1;
        int32_t  lastTouchN    = -1;
        uint32_t totalBeatN    = 0;
        uint32_t totalSpo2N    = 0;
        int32_t  battPctX10    = -1;
        int8_t   charging      = -1;
        int8_t   usb           = -1;

        /// False until the first snapshot lands. "Nothing has been heard yet"
        /// and "the service says nothing is arriving" must not look alike:
        /// only one of them means the run is broken.
        bool     everReceived  = false;
    };

    Model();

    void bind(ModelListener *listener) { modelListener = listener; }

    FrontendApplication &application();
    void tick();

    /**
     * @brief Leaves the GUI. The service keeps recording -- closing this
     *        screen is the normal thing to do before going to sleep.
     */
    void exitApp();

    const Status &status() const { return mStatus; }

protected:
    ModelListener     *modelListener;
    const SDK::Kernel &mKernel;

    bool   mInvalidate = false;
    Status mStatus{};

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
