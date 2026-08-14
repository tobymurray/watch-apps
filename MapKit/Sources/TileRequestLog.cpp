#include <MapKit/TileRequestLog.hpp>

#include <MapKit/PackCatalog.hpp>

#include <cstdio>
#include <cstring>
#include <memory>

namespace MapKit
{
namespace
{

/// Beside the packs, in the shared directory Map Manager watches. The
/// extension matters: Map Manager tracks `*.rawtiles` and would try to
/// CRC-verify anything that matched, so this deliberately does not.
constexpr const char* kRequestPath = "../SharedData/maps/requested-tiles.txt";

constexpr const char* kHeader =
    "# mapkit-requested-tiles v1\n"
    "# Places visited with no map coverage. One line per distinct tile, first\n"
    "# visit only. z/x/y are XYZ (slippy) tile coordinates; lat/lon is that\n"
    "# tile's centre in degrees. Duplicates across sessions are possible --\n"
    "# de-duplicate on z/x/y when consuming.\n";

} // namespace

bool TileRequestLog::alreadySeen(uint32_t x, uint32_t y) const
{
    for (size_t i = 0; i < mCount; ++i) {
        if (mSeen[i].x == x && mSeen[i].y == y) {
            return true;
        }
    }
    return false;
}

bool TileRequestLog::note(int32_t latUdeg, int32_t lonUdeg)
{
    const uint32_t tx = MapMath::tileCoord(MapMath::lonToWorldX(lonUdeg, kRequestZoom));
    const uint32_t ty = MapMath::tileCoord(MapMath::latToWorldY(latUdeg, kRequestZoom));

    if (alreadySeen(tx, ty)) {
        return false;   // the common case: still in the tile we already filed
    }
    if (mCount >= kMaxTilesPerSession) {
        return false;   // remembered enough places for one activity
    }

    // Record it as seen whether or not the write succeeds. A failed append is
    // not worth retrying once a second for the rest of the activity, and the
    // whole thing is best-effort.
    mSeen[mCount++] = Tile { tx, ty };
    return append(tx, ty);
}

bool TileRequestLog::append(uint32_t x, uint32_t y) const
{
    // The maps directory is normally already there -- something has to have
    // put packs in it -- but a watch that has never had one still deserves to
    // have its requests recorded, which is exactly the case where they matter
    // most.
    mKernel.fs.mkdir(kMapsDir);

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kRequestPath);
    if (!file) {
        return false;
    }
    // wMode=true, override=false: open for writing without truncating.
    if (!file->open(true, false)) {
        return false;
    }

    const size_t existing = file->size();
    if (existing >= kMaxBytes) {
        // Stop rather than rotate -- see the class comment. An old request is
        // as valid as a new one.
        file->close();
        return false;
    }

    // Whether a non-override write-mode open positions the cursor at EOF or at
    // 0 is not guaranteed by the interface, so seek explicitly (same idiom as
    // Map Manager's ManagerLog).
    file->seek(existing);

    size_t written = 0;
    if (existing == 0) {
        file->write(kHeader, std::strlen(kHeader), written);
    }

    // Tile centre, which is all the resolution this file carries by design.
    const int64_t centreX = static_cast<int64_t>(x) * MapMath::TILE_DIM + MapMath::TILE_DIM / 2;
    const int64_t centreY = static_cast<int64_t>(y) * MapMath::TILE_DIM + MapMath::TILE_DIM / 2;
    const int32_t lonU = MapMath::worldXToLonUDeg(centreX, kRequestZoom);
    const int32_t latU = MapMath::worldYToLatUDeg(centreY, kRequestZoom);

    char line[128];
    const int len = std::snprintf(line, sizeof(line), "%u/%lu/%lu %.4f %.4f %s\n",
                                  static_cast<unsigned>(kRequestZoom),
                                  static_cast<unsigned long>(x),
                                  static_cast<unsigned long>(y),
                                  static_cast<double>(latU) * MapMath::MICRODEG,
                                  static_cast<double>(lonU) * MapMath::MICRODEG,
                                  mAppTag != nullptr ? mAppTag : "?");
    bool ok = false;
    if (len > 0 && static_cast<size_t>(len) < sizeof(line)) {
        written = 0;
        ok = file->write(line, static_cast<size_t>(len), written) && written == static_cast<size_t>(len);
    }
    ok = file->flush() && ok;
    file->close();
    return ok;
}

} // namespace MapKit
