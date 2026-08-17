/**
 ******************************************************************************
 * @file    Service.hpp
 * @brief   FwDump service: drives the dump and publishes what it is doing.
 ******************************************************************************
 *
 * The dump runs here, on the service thread, and not on the GUI thread. That is
 * not tidiness: a long synchronous loop on the GUI thread trips the app-liveness
 * watchdog and reboots the watch, which is exactly what happened to MapManager
 * verifying a large pack. The service does bounded slices of work between
 * message waits, and the screen is only ever told about it.
 *
 * Unlike a typical utility app's service, this one does NOT end itself when the
 * GUI stops. A 4 MB dump takes minutes, the screen blanks long before that, and
 * a dump that died whenever the user stopped looking at it would be useless.
 * Leaving the app for good is COMMAND_APP_STOP, which does end it -- with every
 * completed chunk still on disk for the next run to resume from.
 *
 ******************************************************************************
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "SDK/Kernel/Kernel.hpp"

#include "Commands.hpp"
#include "DumpConfig.hpp"
#include "DumpRegion.hpp"
#include "FlashDumper.hpp"

class Service
{
public:
    Service(SDK::Kernel& kernel);

    virtual ~Service() = default;

    void run();

    /// One iteration of the work run() does between message waits: advance the
    /// dumper by a slice, notice a stall, publish if due. Public so the
    /// orchestration can be driven directly -- run()'s own loop blocks on the
    /// kernel message queue and never returns, so it cannot be exercised as a
    /// unit.
    void poll();

    /// Message-wait period run() would use next: short while there is dumping
    /// to get back to, longer when idle. Pure query.
    uint32_t nextWaitMs() const;

    /// Send one status snapshot now. Public for the same reason poll() is.
    void publish();

private:
    /// How many bytes one poll() hands to FlashDumper::step().
    ///
    /// This, and not the message-wait period, is what sets throughput. The loop
    /// is gated by a kernel message wait, so a one-sub-write-per-wait design
    /// would tie the rate to the wait rather than to the storage -- the same
    /// ~80 KB/s ceiling MapManager measured before it started budgeting per call
    /// instead of per chunk. 64 KB is roughly 50ms of I/O at the rate this
    /// storage actually sustains, which is also the ceiling it puts on how long
    /// the service can go without answering a message.
    static constexpr size_t kSliceBudgetBytes = 64 * 1024;

    /// Message wait while there is work pending. Short because there is real
    /// work to return to, not to poll: each iteration does a full slice of I/O,
    /// so this is not a spin.
    static constexpr uint32_t kBusyWaitMs = 10;

    /// Message wait with nothing to do. Long enough not to cost power, short
    /// enough that a Start pressed on the screen is acted on immediately -- the
    /// wait ends the moment a message arrives, so this only bounds how long an
    /// idle service sleeps, not how quickly it responds.
    static constexpr uint32_t kIdleWaitMs = 1000;

    /// How often to publish a snapshot while a dump runs. Once or twice a second
    /// is the cadence a person reads a progress screen at; publishing per
    /// sub-write would make the message queue, not the storage, the bottleneck
    /// of the dump.
    static constexpr uint32_t kPublishPeriodMs = 500;

    /// A gap between two consecutive slices longer than this means something
    /// stopped the app rather than merely delayed it. Connecting USB is the
    /// expected cause and the one worth telling the user about.
    ///
    /// Comfortably above any legitimate gap: a slice is ~50ms of I/O behind a
    /// 10ms wait, so even a very slow chunk boundary is two orders of magnitude
    /// under this.
    static constexpr uint32_t kStallThresholdMs = 4000;

    SDK::Kernel& mKernel;

    DumpConfig::Status mConfigStatus = DumpConfig::Status::Default;
    DumpRegion         mRegion{};

#if defined(SIMULATOR)
    /// Stands in for flash on a host build, where 0x08000000 is not mapped and
    /// dereferencing it would end the process rather than read anything. Filled
    /// with a deterministic pattern so the screen has plausible, stable content
    /// to render and the CRCs are reproducible across runs.
    ///
    /// The one place the simulator and the watch genuinely differ, and the
    /// reason the simulator can honestly exercise the screen but not the read.
    std::vector<uint8_t> mSyntheticFlash;
#endif

    /// Where the region is in this process's address space. On the watch, the
    /// identity mapping of mRegion.base.
    const uint8_t* mWindow = nullptr;

    /// Constructed after mRegion and mWindow are settled, so it is held by
    /// pointer rather than by value.
    std::unique_ptr<FlashDumper> mDumper;

    bool mGuiStarted = false;

    uint32_t mStartedAtMs    = 0;
    uint32_t mLastPublishAtMs = 0;
    uint32_t mLastSliceAtMs  = 0;
    uint32_t mStalledMs      = 0;

    /// Last state published, so a transition can be published immediately
    /// rather than waiting out the throttle -- the difference between a screen
    /// that says DONE the moment it is true and one that says it half a second
    /// late.
    FlashDumper::State mLastPublishedState = FlashDumper::State::Idle;

    void handleCommand(SDK::MessageBase* msg);
    void handleStart();

    /// Sets up mRegion, mWindow and mDumper. Separate from the constructor
    /// because it reads a file, and a constructor that touches storage is a
    /// constructor that can fail in ways it cannot report.
    void configure();
};

#endif // SERVICE_HPP
