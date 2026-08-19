#ifndef MODEL_HPP
#define MODEL_HPP

#include "touchgfx/UIEventListener.hpp"

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Interfaces/IGuiLifeCycleCallback.hpp"
#include "SDK/Interfaces/ICustomMessageHandler.hpp"
#include <SDK/GUI/Config.hpp>

#include "BenchLog.hpp"
#include "BenchSuite.hpp"
#include "Canvas.hpp"
#include "Cards.hpp"
#include "Commands.hpp"

namespace App::Config
{
constexpr uint32_t kFrameRate = SDK::GUI::Config::kFrameRate;
} // namespace App::Config

class FrontendApplication;
class ModelListener;

/**
 * @class Model
 * @brief The lab bench: owns the buffers, the suite, the log and the mode.
 *
 * Everything MapLab does happens on the GUI thread, which is unusual for this
 * repository and is the whole point. The numbers that matter are what a
 * *renderer* would pay, and a renderer lives in the GUI process, draws from
 * `draw()`, and shares a thread with everything else the screen is doing. A
 * benchmark that ran in the Service would measure a machine no map will ever
 * run on.
 *
 * ---------------------------------------------------------------------------
 * WHY THE BUFFERS ARE FILE-STATIC IN THE .CPP
 *
 * Same reason `MapKit`'s TileCache is, and it is worth restating because it is
 * the one arrangement in this app that a well-meaning cleanup would undo:
 * these buffers must be arbitrated by the **linker at build time**, not by the
 * TouchGFX FrontendHeap at run time. A canvas that does not fit should fail
 * the build with a `.bss` overflow -- which is a measurement, and is exactly
 * how `SLOTS = 1` was established for the tile cache -- rather than fit into a
 * heap and fail on the wearer's wrist.
 *
 * MapLab's own footprint is NOT the budget under test. This app has a small
 * inherited GUI and can afford 149 KiB of buffers; `RunMap`, which sets the
 * shared ceiling for the three map apps, cannot be assumed to. That experiment
 * is `Tools/gate_b_link_test.sh`, and it is run against RunMap, not this.
 *
 * ---------------------------------------------------------------------------
 * ONE BENCH PER TICK
 *
 * The suite is stepped from tick(), one bench at a time, so the longest the
 * GUI thread is ever blocked by an ordinary run is one bench (a few hundred
 * milliseconds). Running the whole suite inside one call would block for the
 * best part of a minute -- which is the thing W01 exists to find the limit of,
 * not a thing this app should do to itself by accident.
 */
class Model : public touchgfx::UIEventListener,
              public SDK::Interface::IGuiLifeCycleCallback,
              public SDK::Interface::ICustomMessageHandler
{
public:
    enum class Mode : uint8_t {
        Menu = 0,   ///< choose what to do
        Running,    ///< stepping the suite
        Cards,      ///< the visual suite, one full-screen card at a time
        Stair,      ///< the watchdog staircase, one deliberate step at a time
    };

    /// Menu entries, in order.
    enum class MenuItem : uint8_t {
        RunAll = 0,
        Cards,
        Stair,
        Exit,
        Count
    };

    /// Everything the screen draws. Flat and copyable: the view formats it and
    /// holds no reference into the model.
    struct Status {
        Mode     mode        = Mode::Menu;
        uint8_t  menuIndex   = 0;

        int      benchIndex  = 0;
        int      benchTotal  = 0;
        bool     complete    = false;

        /// Last completed bench, for the two lines of feedback that make a
        /// forty-second run watchable rather than a frozen screen.
        char     lastId[8]   = { 0 };
        char     lastName[16]= { 0 };
        uint32_t lastUsPerOp = 0;
        bool     lastValid   = false;
        char     lastNote[16]= { 0 };

        uint32_t logRows     = 0;
        uint32_t logFailures = 0;
        uint32_t runIndex    = 0;
        int32_t  coldTouchMs = -1;

        uint8_t  card        = 0;
        int      stairStep   = 0;
        uint32_t stairMs     = 0;
        bool     stairArmed  = false;
    };

    Model();
    /// Unregisters from the command processor.
    ///
    /// The processor holds this object as two interface pointers, and the
    /// ordinary way out -- exitApp() -- clears them. But the app can also be
    /// stopped from outside (COMMAND_APP_STOP, or a USB connection, which
    /// terminates every running app), and then nothing clears them: the
    /// processor is left calling virtuals on an object being destroyed. The
    /// simulator shows that as `pure virtual method called` on teardown.
    ~Model() override;

    void bind(ModelListener *listener) { modelListener = listener; }

    FrontendApplication &application();
    void tick();
    void exitApp();

    const Status &status() const { return mStatus; }

    // --- what the screen's four buttons do --------------------------------
    void up();
    void down();
    void select();
    void back();

    // --- the canvas, for the card view and the blit benches ---------------
    MapLab::Canvas   &canvas() { return mCanvas; }
    const uint8_t    *canvasPixels() const;
    const uint8_t    *tileBytes() const;   ///< 256x256 ABGR2222, for B02.
    const SDK::Kernel &kernel() const { return mKernel; }

    /// A blit bench the view must time inside draw(). Returns false when
    /// there is nothing pending. Hands over the source too, because which
    /// buffer and which layout are properties of the bench, not of the view:
    /// B01 is one full-screen canvas, B02 is a 256 px raster tile blitted as
    /// the shipped mosaic.
    bool blitPending(int &benchIndex, uint32_t &repeats, const uint8_t *&source,
                     int16_t &srcW, int16_t &srcH, bool &mosaic);
    void blitComplete(int benchIndex, uint32_t iterations, uint32_t elapsedMs,
                      int32_t bytesPerBlit);

    /// Redraw the current card into the canvas.
    void drawCurrentCard();

protected:
    ModelListener     *modelListener = nullptr;
    const SDK::Kernel &mKernel;

    // IGuiLifeCycleCallback
    void onStart()   override;
    void onResume()  override;
    void onSuspend() override;
    void onStop()    override;

    // ICustomMessageHandler
    bool customMessageHandler(SDK::MessageBase *msg) override;

private:
    void setCapabilities();
    void stepSuite();
    void publish();
    static int32_t measureColdTouch(const SDK::Kernel &kernel);

    MapLab::Canvas     mCanvas;
    MapLab::BenchLog   mLog;
    MapLab::BenchSuite mSuite;

    Status   mStatus{};
    bool     mInvalidate    = false;
    int      mPendingBlit   = -1;
    bool     mRunStarted    = false;
};

#endif // MODEL_HPP
