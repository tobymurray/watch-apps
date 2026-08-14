/**
 ******************************************************************************
 * @file    TileRequestLog.hpp
 * @brief   Records where the wearer went with no map, so somebody can build
 *          one for next time.
 *
 * A watch that says `no map for here` knows something useful and, until now,
 * threw it away. This writes it down: one line per distinct area entered
 * without tile coverage, appended to a shared file in the same directory the
 * packs live in, where a desktop tool can pick it up over the same USB
 * connection used to deploy packs in the first place.
 *
 * It closes the loop the other way round from everything else in MapKit. Map
 * Manager tells apps which packs are trustworthy; this tells whoever makes the
 * packs which ones are missing.
 *
 * ---------------------------------------------------------------------------
 * WHAT IT RECORDS, AND AT WHAT RESOLUTION
 * ---------------------------------------------------------------------------
 * Not the GPS fix. A tile.
 *
 * Positions are quantised to a tile at `kRequestZoom` and only the *first*
 * visit to each tile is written. That is not a privacy fig leaf bolted on
 * afterwards -- it is what makes the output useful:
 *
 *   - The unit of a map pack is an area, not a point. "Build me something
 *     covering these tiles" is a request a generator can act on; a list of
 *     ten thousand fixes is one it would have to reduce first.
 *   - It bounds the writing. At 1 Hz an unquantised log would append tens of
 *     thousands of lines per activity, on the GUI thread, to internal flash.
 *     Quantised, a walk crosses a z12 tile boundary every half hour or so.
 *   - It bounds what is disclosed. This file lives in a directory every
 *     installed app can read and anyone who plugs the watch in can copy. A z12
 *     tile is several kilometres across, so it says "somewhere around here",
 *     which is all a pack generator needs and rather less than a track log.
 *
 * `kRequestZoom` is 12 because that is the coarsest zoom the packs measured
 * for this work carry, which makes one tile the natural "one region" unit.
 *
 * ---------------------------------------------------------------------------
 * WHEN IT RECORDS
 * ---------------------------------------------------------------------------
 * Only for the two states that mean *this place has no tiles*: no pack covers
 * the position at all, and a pack was selected but has no tile here at this
 * zoom.
 *
 * Deliberately not for a corrupt or unopenable pack. Those also leave the
 * screen blank, but the fix is to re-copy a pack that already exists, not to
 * build a new one -- filing them here would put work in the queue that nobody
 * should do.
 *
 * ---------------------------------------------------------------------------
 * FORMAT -- text, appended, versioned by its own header line
 * ---------------------------------------------------------------------------
 * @code
 *   # mapkit-requested-tiles v1
 *   # Places visited with no map coverage. One line per distinct tile, first
 *   # visit only. z/x/y are XYZ (slippy) tile coordinates; lat/lon is that
 *   # tile's centre in degrees. Duplicates across sessions are possible --
 *   # de-duplicate on z/x/y when consuming.
 *   12/1218/1517 44.5865 -75.9375 RunMap
 * @endcode
 *
 * Text rather than a binary record because the consumer is a desktop script
 * nobody has written yet, and the whole file is a few kilobytes. The trailing
 * tag names the app, which is worth having: a pack for somebody who was hiking
 * and a pack for somebody who was cycling are not the same pack.
 *
 * Best-effort throughout. Every failure is swallowed: this is an aid to
 * whoever makes packs, and no part of an activity may depend on it.
 ******************************************************************************
 */

#ifndef MAPKIT_TILEREQUESTLOG_HPP
#define MAPKIT_TILEREQUESTLOG_HPP

#include <MapKit/MapMath.hpp>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include <cstddef>
#include <cstdint>

namespace MapKit
{

class TileRequestLog
{
public:
    /// Zoom the request is quantised to. See the header comment: this is the
    /// coarsest zoom real packs carry, so one tile is one askable region.
    static constexpr uint8_t kRequestZoom = 12;

    /// Distinct tiles remembered within one run of the app. Bounds both the
    /// memory this holds and the number of appends one activity can make. An
    /// activity that crosses more than this many z12 tiles has covered a few
    /// hundred kilometres, at which point the first sixty-four are more than
    /// enough to describe where a map was wanted.
    static constexpr size_t kMaxTilesPerSession = 64;

    /// Stop appending once the file reaches this size, rather than rotating.
    /// A request log is not a diagnostic tail where the recent lines are the
    /// interesting ones -- an old request is exactly as valid as a new one, so
    /// dropping the oldest to make room would discard the very thing being
    /// collected. At roughly 40 bytes a line this holds several thousand
    /// distinct places, which nobody will reach without having processed it.
    static constexpr size_t kMaxBytes = 256 * 1024;

    /// @param appTag short name of the requesting app, written on each line.
    TileRequestLog(const SDK::Kernel& kernel, const char* appTag)
        : mKernel(kernel), mAppTag(appTag)
    {
    }

    TileRequestLog(const TileRequestLog&)            = delete;
    TileRequestLog& operator=(const TileRequestLog&) = delete;

    /**
     * @brief Note that (@p latUdeg, @p lonUdeg) had no map coverage.
     *
     * Quantises to a tile, does nothing if that tile has already been recorded
     * this session, and otherwise appends one line.
     *
     * @return true if a line was written -- for tests and for the caller's own
     *         logging; nothing behavioural depends on it.
     */
    bool note(int32_t latUdeg, int32_t lonUdeg);

    /// Distinct tiles recorded this session.
    size_t count() const { return mCount; }

private:
    struct Tile {
        uint32_t x;
        uint32_t y;
    };

    bool alreadySeen(uint32_t x, uint32_t y) const;
    bool append(uint32_t x, uint32_t y) const;

    const SDK::Kernel& mKernel;
    const char*        mAppTag;
    Tile               mSeen[kMaxTilesPerSession] {};
    size_t             mCount = 0;
};

} // namespace MapKit

#endif // MAPKIT_TILEREQUESTLOG_HPP
