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

    /// One roster row, as the list shows it.
    struct PackRow {
        char    name[CustomMessage::kMaxRowNameLen] = {};
        uint8_t state = static_cast<uint8_t>(CustomMessage::PackState::Pending);
    };

    /// The whole roster, held complete rather than windowed.
    ///
    /// The list only ever draws the handful of rows that fit, but the data
    /// behind it stays whole: at this size the array costs well under a
    /// kilobyte, while fetching rows on demand as the list scrolls would add a
    /// round-trip per keypress and an awkward question about what to draw when
    /// the roster changes mid-scroll.
    struct Roster {
        PackRow  rows[CustomMessage::kMaxRosterPacks] = {};
        uint16_t count        = 0;
        bool     everReceived = false; ///< False until a complete burst arrives.
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
    const Roster   &roster()   const { return mRoster; }

protected:
    ModelListener* modelListener;

    // Fields required for GUI <-> Service communication
    const SDK::Kernel& mKernel;

    bool     mInvalidate = false;
    Progress mProgress{};
    Roster   mRoster{};

    /// Rows of the in-flight burst seen so far. A burst is only swapped into
    /// mRoster once its last row lands, so a repaint never catches the list
    /// half-rebuilt -- which would otherwise show rows from two different
    /// rosters at once while packs are being added.
    Roster   mIncoming{};

    // IGuiLifeCycleCallback
    void onStart()   override;
    void onResume()  override;
    void onSuspend() override;
    void onStop()    override;

    // ICustomMessageHandler
    bool customMessageHandler(SDK::MessageBase *msg) override;

    /// Fold one row of a roster burst in, publishing the result once the
    /// burst's last row arrives. Always returns true: the message was ours
    /// whether or not it completed a roster.
    bool handlePackStatus(const CustomMessage::MapManagerPackStatus *chunk);

    void setCapabilities();
};

#endif // MODEL_HPP
