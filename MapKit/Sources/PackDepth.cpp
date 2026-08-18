#include "MapKit/PackDepth.hpp"

namespace MapKit
{
namespace
{

uint32_t readU32LE(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

bool deepestZoomWithTiles(const uint8_t* header, size_t length, uint8_t& out)
{
    if (header == nullptr || length < kPackHeaderBytes) {
        return false;
    }

    // Walk down, so the first hit is the answer. Zoom 0 is a legal level with
    // one tile in it, so the loop has to be able to reach it.
    for (size_t z = kPackZoomDirCount; z-- > 0;) {
        const uint8_t* entry = header + kPackZoomDirOffset + (z * 8);
        if (readU32LE(entry + 4) != 0) {
            out = static_cast<uint8_t>(z);
            return true;
        }
    }
    return false;
}

} // namespace MapKit
