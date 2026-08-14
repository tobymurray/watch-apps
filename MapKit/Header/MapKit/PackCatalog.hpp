/**
 ******************************************************************************
 * @file    PackCatalog.hpp
 * @brief   What packs are on this watch, cheaply enough to ask on the GUI
 *          thread.
 *
 * Enumerates `*.rawtiles` in the shared map directory and reads the first 292
 * bytes -- the rawtiles header -- of each. That is all the selection rule
 * needs (bbox, zoom_max) and it costs one open plus one short read per pack,
 * bounded regardless of pack size.
 *
 * This deliberately does NOT structurally open anything. A structural open
 * walks the whole tile index (one seek+read per entry -- thousands of them on
 * a real pack), and doing that for every candidate just to find out which one
 * to draw would put an unbounded multiple of that cost on the GUI thread. The
 * winner alone is opened, by MapSession, and even then with
 * `skipCrcVerify=true`: a mandatory whole-file CRC scan at 45 MB froze the GUI
 * for ~10 s and at 201 MB tripped the app-liveness watchdog and force-restarted
 * the watch. That scan belongs to Map Manager, in the background, from boot.
 *
 * The peek is a *screen*, not a validation. A pack that passes it can still
 * fail its structural open; that is fine and is reported. What the peek
 * rejects is what would make the peeked fields meaningless or the drawing
 * code wrong: bad magic, wrong format major, a tile_dim other than 256 (the
 * mosaic arithmetic is shifts by MapMath::TILE_SHIFT), a pixel format other
 * than ABGR2222 (what blitCopy is handed), a projection/addressing other than
 * WebMercator/Quadtree (what MapMath computes), an inverted bbox.
 ******************************************************************************
 */

#ifndef MAPKIT_PACKCATALOG_HPP
#define MAPKIT_PACKCATALOG_HPP

#include <MapKit/PackSelection.hpp>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include <cstdint>

namespace MapKit
{

/// Sandbox-relative path of the directory Map Manager verifies packs in.
///
/// Sandbox-relative on purpose: absolute, volume-prefixed paths (`N:/...`)
/// never resolve from inside an app on hardware -- only relative ones do, and
/// `../SharedData/` is how an app reaches the cross-app shared directory.
inline constexpr const char* kMapsDir = "../SharedData/maps";

inline constexpr const char* kPackExtension = ".rawtiles";

/**
 * @brief A fixed-size roster of candidate packs, with their names owned here.
 *
 * PackFacts::name points into this object's own storage, so a Catalog must
 * outlive any PackFacts taken from it. It has no heap allocation and no
 * growth: kMaxPacks is a hard cap, and packs beyond it are ignored rather
 * than making the GUI's memory use depend on what is in a directory.
 */
class PackCatalog
{
public:
    /// Map Manager was measured carrying 7 packs (160 MiB) on a real watch;
    /// 12 leaves headroom without letting a directory dictate RAM use.
    static constexpr size_t kMaxPacks    = 12;
    static constexpr size_t kMaxNameLen  = 64;
    /// rawtiles v1 header length (spec § 4). The smallest legal pack is this
    /// plus a 4-byte CRC footer, which is the size floor peek() enforces.
    static constexpr size_t kHeaderBytes = 292;
    static constexpr size_t kMinPackBytes = kHeaderBytes + 4;

    explicit PackCatalog(const SDK::Kernel& kernel) : mKernel(kernel) {}

    /**
     * @brief Re-enumerates the maps directory and re-peeks every header.
     *
     * Also re-reads each pack's trust marker to set `knownCorrupt`, which
     * needs the pack's own declared footer CRC -- so this costs a second short
     * read per pack (the trailing 4 bytes) plus a 16-byte marker read.
     *
     * @return the number of usable candidates found (0 is normal and not an
     *         error: no packs deployed yet).
     */
    size_t rescan();

    size_t count() const { return mCount; }
    const PackFacts* facts() const { return mFacts; }
    const PackFacts& at(size_t i) const { return mFacts[i]; }

    /// True once rescan() has run at least once, whatever it found. Lets a
    /// caller tell "no packs" from "haven't looked yet" -- which matters,
    /// because the scan is meant to happen exactly once per app launch.
    bool scanned() const { return mScanned; }

    /// Records a Bad verdict that arrived after the scan, so the selection
    /// rule stops offering this pack. Verdicts land asynchronously while an
    /// app is running; the roster does not get rebuilt for them.
    void markCorrupt(size_t i)
    {
        if (i < mCount) {
            mFacts[i].knownCorrupt = true;
        }
    }

    /// Fills @p out with "<kMapsDir>/<name>". @return false if it would not
    /// fit, in which case that pack cannot be used (a truncated path names a
    /// different file).
    static bool fullPathFor(const char* name, char* out, size_t outSize);

private:
    /// Reads and screens one pack header. @return false if the file cannot be
    /// used at all, in which case @p out is untouched.
    bool peek(const char* fullPath, PackFacts& out) const;

    /// Reads the pack's own trailing 4-byte declared CRC (rawtiles § 10).
    bool declaredCrc(const char* fullPath, uint64_t& sizeOut, uint32_t& crcOut) const;

    const SDK::Kernel& mKernel;
    PackFacts          mFacts[kMaxPacks] {};
    char               mNames[kMaxPacks][kMaxNameLen] {};
    size_t             mCount   = 0;
    bool               mScanned = false;
};

} // namespace MapKit

#endif // MAPKIT_PACKCATALOG_HPP
