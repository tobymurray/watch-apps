/**
 ******************************************************************************
 * @file    Service.hpp
 * @brief   BacklightProbe service: runs the plan and publishes where it is.
 ******************************************************************************
 *
 * The experiment runs here, on the service thread, for the reason FwDump's dump
 * does: a long synchronous sequence on the GUI thread trips the app-liveness
 * watchdog and reboots the watch. `poll()` does whatever has come due and
 * returns; the plan advances across many polls, and the screen is only ever told
 * about it.
 *
 * Like FwDump and unlike a typical utility app, this service does **not** end
 * when the GUI stops. The run is minutes long and includes deliberate periods
 * with the screen dark; a service that died whenever the user stopped looking
 * would abandon the experiment halfway. Leaving for good is `COMMAND_APP_STOP`.
 *
 * ## It is not read-only, and here is exactly how far that goes
 *
 * FwDump can claim to be read-only outright. This app cannot, and the difference
 * should be stated rather than glossed:
 *
 *   - Memory access is **reads only**. Peripheral registers are read through
 *     `RegisterSweep`, which writes none of them. Nothing here writes a
 *     register, a flash address, or an option byte. In particular nothing writes
 *     `FLASH_OPTR`, `FLASH_OPTKEYR` or `MPU_RNR`.
 *   - Filesystem access is **writes into the app's own sandbox** and nowhere
 *     else.
 *   - The one outward effect is `REQUEST_BACKLIGHT_SET`, which asks the kernel
 *     to change the backlight. That is a normal app message that two shipped
 *     apps already send, and the kernel remains the thing driving the hardware.
 *     This app never touches the pin.
 *
 * That last point is the boundary between this phase and the one after it. If
 * the sweep ends up showing a timer channel behind the light, driving it
 * directly would be a different category of thing entirely, and it does not
 * belong in this app.
 *
 ******************************************************************************
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <cstddef>
#include <cstdint>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include "BacklightRequest.hpp"
#include "Commands.hpp"
#include "ProbeLog.hpp"
#include "ProbePlan.hpp"

class Service : public Probe::ProbeExecutor
{
public:
    explicit Service(SDK::Kernel& kernel);

    ~Service() override = default;

    void run();

    /// One iteration of what run() does between message waits. Public so the
    /// orchestration can be driven directly: run()'s own loop blocks on the
    /// kernel message queue and never returns.
    void poll();

    /// Message-wait period run() would use next. Short while the plan is
    /// running, long when idle. Pure query.
    uint32_t nextWaitMs() const;

    /// Send one status snapshot now.
    void publish();

    // Probe::ProbeExecutor -------------------------------------------------
    void setBacklight(size_t index, const Probe::Step& step) override;
    void sweep(size_t index, const Probe::Step& step) override;
    void probeIids(size_t index, const Probe::Step& step) override;
    void note(size_t index, const Probe::Step& step) override;
    void stepBegan(size_t index, const Probe::Step& step) override;
    void planFinished() override;

private:
    /// Message wait while the plan runs.
    ///
    /// Also the granularity of the plan's timing, which is why it is this
    /// small: two consecutive instantaneous steps land one poll apart, and the
    /// cancel test in Suite 2 depends on "immediately after" meaning ten
    /// milliseconds rather than a second.
    static constexpr uint32_t kBusyWaitMs = 10;

    /// Message wait with nothing to do. The wait ends the moment a message
    /// arrives, so this bounds how long an idle service sleeps, not how fast it
    /// answers a Start.
    static constexpr uint32_t kIdleWaitMs = 1000;

    /// How often to publish while the plan runs. Faster than FwDump's, because
    /// the screen is drawing a millisecond counter that a video is read against
    /// and a stale one is worse than no counter at all.
    static constexpr uint32_t kPublishPeriodMs = 100;

    SDK::Kernel& mKernel;

    /// Held open for the whole run and flushed after every record, so an app
    /// stopped mid-experiment leaves everything up to that point on disk.
    std::unique_ptr<SDK::Interface::IFile> mResultsFile;
    std::unique_ptr<Probe::ProbeLog>       mLog;

    Probe::ProbeRunner mRunner;

    bool mGuiStarted = false;

    uint32_t mStartedAtMs     = 0;
    uint32_t mLastPublishAtMs = 0;

    /// Last state published, so a transition is published at once rather than
    /// waiting out the throttle.
    CustomMessage::ProbeState mLastPublishedState = CustomMessage::ProbeState::Idle;

    CustomMessage::ProbeState mState = CustomMessage::ProbeState::Idle;

    /// The most recent request's result, for the screen.
    SDK::MessageResult mLastResult = SDK::MessageResult::PENDING;

    /// Whether the timer bases are swept. Read from a config file at start;
    /// off unless asked for. See RegisterSweep.hpp for why.
    bool mIncludeTimers = false;

    /// The outstanding GUI-send request, or zero. Monotonic so a stale reply
    /// cannot be mistaken for a current one.
    uint32_t mGuiSendSeq        = 0;
    uint8_t  mGuiSendBrightness = 0;
    uint32_t mGuiSendAutoOffMs  = 0;
    uint32_t mGuiSendTimeoutMs  = 0;

    /// Which step asked for it, so the reply lands in the file against the right
    /// index however late it arrives.
    size_t mGuiSendStep = 0;

    size_t mObserveSteps = 0;

    void handleCommand(SDK::MessageBase* msg);
    void handleStart();
    void handleGuiSendResult(const CustomMessage::ProbeCommand& reply);

    /// Opens the results file and writes the preamble. Separate from the
    /// constructor because it touches storage, and a constructor that can fail
    /// silently is worse than a method that reports.
    void openResults();

    /// Reads the optional config that enables the unconfirmed timer bases.
    void readConfig();
};

#endif // SERVICE_HPP
