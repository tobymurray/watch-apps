/**
 ******************************************************************************
 * @file    TraceBuffer.hpp
 * @brief   Bounded GPS breadcrumb storage for the live map face.
 *
 * Points are stored as world pixels at MapMath::TRACE_ZOOM -- a fixed lattice
 * that belongs to the app, not to whichever pack happens to be selected, so a
 * run that crosses from one pack to another keeps one continuous trace. They
 * are rescaled to the display zoom with a shift at draw time
 * (MapMath::rescale).
 *
 * Appends are distance-decimated: a fix is kept only when it moves at least
 * `mThresholdPx` from the last kept point. When the buffer fills, it thins
 * Garmin-style: every second point is dropped and the threshold doubles, so an
 * arbitrarily long activity always fits with progressively coarser (never
 * truncated) history.
 *
 * Pure code, no SDK dependencies: host-tested.
 ******************************************************************************
 */

#ifndef MAPKIT_TRACEBUFFER_HPP
#define MAPKIT_TRACEBUFFER_HPP

#include <MapKit/MapMath.hpp>

#include <cstdint>
#include <cstddef>

namespace MapKit
{

class TraceBuffer
{
public:
    static constexpr size_t CAPACITY = 1024;   // 8 KiB of points

    /// Decimation distance, in TRACE_ZOOM world pixels. 24 px at z18 is the
    /// same ground distance as the 6 px at z16 that this was tuned to on
    /// hardware -- roughly 14 m at the equator, less with latitude.
    static constexpr int32_t INITIAL_THRESHOLD_PX = 24;

    struct Point {
        int32_t x;
        int32_t y;
    };

    /// Appends a fix (world px at TRACE_ZOOM) if it is at least the current
    /// threshold away from the last kept point (Chebyshev distance -- cheaper
    /// than Euclidean and within sqrt(2) of it, which decimation doesn't care
    /// about). Returns true if the point was kept.
    bool append(int32_t x, int32_t y)
    {
        if (mCount > 0) {
            const Point& last = mPoints[mCount - 1];
            const int32_t dx  = absDelta(x, last.x);
            const int32_t dy  = absDelta(y, last.y);
            if (dx < mThresholdPx && dy < mThresholdPx) {
                return false;
            }
        }
        if (mCount == CAPACITY) {
            thin();
        }
        mPoints[mCount++] = Point { x, y };
        return true;
    }

    void clear()
    {
        mCount       = 0;
        mThresholdPx = INITIAL_THRESHOLD_PX;
    }

    size_t count() const { return mCount; }
    const Point& at(size_t i) const { return mPoints[i]; }
    int32_t thresholdPx() const { return mThresholdPx; }

private:
    static int32_t absDelta(int32_t a, int32_t b)
    {
        return a > b ? a - b : b - a;
    }

    /// Drop every second point (keeping the first and the most recent)
    /// and double the decimation threshold.
    void thin()
    {
        size_t kept = 0;
        for (size_t i = 0; i < mCount; i += 2) {
            mPoints[kept++] = mPoints[i];
        }
        // Always retain the newest point so the trace never visibly
        // retreats from the wearer's position.
        if ((mCount & 1) == 0 && mCount > 0) {
            mPoints[kept++] = mPoints[mCount - 1];
        }
        mCount = kept;
        mThresholdPx *= 2;
    }

    Point   mPoints[CAPACITY] = {};
    size_t  mCount            = 0;
    int32_t mThresholdPx      = INITIAL_THRESHOLD_PX;
};

} // namespace MapKit

#endif // MAPKIT_TRACEBUFFER_HPP
