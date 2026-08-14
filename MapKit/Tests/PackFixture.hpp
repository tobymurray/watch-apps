/**
 * @file PackFixture.hpp
 * @brief Builds the smallest legal rawtiles v1 pack, and Map Manager trust
 *        markers to sit beside it, for the host tests.
 *
 * The pack is a 292-byte header (Quadtree / WebMercator / ABGR2222, zero
 * tiles, zero extensions) plus a 4-byte CRC-32 footer. With tile_count == 0
 * the tile-index walk and the zoom_offsets checks degenerate to "everything
 * must be zero", and extensions_offset must equal tile_blob_start (which is
 * index_offset, 292). That is enough for a structural open to succeed, which
 * is all these tests need -- tile *serving* is the vendored reader's own
 * business and is covered by its upstream conformance tests.
 *
 * The CRC is computed here rather than imported from Container.cpp, and
 * cross-checked against the spec's pinned vector, so a test failure cannot be
 * a shared bug in one implementation agreeing with itself.
 */
#ifndef MAPKIT_TESTS_PACKFIXTURE_HPP
#define MAPKIT_TESTS_PACKFIXTURE_HPP

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace MapKitTest {

constexpr size_t kHeaderSize = 292;
constexpr size_t kFooterSize = 4;
constexpr size_t kPackSize   = kHeaderSize + kFooterSize;

inline void writeU16LE(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

inline void writeU32LE(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

inline void writeI32LE(uint8_t* p, int32_t v) { writeU32LE(p, static_cast<uint32_t>(v)); }

inline void writeU64LE(uint8_t* p, uint64_t v)
{
    writeU32LE(p, static_cast<uint32_t>(v));
    writeU32LE(p + 4, static_cast<uint32_t>(v >> 32));
}

/// CRC-32/ISO-HDLC. Pinned by PackFixture.Crc32MatchesSpecVector.
inline uint32_t crc32(const uint8_t* data, size_t length)
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

struct PackSpec {
    uint8_t zoomMin = 0;
    uint8_t zoomMax = 16;
    int32_t minLon  = -76100000;
    int32_t minLat  =  44500000;
    int32_t maxLon  = -75900000;
    int32_t maxLat  =  44700000;
    uint16_t tileDim     = 256;
    uint8_t  pixelFormat = 1;   ///< ABGR2222
    uint8_t  projection  = 1;   ///< WebMercator
    uint8_t  addressing  = 1;   ///< Quadtree
    uint8_t  formatMajor = 1;
    bool     goodMagic   = true;
};

/// A structurally valid, CRC-correct pack, as a byte string ready to seed into
/// the in-memory filesystem.
inline std::string buildPack(const PackSpec& spec = PackSpec{})
{
    std::string bytes(kPackSize, '\0');
    uint8_t* h = reinterpret_cast<uint8_t*>(&bytes[0]);

    if (spec.goodMagic) {
        h[0] = 'R'; h[1] = 'A'; h[2] = 'W'; h[3] = 'T';
    } else {
        h[0] = 'N'; h[1] = 'O'; h[2] = 'P'; h[3] = 'E';
    }
    h[4] = spec.formatMajor;
    h[5] = 0;                              // format_minor
    for (int i = 0; i < 16; ++i) {
        h[8 + i] = static_cast<uint8_t>(0xA0 + i);   // pack_uuid: must be non-zero
    }
    // supersedes_uuid (24..39) and parent_uuid (40..55) stay zero: legal.
    h[56] = spec.pixelFormat;
    h[57] = spec.projection;
    h[58] = spec.addressing;
    h[59] = 1;                             // axis = XYZ
    writeU16LE(h + 60, spec.tileDim);
    h[62] = spec.zoomMin;
    h[63] = spec.zoomMax;
    writeI32LE(h + 64, spec.minLon);
    writeI32LE(h + 68, spec.minLat);
    writeI32LE(h + 72, spec.maxLon);
    writeI32LE(h + 76, spec.maxLat);
    writeU64LE(h + 80, 0);                 // build_timestamp: 0 = no freshness info
    writeU32LE(h + 88, 0);                 // tile_count
    writeU32LE(h + 92, 292);               // index_offset, fixed in v1
    // zoom_offsets[24] (96..287) stay zero, required when every count is 0.
    writeU32LE(h + 288, 292);              // extensions_offset == tile_blob_start

    writeU32LE(h + kHeaderSize, crc32(h, kHeaderSize));
    return bytes;
}

/// The CRC a pack declares in its own footer -- what a trust marker has to
/// agree with for its verdict to apply.
inline uint32_t declaredCrcOf(const std::string& pack)
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(pack.data());
    const uint8_t* f = p + pack.size() - 4;
    return static_cast<uint32_t>(f[0]) | (static_cast<uint32_t>(f[1]) << 8)
         | (static_cast<uint32_t>(f[2]) << 16) | (static_cast<uint32_t>(f[3]) << 24);
}

/// A 16-byte marker in Map Manager's format. `magic` is 'MPT1' (good) or
/// 'MPTX' (bad); see MapKit/PackTrustReader.hpp.
inline std::string buildMarker(uint32_t magic, uint64_t fileSize, uint32_t crc)
{
    std::string bytes(16, '\0');
    uint8_t* p = reinterpret_cast<uint8_t*>(&bytes[0]);
    writeU32LE(p + 0, magic);
    writeU64LE(p + 4, fileSize);
    writeU32LE(p + 12, crc);
    return bytes;
}

} // namespace MapKitTest

#endif // MAPKIT_TESTS_PACKFIXTURE_HPP
