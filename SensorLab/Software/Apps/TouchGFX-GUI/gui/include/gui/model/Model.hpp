#ifndef MODEL_HPP
#define MODEL_HPP

#include "touchgfx/UIEventListener.hpp"

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Interfaces/IGuiLifeCycleCallback.hpp"
#include "SDK/Interfaces/ICustomMessageHandler.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include <SDK/GUI/Config.hpp>

#include "Catalogue/Catalogue.hpp"
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
 * @brief GUI-side mirror of the instrument's state.
 *
 * The service publishes a status plus the roster as an indexed burst when the
 * GUI attaches, after each interval, and after each command -- and nothing in
 * between. So this holds the last snapshot rather than polling for one, which
 * matters for an instrument whose screen is open for a minute of every twelve
 * hours: publishing into a void the rest of the time would cost battery, and it
 * would charge that cost to the measurement.
 *
 * The roster is reassembled **by index**, not by arrival order. A burst that
 * lost its middle message must leave a gap rather than shift every row after it,
 * which is why `SensorRoster` carries `first` and `total` and why `mHaveRow`
 * exists. SleepLab's ledger row T2 is what a burst contract that relies on
 * ordering looks like when it goes wrong: it fails silently and looks like data.
 */
class Model : public touchgfx::UIEventListener,
              public SDK::Interface::IGuiLifeCycleCallback,
              public SDK::Interface::ICustomMessageHandler
{
public:
    /// Plain copies of the messages' payloads, decoupled from their lifetimes:
    /// `SDK::MessageBase` deletes copy-construction and the block goes back to
    /// the kernel's pool the moment the handler returns (ledger row P12).
    struct State
    {
        CustomMessage::SensorLabStatusData status {};

        /// One row per declared sensor type, indexed by catalogue position.
        CustomMessage::RosterRow rows[SensorLab::Catalogue::kTypeCount] {};
        /// Whether a burst has actually delivered each row. A row that has not
        /// arrived is drawn as unknown rather than as a zeroed measurement --
        /// zeroed memory reads as "resolves nothing, delivers nothing", which is
        /// a finding, and inventing one would be the worst thing this screen
        /// could do.
        bool rowsSeen[SensorLab::Catalogue::kTypeCount] {};
        /// Rows the service says exist, from the burst header.
        uint8_t total = 0;
    };

    Model();

    void bind(ModelListener *listener) { modelListener = listener; }

    FrontendApplication &application();
    void tick();

    /// Leave the GUI. A soak keeps recording: closing this screen before a
    /// twelve-hour run is the normal thing to do.
    void exitApp();

    const State &state() const { return mState; }

    /// Ask the service to publish now rather than at its next interval.
    void requestUpdate();
    /// Start a sweep, start a soak, or stop the run.
    void send(CustomMessage::Command command);

protected:
    ModelListener     *modelListener;
    const SDK::Kernel &mKernel;

    bool  mInvalidate = false;
    State mState {};

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
