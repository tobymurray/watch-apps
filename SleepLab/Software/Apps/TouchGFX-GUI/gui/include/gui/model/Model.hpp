#ifndef MODEL_HPP
#define MODEL_HPP

#include "touchgfx/UIEventListener.hpp"

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Interfaces/IGuiLifeCycleCallback.hpp"
#include "SDK/Interfaces/ICustomMessageHandler.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include <SDK/GUI/Config.hpp>

#include "Commands.hpp"

namespace App::Config
{
constexpr uint32_t kFrameRate = SDK::GUI::Config::kFrameRate;
} // namespace App::Config

class FrontendApplication;
class ModelListener;

/**
 * @class Model
 * @brief GUI-side mirror of what the service has published.
 *
 * The service publishes on GUI start, after every scoring epoch, and when a
 * night closes -- and nothing in between. So this holds the last snapshot
 * rather than polling for one: a report screen that asked once a second would
 * cost battery for the eight hours nobody is looking at it.
 */
class Model : public touchgfx::UIEventListener,
              public SDK::Interface::IGuiLifeCycleCallback,
              public SDK::Interface::ICustomMessageHandler
{
public:
    /// Plain copy of the report message, decoupled from its lifetime -- the
    /// message returns to the kernel's pool the moment the handler returns.
    struct Report
    {
        CustomMessage::SleepReportData data {};
        /// False until the first report lands. "Nothing heard yet" and "the
        /// service says there is no night" must not look alike: only one of
        /// them means something is wrong.
        bool everReceived = false;
    };

    /// One night, as the history list draws it.
    struct HistoryRow
    {
        int32_t startUtcDays  = 0;
        int16_t totalSleepMin = -1;
        int16_t efficiencyPct = -1;
        int16_t hrMinX10      = -1;
        uint8_t worn          = 0;
        uint8_t interrupted   = 0;
    };

    /// Held complete rather than windowed. At this size the array is under a
    /// kilobyte, while fetching rows on demand as the list scrolls would add a
    /// round-trip per keypress and an awkward question about what to draw when
    /// the history changes mid-scroll -- the same call MapManager made.
    static constexpr uint8_t kMaxHistory = 28;

    struct History
    {
        HistoryRow rows[kMaxHistory] = {};
        uint8_t    count        = 0;
        bool       everReceived = false;
    };

    Model();

    void bind(ModelListener *listener) { modelListener = listener; }

    FrontendApplication &application();
    void tick();

    /// Leaves the screen. The service keeps recording -- closing this screen
    /// at 22:45 is the normal thing to do, not a shutdown.
    void exitApp();

    const Report  &report()  const { return mReport; }
    const History &history() const { return mHistory; }

protected:
    ModelListener     *modelListener;
    const SDK::Kernel &mKernel;

    bool    mInvalidate = false;
    Report  mReport {};
    History mHistory {};

    /// Rows of the in-flight burst. A burst is only swapped into mHistory once
    /// its last row lands, so a repaint never catches the list half-rebuilt --
    /// which would show rows from two different bursts at once.
    History mIncoming {};

    // IGuiLifeCycleCallback
    void onStart()   override;
    void onResume()  override;
    void onSuspend() override;
    void onStop()    override;

    // ICustomMessageHandler
    bool customMessageHandler(SDK::MessageBase *msg) override;

    /// Fold one chunk of a history burst in, publishing once its last row
    /// arrives. Always returns true: the message was ours whether or not it
    /// completed a burst.
    bool handleHistory(const CustomMessage::SleepHistory *chunk);

    void setCapabilities();
};

#endif // MODEL_HPP
