/**
 ******************************************************************************
 * @file    Service.hpp
 * @brief   MapManager service: discovers and background-verifies map packs.
 ******************************************************************************
 *
 * Runs from boot (APP_AUTOSTART) regardless of whether the GUI is ever
 * opened -- unlike a typical app's service, this one does NOT end itself
 * when the GUI closes; the whole point is that verification keeps making
 * progress in the background whether anyone is looking or not.
 *
 ******************************************************************************
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "SDK/Kernel/Kernel.hpp"

#include "Commands.hpp"
#include "ManagerLog.hpp"
#include "PackCrcVerifier.hpp"

class Service
{
public:
    Service(SDK::Kernel &kernel);

    virtual ~Service() = default;

    void run();

    /// One iteration of the background work run() does between message
    /// waits: rescan (throttled) and advance the current verifier by one
    /// slice. Public so host tests can drive the scan/verify orchestration
    /// directly -- run()'s own loop blocks on the kernel message queue and
    /// never returns, so it cannot be tested as a unit.
    void poll();

    /// Number of packs currently tracked, and how many of those are
    /// Verified. For tests and for publish().
    size_t trackedCount() const { return mEntries.size(); }
    uint16_t verifiedCount() const;

    /// Message-wait period run() would use for its next iteration: short
    /// while there is verification work pending, otherwise long enough to
    /// sleep until the next rescan is actually due (capped so an open GUI
    /// still refreshes). Pure query, exposed so the idle-power behaviour is
    /// testable rather than only inspectable.
    uint32_t nextWaitMs() const;

private:
    // Sandbox-relative (see Container::openFromFile's doc comment in the
    // AthensRun app this pattern originated in for why: absolute
    // volume-prefixed paths never resolve on hardware, only relative ones
    // do). ".." reaches SharedData -- a real, writable, cross-app directory
    // sitting alongside every app's own folder; AthensRun's own stride
    // calibrator already writes to "../SharedData/..." today.
    static constexpr const char* kMapsDir       = "../SharedData/maps";
    static constexpr const char* kPackExtension = ".rawtiles";

    // How often to rescan the directory for newly-deployed packs. Packs are
    // deployed over USB one at a time in practice, so this doesn't need to
    // be fast -- just frequent enough that dropping a new file in doesn't
    // require restarting the app.
    static constexpr uint32_t kRescanPeriodMs = 30000;

    // How often to publish a progress snapshot to the GUI while a scan is
    // in progress. Independent of PackCrcVerifier's own internal log
    // throttle -- this one governs kernel message-queue traffic, not the
    // diagnostic log.
    static constexpr uint32_t kPublishPeriodMs = 1000;

    // How many bytes one driveCurrentEntry() call hands to step().
    //
    // This, not the message-wait period, is what sets verification
    // throughput. The loop below is gated by a kernel message wait, so a
    // one-I/O-chunk-per-wait design makes the wait the bottleneck: at 4096
    // bytes per 50ms wait the ceiling is ~80KB/s no matter how fast the
    // storage is, which matched the ~77KB/s measured on-device exactly and
    // meant the loop was ~94% idle. A slice much larger than one chunk
    // amortises the wait away and lets the storage set the rate.
    //
    // The tradeoff is responsiveness: the service cannot service a message
    // while a slice is in flight, so this is a ceiling on message latency
    // too. 64KB is ~50ms of I/O at the rate the device actually sustains,
    // which is the same latency the old 50ms wait already imposed.
    static constexpr size_t kSliceBudgetBytes = 64 * 1024;

    // Message wait while a verification is pending. Short because there is
    // real work to return to, not to poll: each iteration does a full slice
    // of I/O, so this is not a spin.
    static constexpr uint32_t kBusyWaitMs = 10;

    SDK::Kernel &mKernel;
    ManagerLog   mLog;
    bool         mGuiStarted;

    /// One tracked pack: its verifier, plus the size the directory reported
    /// the last time this entry was armed. The size is what lets a finished
    /// verdict be reconsidered -- see scanForNewPacks().
    struct TrackedPack {
        TrackedPack(const SDK::Kernel &kernel, std::string path, uint64_t sizeAtScan)
            : verifier(kernel, std::move(path))
            , sizeAtLastScan(sizeAtScan)
        {
        }

        PackCrcVerifier verifier;
        uint64_t        sizeAtLastScan;
        bool            seenThisScan = false;
    };

    // Held by pointer, not by value. PackCrcVerifier holds kernel references,
    // so it is not move-assignable, which a vector of values would need in
    // order to erase a dropped pack from the middle. Indirection also pins
    // each entry: nothing an entry hands out is invalidated by the list
    // growing or shrinking around it. The list holds a handful of packs, so
    // the allocation is not worth avoiding.
    std::vector<std::unique_ptr<TrackedPack>> mEntries;
    size_t   mCurrentIndex  = 0;
    bool     mScannedOnce   = false;
    uint32_t mLastScanAtMs  = 0;
    uint32_t mLastPublishAtMs = 0;

    void handleCommand(SDK::MessageBase *msg);

    /// Lists kMapsDir and reconciles mEntries against what is actually there:
    ///   - a *.rawtiles file not already tracked becomes a new entry;
    ///   - a tracked entry whose on-disk size has changed since it was armed
    ///     is reset to Idle so it gets re-verified (this is what rescues a
    ///     pack that was discovered and written off mid-copy);
    ///   - a tracked entry whose file is gone is dropped, so it stops being
    ///     counted in the totals the GUI shows.
    /// Safe to call repeatedly; unchanged entries are left untouched.
    void scanForNewPacks();

    /// Advances whichever entry is current: starts it if not yet started,
    /// steps it by kSliceBudgetBytes if in progress, and moves on to the next
    /// not-yet-done entry once it finishes.
    void driveCurrentEntry();

    void publish();
};

#endif // SERVICE_HPP
