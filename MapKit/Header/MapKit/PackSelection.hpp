/**
 ******************************************************************************
 * @file    PackSelection.hpp
 * @brief   Which of several verified packs should be drawn, and why.
 *
 * The proof of concept these apps grew out of hardcoded one pack path. A
 * generic app cannot: Map Manager discovers and verifies *every* `*.rawtiles`
 * file dropped into `../SharedData/maps/`, and a watch can carry several at
 * once -- a city at high zoom, a region at low zoom, somewhere last summer's
 * holiday was.
 *
 * ---------------------------------------------------------------------------
 * THE RULE
 * ---------------------------------------------------------------------------
 * Among the packs whose bounding box contains the current fix, and that are
 * not *known* corrupt, pick:
 *
 *   1. the largest `zoom_max`               -- the most detail available here;
 *   2. tie-break: the smallest bbox area    -- the more local pack of two that
 *                                              reach the same zoom is the one
 *                                              built for this place;
 *   3. tie-break: the lexicographically first filename -- so the choice is
 *                                              deterministic and reproducible
 *                                              in a bug report, never "whatever
 *                                              the directory listed first".
 *
 * If nothing contains the fix, there is no selection: the trace draws on black
 * and the status line says so. A pack that does not cover you is a blank
 * screen either way, so preferring a distant one over none would only be a
 * more confusing blank screen.
 *
 * ---------------------------------------------------------------------------
 * WHY TRUST IS NOT PART OF THE RANKING
 * ---------------------------------------------------------------------------
 * Deliberate, and the one part of this worth arguing with. Verification state
 * is not a property of a pack's *suitability*; it is a property of how far a
 * background scan has got. Ranking on it would mean the map silently swaps
 * from the coarse pack to the detailed one partway through an activity, at a
 * moment governed by disk throughput -- which reads as a glitch, and is worse
 * than a few seconds of an honest "verifying map".
 *
 * So trust gates *rendering*, not *choice*: the selected pack is drawn only
 * once its marker says Good for these exact bytes, and until then the status
 * line says which of the three things is happening.
 *
 * `Bad` is the one verdict that does participate, as an exclusion. It is
 * final -- a file confirmed corrupt does not become uncorrupt by being read
 * again -- so continuing to prefer a corrupt pack over a working one would be
 * choosing a permanently blank screen on purpose.
 *
 * ---------------------------------------------------------------------------
 * WHY NOT A CONFIGURED PACK NAME
 * ---------------------------------------------------------------------------
 * Kira's `[config]` block could write the wanted filename into the app's
 * folder, and that would be less code. It was rejected because it puts the
 * work on the wearer at exactly the wrong moment: the answer to "which pack"
 * changes when you travel, which is when you are least able to plug the watch
 * into a computer and edit a JSON file. Coverage is a question the pack header
 * already answers, for free, correctly, every time.
 *
 * Pure code, no SDK dependencies: host-tested
 * (MapKit/Tests/PackSelection_test.cpp).
 ******************************************************************************
 */

#ifndef MAPKIT_PACKSELECTION_HPP
#define MAPKIT_PACKSELECTION_HPP

#include <cstddef>
#include <cstdint>

namespace MapKit
{

/// Everything the selection rule needs about one candidate pack, in the units
/// the rawtiles header stores them in. Filled by PackCatalog from a 296-byte
/// header peek -- no structural open, no CRC, no tile index.
struct PackFacts {
    int32_t  bboxMinLonUDeg = 0;
    int32_t  bboxMinLatUDeg = 0;
    int32_t  bboxMaxLonUDeg = 0;
    int32_t  bboxMaxLatUDeg = 0;
    uint8_t  zoomMax        = 0;
    /// True when Map Manager's marker says Bad for these exact bytes. See the
    /// header comment: this excludes, everything else about trust does not.
    bool     knownCorrupt   = false;
    /// Filename only (no directory), used as the final tie-break and to build
    /// the full path. Never null.
    const char* name        = nullptr;

    bool contains(int32_t latUdeg, int32_t lonUdeg) const
    {
        return lonUdeg >= bboxMinLonUDeg && lonUdeg <= bboxMaxLonUDeg
            && latUdeg >= bboxMinLatUDeg && latUdeg <= bboxMaxLatUDeg;
    }

    /// Bbox area in squared microdegrees. int64 because a whole-world bbox is
    /// 360e6 x 180e6, which overflows int32 several times over.
    int64_t areaUDeg2() const
    {
        const int64_t w = static_cast<int64_t>(bboxMaxLonUDeg) - bboxMinLonUDeg;
        const int64_t h = static_cast<int64_t>(bboxMaxLatUDeg) - bboxMinLatUDeg;
        return w * h;
    }
};

/// Index returned by selectPack() when no candidate covers the fix.
constexpr size_t kNoPack = static_cast<size_t>(-1);

/// True if @p a should be preferred over @p b for a fix both of them cover.
/// Exposed for its own test; selectPack() is what callers want.
bool preferPack(const PackFacts& a, const PackFacts& b);

/// Applies the rule in this file's header comment.
/// @return an index into @p packs, or kNoPack.
size_t selectPack(const PackFacts* packs, size_t count,
                  int32_t latUdeg, int32_t lonUdeg);

} // namespace MapKit

#endif // MAPKIT_PACKSELECTION_HPP
