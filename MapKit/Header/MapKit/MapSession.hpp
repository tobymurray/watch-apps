/**
 ******************************************************************************
 * @file    MapSession.hpp
 * @brief   All live map state for one running app: which pack, how far its
 *          verification has got, where we are, and the breadcrumb so far.
 *
 * Owned by the app's Model, never by a screen. TouchGFX destroys and
 * re-creates screens on every transition, so anything a screen owned would be
 * thrown away each time the wearer scrolled to another face -- taking the open
 * pack handle and the whole trace with it. The map face borrows this; it does
 * not hold it.
 *
 * The tile cache is the exception: it is passed in by reference rather than
 * held, because it must have *static* storage duration in the app (see
 * TileCache.hpp) and MapSession itself lives inside the Model.
 *
 * ---------------------------------------------------------------------------
 * THE THREE THINGS THAT MUST NOT LOOK ALIKE
 * ---------------------------------------------------------------------------
 * "No pack for here", "pack not verified yet" and "pack is broken" are three
 * different situations with three different responses, and none of them is a
 * crash. In all of them the activity keeps recording and the trace keeps
 * drawing on the background; only the status line differs. Status is what
 * says which -- see MapStatus.
 *
 * ---------------------------------------------------------------------------
 * WHAT COSTS WHAT
 * ---------------------------------------------------------------------------
 * Every path here is called from the GUI thread at the 1 Hz GPS cadence, so:
 *
 *   - The directory scan and header peek run once, on the first fix (a fix is
 *     what makes the question answerable) and again only when the wearer
 *     leaves the selected pack's coverage. Packs cannot appear while the app
 *     runs -- connecting USB to copy one terminates every running app -- so
 *     rescanning on a timer would find nothing, forever.
 *   - The structural open runs once per selected pack, with
 *     skipCrcVerify=true. Never with the CRC scan: at 45 MB it froze the GUI
 *     for ~10 s, and at 201 MB it tripped the app-liveness watchdog and
 *     force-restarted the watch.
 *   - The trust poll is one 16-byte file read, and it stops the moment the
 *     verdict resolves either way. It has to keep running until then, because
 *     Map Manager's background pass finishes on its own schedule and
 *     "verifying" must be able to become "live" without the wearer doing
 *     anything.
 ******************************************************************************
 */

#ifndef MAPKIT_MAPSESSION_HPP
#define MAPKIT_MAPSESSION_HPP

#include <MapKit/MapMath.hpp>
#include <MapKit/PackCatalog.hpp>
#include <MapKit/PackSelection.hpp>
#include <MapKit/TileCache.hpp>
#include <MapKit/TraceBuffer.hpp>
#include <SDK/RawTiles/Container.hpp>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include <cstdint>

namespace MapKit
{

/// What the map face should tell the wearer. Ordered roughly by how early in
/// the sequence each one occurs; nothing depends on the ordering.
enum class MapStatus : uint8_t {
    NoFix,        ///< No GPS fix yet, so "which pack covers me" has no answer.
    NoPack,       ///< Looked; nothing on this watch covers this position.
    PackError,    ///< The chosen pack failed its structural open. Final.
    Corrupt,      ///< Map Manager confirmed these exact bytes corrupt. Final.
    Verifying,    ///< Pack is fine structurally; no verdict yet. Resolves itself.
    OffCoverage,  ///< Pack is live, but there is no tile at this spot/zoom.
    Live,         ///< Drawing.
};

class MapSession
{
public:
    MapSession(const SDK::Kernel& kernel, TileCache& cache)
        : mKernel(kernel), mCatalog(kernel), mCache(cache)
    {
    }

    MapSession(const MapSession&)            = delete;
    MapSession& operator=(const MapSession&) = delete;

    // -- fed by the app ----------------------------------------------------

    /**
     * @brief One GPS sample. Call on every GPS_POSITION message.
     * @param latitude, longitude: decimal degrees, as the GPS parser gives them.
     * @param fix: whether those coordinates mean anything.
     * @param recording: append to the breadcrumb (i.e. the activity is
     *        actively running -- a paused activity must not draw a straight
     *        line across the gap).
     */
    void onPosition(float latitude, float longitude, bool fix, bool recording);

    /// New activity: drop the breadcrumb. Pack selection is unaffected.
    void resetTrace() { mTrace.clear(); }

    /// Step the display zoom through the selected pack's own zoom range,
    /// wrapping. No-op with no pack (there is no range to step through).
    void cycleZoom();

    // -- read by the map face ----------------------------------------------

    MapStatus status() const;

    /// True when there is a pack open, CRC-trusted, and safe to draw from.
    /// The map face must not draw tiles otherwise: Container::isOpen() is
    /// true from the structural open onward, i.e. before the CRC is confirmed.
    bool renderable() const { return mOpenResult == SDK::RawTiles::OpenResult::Ok && mTrusted; }

    const SDK::RawTiles::Container& container() const { return mContainer; }
    TileCache&                      cache() const { return mCache; }
    const TraceBuffer&              trace() const { return mTrace; }

    int64_t centerX() const { return mCenterX; }   ///< world px at TRACE_ZOOM
    int64_t centerY() const { return mCenterY; }   ///< world px at TRACE_ZOOM
    uint8_t zoom() const { return mZoom; }
    bool    fix() const { return mFix; }

    /// Human-readable detail for MapStatus::PackError, else nullptr.
    const char* packErrorText() const;

    /// Filename of the selected pack, or nullptr if none is selected.
    const char* packName() const;

private:
    /// Runs the selection rule against the catalog already in memory and opens
    /// the winner. Pure choice plus at most one open -- deliberately no
    /// directory scan, see onPosition().
    void chooseAndOpen();
    void pollTrust();
    void closePack();
    bool coversCurrentFix() const;

    const SDK::Kernel& mKernel;
    PackCatalog        mCatalog;
    TileCache&         mCache;
    TraceBuffer        mTrace;

    SDK::RawTiles::Container mContainer;

    // Selection
    size_t mSelected = kNoPack;
    char   mPackPath[SDK::Interface::IFileSystem::skMaxPathLen] {};

    // Open state. FileNotFound is the "nothing selected" value, which is also
    // what an absent pack would report -- both mean "no container".
    SDK::RawTiles::OpenResult mOpenResult = SDK::RawTiles::OpenResult::FileNotFound;

    // Trust. Both sticky: a completed verdict about a fixed set of bytes does
    // not change, so once either is set the poll stops.
    bool mTrusted = false;
    bool mCorrupt = false;

    // Position, kept at TRACE_ZOOM so it shares the trace's lattice.
    int32_t mLatUDeg = 0;
    int32_t mLonUDeg = 0;
    int64_t mCenterX = 0;
    int64_t mCenterY = 0;
    bool    mFix     = false;
    bool    mHadFix  = false;   ///< true once any fix has ever arrived

    /// Log-once latch for "nothing covers here", which is a normal state that
    /// persists for as long as the wearer stays outside every pack.
    bool    mReportedNoPack = false;

    uint8_t mZoom = 0;          ///< 0 until a pack is open and sets its range
};

} // namespace MapKit

#endif // MAPKIT_MAPSESSION_HPP
