/**
 ******************************************************************************
 * @file    TileCache.hpp
 * @brief   Fixed-slot LRU cache of decoded map tiles.
 *
 * SLOTS x 64 KiB of storage, held in the cache object itself. The consuming
 * app must give that object *static* storage duration (see each app's
 * Model.cpp) rather than putting it in the TouchGFX FrontendHeap, so the
 * linker -- not the heap -- arbitrates the GUI RAM budget, and a cache that
 * does not fit fails the build instead of the device.
 *
 * SLOTS is 1, and that is a measurement, not a guess. Re-measured for these
 * apps against a pristine apps-v1.3.0, on RunMap (the largest of the three
 * GUIs, and therefore the one that sets the shared ceiling):
 *
 *   SLOTS = 4  ->  `.bss' will not fit in region `RAM', overflowed by 165,044 B
 *   SLOTS = 2  ->  `.bss' will not fit in region `RAM', overflowed by  33,884 B
 *   SLOTS = 1  ->  links, with room to spare
 *
 * The 4-slot figure reproduces the ~160 KB the proof of concept recorded on
 * the Running GUI, which is a good sign that nothing has drifted. Note that
 * the 2-slot margin is only ~34 KB: a second slot is not "nearly there".
 *
 * With one slot a full-viewport redraw re-reads up to the whole 2x2 mosaic at
 * ~6-9 ms per tile (measured on hardware): ~30 ms worst case at the 1 Hz
 * fix-driven cadence. More slots need RAM reclaimed from the inherited
 * activity GUI, not a bigger number here. BikeMap and HikeMap have smaller
 * GUIs and may have room for two, but this constant is shared by all three,
 * so RunMap is what it has to satisfy.
 *
 * Absent tiles (outside pack coverage) are resolved by findTile() alone
 * (index lookup, no I/O) and never occupy a slot.
 ******************************************************************************
 */

#ifndef MAPKIT_TILECACHE_HPP
#define MAPKIT_TILECACHE_HPP

#include <SDK/RawTiles/Container.hpp>

#include <cstdint>

namespace MapKit
{

class TileCache
{
public:
    static constexpr uint32_t SLOTS      = 1;
    static constexpr uint32_t TILE_BYTES = 256 * 256; // ABGR2222, 1 B/px

    /// Returns the decoded tile pixels for (z, x, y), reading through the
    /// cache, or nullptr when the tile is absent from the pack or the
    /// read fails. `container` must be open.
    const uint8_t* get(const SDK::RawTiles::Container& container,
                       uint8_t z, uint32_t x, uint32_t y)
    {
        ++mClock;
        for (Slot& s : mSlots) {
            if (s.used && s.z == z && s.x == x && s.y == y) {
                s.lastUse = mClock;
                return s.pixels;
            }
        }
        const SDK::RawTiles::TileInfo info = container.findTile(z, x, y);
        if (!info.valid()) {
            return nullptr;
        }
        Slot& victim = lru();
        if (container.readTile(info, victim.pixels, TILE_BYTES)
            != SDK::RawTiles::ReadResult::Ok) {
            victim.used = false; // slot content is now garbage
            return nullptr;
        }
        victim.used    = true;
        victim.z       = z;
        victim.x       = x;
        victim.y       = y;
        victim.lastUse = mClock;
        return victim.pixels;
    }

    /// Drop every cached tile. Must be called whenever the Container is
    /// re-pointed at a different pack: slots are keyed on (z, x, y) only, so
    /// a stale slot from the previous pack would be served as if it belonged
    /// to the new one.
    void clear()
    {
        for (Slot& s : mSlots) {
            s.used = false;
        }
    }

private:
    struct Slot {
        uint8_t  pixels[TILE_BYTES];
        uint32_t x       = 0;
        uint32_t y       = 0;
        uint32_t lastUse = 0;
        uint8_t  z       = 0;
        bool     used    = false;
    };

    Slot& lru()
    {
        Slot* best = &mSlots[0];
        for (Slot& s : mSlots) {
            if (!s.used) {
                return s;
            }
            if (s.lastUse < best->lastUse) {
                best = &s;
            }
        }
        return *best;
    }

    Slot     mSlots[SLOTS];
    uint32_t mClock = 0;
};

} // namespace MapKit

#endif // MAPKIT_TILECACHE_HPP
