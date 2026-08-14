#include <MapKit/PackSelection.hpp>

#include <cstring>

namespace MapKit
{

bool preferPack(const PackFacts& a, const PackFacts& b)
{
    if (a.zoomMax != b.zoomMax) {
        return a.zoomMax > b.zoomMax;          // 1. more detail
    }
    const int64_t areaA = a.areaUDeg2();
    const int64_t areaB = b.areaUDeg2();
    if (areaA != areaB) {
        return areaA < areaB;                  // 2. more local
    }
    // 3. deterministic. Both names come from a directory listing and are
    // never null; a defensive check here would only hide a caller bug.
    return std::strcmp(a.name, b.name) < 0;
}

size_t selectPack(const PackFacts* packs, size_t count,
                  int32_t latUdeg, int32_t lonUdeg)
{
    size_t best = kNoPack;
    for (size_t i = 0; i < count; ++i) {
        const PackFacts& p = packs[i];
        if (p.knownCorrupt || !p.contains(latUdeg, lonUdeg)) {
            continue;
        }
        if (best == kNoPack || preferPack(p, packs[best])) {
            best = i;
        }
    }
    return best;
}

} // namespace MapKit
