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

    SDK::Kernel &mKernel;
    ManagerLog   mLog;
    bool         mGuiStarted;

    std::vector<PackCrcVerifier> mEntries;
    size_t   mCurrentIndex  = 0;
    bool     mScannedOnce   = false;
    uint32_t mLastScanAtMs  = 0;
    uint32_t mLastPublishAtMs = 0;

    void handleCommand(SDK::MessageBase *msg);

    /// Lists kMapsDir, appending a new PackCrcVerifier for any *.rawtiles
    /// file not already tracked (matched by path). Safe to call repeatedly;
    /// already-tracked entries are left untouched.
    void scanForNewPacks();

    /// Advances whichever entry is current: starts it if not yet started,
    /// steps it if in progress, and moves on to the next not-yet-done entry
    /// once it finishes.
    void driveCurrentEntry();

    void publish();
};

#endif // SERVICE_HPP
