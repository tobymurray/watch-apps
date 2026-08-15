/*
 * VENDORED, byte-for-byte apart from this notice, from una-sdk branch
 * feat/rawtiles-container @ b957aa62. See the matching notice on
 * SDK/RawTiles/Container.hpp for why, and for what should replace it.
 */
/**
 ******************************************************************************
 * @file    Container.cpp
 * @brief   Implementation of SDK::RawTiles::Container.
 *
 * Spec references in comments use the section numbers from rawtiles v0.6
 * (wire format (1, 0)).
 ******************************************************************************
 */

#include "SDK/RawTiles/Container.hpp"

#include <array>
#include <cmath>
#include <cstring>

namespace SDK
{
namespace RawTiles
{
namespace
{

constexpr std::size_t kHeaderSize     = 292;
constexpr std::size_t kFooterSize     = 4;
constexpr std::size_t kMinFileSize    = kHeaderSize + kFooterSize; // § 11 #1
constexpr std::size_t kIndexEntrySize = 20;                        // § 5.1
constexpr std::size_t kZoomDirCount   = 24;                        // § 4.12
constexpr uint64_t    kMaxFileSize    = 0xFFFFFFFFu;                // § 11 #30
constexpr std::size_t kChunkSize      = 4096; ///< Bounded scratch for streamed reads.
// Local-only bump from the vendored 128B: verifyCrc() was doing ~352,000
// 128B reads over the app<->kernel filesystem IPC to scan a 45MB pack,
// which froze the app for ~10s on first GPS_POSITION (see ensureMapPack()
// in Model.cpp). Not yet ported upstream to feat/rawtiles-container --
// this file stays out of sync with that branch until someone does.

inline uint32_t align4(uint32_t n)
{
    return (n + 3u) & ~uint32_t { 3u };
}

inline uint16_t readU16LE(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t readU32LE(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t readI32LE(const uint8_t *p)
{
    return static_cast<int32_t>(readU32LE(p));
}

inline uint64_t readU64LE(const uint8_t *p)
{
    uint64_t lo = readU32LE(p);
    uint64_t hi = readU32LE(p + 4);
    return lo | (hi << 32);
}

bool uuidIsZero(const uint8_t *u)
{
    for (int i = 0; i < 16; ++i) {
        if (u[i] != 0) {
            return false;
        }
    }
    return true;
}

/// CRC-32/ISO-HDLC table (the PNG/zlib variant; spec § 10). A function-local
/// static initializer is a "magic static": the C++11 standard guarantees
/// thread-safe one-time initialization, unlike the hand-rolled
/// `static bool tableReady` guard this replaces, which was a data race under
/// concurrent first-use.
const uint32_t* crc32Table()
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t {};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    return table.data();
}

/// Incremental CRC-32/ISO-HDLC so the caller can fold in bytes a chunk at a
/// time (the file backend never has the whole pack resident to CRC in one
/// call). @p crc is the running state; pass 0xFFFFFFFF for the first chunk
/// and XOR the final result with 0xFFFFFFFF once all chunks are folded in.
uint32_t crc32Update(uint32_t crc, const uint8_t *data, std::size_t length)
{
    const uint32_t *table = crc32Table();
    for (std::size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

/// True if the (projection, addressing) byte pair is one of the two legal v1
/// combinations per § 8.6.
bool isLegalEnumPair(Projection proj, Addressing addr)
{
    return (proj == Projection::WebMercator && addr == Addressing::Quadtree)
        || (proj == Projection::LocalLinear && addr == Addressing::SingleImage);
}

/**
 * @brief One byte of streaming RFC 3629 UTF-8 validation, O(1) state.
 *
 * Table-free by design: each lead byte's continuation-count and the *first*
 * continuation byte's legal range are looked up directly from the RFC 3629
 * table (this is what rules out overlong encodings — e.g. 0xE0 only allows
 * codepoints ≥ 0x800, so its first continuation byte is restricted to
 * [0xA0, 0xBF] rather than the general [0x80, 0xBF] — and what rules out
 * UTF-16 surrogate halves via 0xED's restricted [0x80, 0x9F]).
 */
struct Utf8State {
    uint8_t  remaining = 0;    ///< Continuation bytes still expected.
    uint8_t  minCont    = 0x80;
    uint8_t  maxCont    = 0xBF;
    uint32_t codepoint  = 0;
};

/// @return false on any byte that cannot legally appear here (malformed
///         UTF-8). @p completed is set true when @p b finished a codepoint
///         (its value is then in @c state.codepoint).
bool feedUtf8Byte(Utf8State &state, uint8_t b, bool &completed)
{
    completed = false;
    if (state.remaining == 0) {
        if (b < 0x80) {
            state.codepoint = b;
            completed = true;
            return true;
        }
        if (b >= 0xC2 && b <= 0xDF) {
            state.remaining = 1; state.minCont = 0x80; state.maxCont = 0xBF;
            state.codepoint = b & 0x1Fu;
            return true;
        }
        if (b == 0xE0) {
            state.remaining = 2; state.minCont = 0xA0; state.maxCont = 0xBF; // no overlong
            state.codepoint = b & 0x0Fu;
            return true;
        }
        if (b >= 0xE1 && b <= 0xEC) {
            state.remaining = 2; state.minCont = 0x80; state.maxCont = 0xBF;
            state.codepoint = b & 0x0Fu;
            return true;
        }
        if (b == 0xED) {
            state.remaining = 2; state.minCont = 0x80; state.maxCont = 0x9F; // no surrogates
            state.codepoint = b & 0x0Fu;
            return true;
        }
        if (b >= 0xEE && b <= 0xEF) {
            state.remaining = 2; state.minCont = 0x80; state.maxCont = 0xBF;
            state.codepoint = b & 0x0Fu;
            return true;
        }
        if (b == 0xF0) {
            state.remaining = 3; state.minCont = 0x90; state.maxCont = 0xBF; // no overlong
            state.codepoint = b & 0x07u;
            return true;
        }
        if (b >= 0xF1 && b <= 0xF3) {
            state.remaining = 3; state.minCont = 0x80; state.maxCont = 0xBF;
            state.codepoint = b & 0x07u;
            return true;
        }
        if (b == 0xF4) {
            state.remaining = 3; state.minCont = 0x80; state.maxCont = 0x8F; // cap at U+10FFFF
            state.codepoint = b & 0x07u;
            return true;
        }
        return false; // 0x80-0xC1 (stray continuation / overlong lead), 0xF5-0xFF
    }
    if (b < state.minCont || b > state.maxCont) {
        return false;
    }
    state.codepoint = (state.codepoint << 6) | (b & 0x3Fu);
    state.minCont = 0x80;
    state.maxCont = 0xBF; // only the first continuation byte has a restricted range
    if (--state.remaining == 0) {
        completed = true;
    }
    return true;
}

} // namespace

bool Container::readAt(uint64_t offset, void *dst, size_t len) const
{
    if (mBackend == Backend::Memory) {
        if (offset > mMemSize || len > mMemSize - offset) {
            return false;
        }
        std::memcpy(dst, mMemData + offset, len);
        return true;
    }
    if (mBackend == Backend::File) {
        if (offset > mFileSize || len > mFileSize - offset) {
            return false;
        }
        if (!mFile->seek(static_cast<size_t>(offset))) {
            return false;
        }
        size_t got = 0;
        if (!mFile->read(static_cast<char *>(dst), len, got)) {
            return false;
        }
        return got == len; // short read (e.g. concurrent truncation) is a failure here
    }
    return false;
}

OpenResult Container::openFromMemory(const uint8_t *data, std::size_t size,
                                      bool skipCrcVerify)
{
    close();
    if (static_cast<uint64_t>(size) > kMaxFileSize) {
        return OpenResult::FileTooLarge;
    }
    mMemData = data;
    mMemSize = size;
    mBackend = Backend::Memory;
    OpenResult r = parseAndValidate(skipCrcVerify);
    if (r != OpenResult::Ok) {
        close();
    }
    return r;
}

OpenResult Container::openFromFile(SDK::Interface::IFileSystem &fs, const char *path,
                                    bool skipCrcVerify)
{
    close();
    mFile = fs.file(path);
    if (!mFile) {
        return OpenResult::FileNotFound;
    }
    if (!mFile->open(false, false)) {
        mFile.reset();
        return OpenResult::FileNotFound;
    }
    mFileSize = mFile->size();
    if (mFileSize > kMaxFileSize) {
        close();
        return OpenResult::FileTooLarge;
    }
    mBackend = Backend::File;
    OpenResult r = parseAndValidate(skipCrcVerify);
    if (r != OpenResult::Ok) {
        close();
    }
    return r;
}

void Container::close()
{
    if (mFile) {
        mFile->close();
        mFile.reset();
    }
    mMemData    = nullptr;
    mMemSize    = 0;
    mFileSize   = 0;
    mBackend    = Backend::None;
    mHeader     = Header { };
    mAttrOffset = 0;
    mAttrLength = 0;
}

OpenResult Container::parseAndValidate(bool skipCrcVerify)
{
    const uint64_t size = backendSize();

    // § 11 #1: minimum size.
    if (size < kMinFileSize) {
        return OpenResult::FileTooShort;
    }

    uint8_t hdr[kHeaderSize];
    if (!readAt(0, hdr, kHeaderSize)) {
        return OpenResult::IoError;
    }

    // § 11 #2: magic.
    if (!(hdr[0] == 'R' && hdr[1] == 'A' && hdr[2] == 'W' && hdr[3] == 'T')) {
        return OpenResult::BadMagic;
    }

    // § 11 #3 / #4: version.
    mHeader.formatMajor = hdr[4];
    mHeader.formatMinor = hdr[5];
    if (mHeader.formatMajor != 1) {
        return OpenResult::BadVersion;
    }
    // Bytes 6-7 are reserved_v1_0; spec § 4 says readers MUST accept any value.

    // § 11 #5: pack_uuid != 0.
    std::memcpy(mHeader.packUuid, hdr + 8, 16);
    if (uuidIsZero(mHeader.packUuid)) {
        return OpenResult::BadUuid;
    }
    std::memcpy(mHeader.supersedesUuid, hdr + 24, 16);
    // § 11 #6: parent_uuid (bytes 40..55) MUST be all-zero.
    for (int i = 40; i < 56; ++i) {
        if (hdr[i] != 0) {
            return OpenResult::BadUuid;
        }
    }

    // § 11 #7: enum bytes. v0.6 legalises pixel_format 2 (RGB565) and
    // compression 1 (RLE, per-entry, checked in the tile-index walk below);
    // everything else stays reserved-reject.
    const uint8_t pixByte  = hdr[56];
    const uint8_t projByte = hdr[57];
    const uint8_t addrByte = hdr[58];
    const uint8_t axisByte = hdr[59];
    if (pixByte != 1 && pixByte != 2) {
        return OpenResult::BadEnum;
    }
    if (projByte != 1 && projByte != 3) {
        return OpenResult::BadEnum;
    }
    if (addrByte != 1 && addrByte != 2) {
        return OpenResult::BadEnum;
    }
    if (axisByte != 1 && axisByte != 2) {
        return OpenResult::BadEnum;
    }
    mHeader.pixelFormat = static_cast<PixelFormat>(pixByte);
    mHeader.projection  = static_cast<Projection>(projByte);
    mHeader.addressing  = static_cast<Addressing>(addrByte);
    mHeader.axis        = static_cast<Axis>(axisByte);

    // § 11 #8: legal projection × addressing pair.
    if (!isLegalEnumPair(mHeader.projection, mHeader.addressing)) {
        return OpenResult::BadEnumPair;
    }

    // § 11 #9: tile_dim_px > 0.
    mHeader.tileDimPx = readU16LE(hdr + 60);
    if (mHeader.tileDimPx == 0) {
        return OpenResult::BadDimensions;
    }

    // § 11 #10: zoom range.
    mHeader.zoomMin = hdr[62];
    mHeader.zoomMax = hdr[63];
    if (mHeader.zoomMax >= kZoomDirCount || mHeader.zoomMin > mHeader.zoomMax) {
        return OpenResult::BadZoomRange;
    }

    // § 11 #11: bbox ranges.
    mHeader.bboxMinLonUDeg = readI32LE(hdr + 64);
    mHeader.bboxMinLatUDeg = readI32LE(hdr + 68);
    mHeader.bboxMaxLonUDeg = readI32LE(hdr + 72);
    mHeader.bboxMaxLatUDeg = readI32LE(hdr + 76);
    if (mHeader.bboxMinLonUDeg < -180000000 || mHeader.bboxMinLonUDeg > 180000000) {
        return OpenResult::BadBbox;
    }
    if (mHeader.bboxMaxLonUDeg < -180000000 || mHeader.bboxMaxLonUDeg > 180000000) {
        return OpenResult::BadBbox;
    }
    if (mHeader.bboxMinLatUDeg < -90000000 || mHeader.bboxMinLatUDeg > 90000000) {
        return OpenResult::BadBbox;
    }
    if (mHeader.bboxMaxLatUDeg < -90000000 || mHeader.bboxMaxLatUDeg > 90000000) {
        return OpenResult::BadBbox;
    }
    if (mHeader.bboxMinLonUDeg > mHeader.bboxMaxLonUDeg
            || mHeader.bboxMinLatUDeg > mHeader.bboxMaxLatUDeg) {
        return OpenResult::BadBbox;
    }

    mHeader.buildTimestamp = readU64LE(hdr + 80);
    mHeader.tileCount      = readU32LE(hdr + 88);
    mHeader.indexOffset    = readU32LE(hdr + 92);

    // § 11 #25: v1 fixes index_offset at 292.
    if (mHeader.indexOffset != kHeaderSize) {
        return OpenResult::BadIndexOffset;
    }

    // § 11.5 allocation ordering: bound tile_count against file size with
    // the division form (the multiplicative form wraps u32 near tile_count
    // ≈ u32::MAX / 20) before trusting it for anything below.
    const uint64_t indexCapacity = (size - kMinFileSize) / kIndexEntrySize;
    if (mHeader.tileCount > indexCapacity) {
        return OpenResult::BadIndexBounds;
    }

    // zoom_offsets[24]: 192 bytes starting at offset 96.
    for (std::size_t z = 0; z < kZoomDirCount; ++z) {
        const uint8_t *e = hdr + 96 + (z * 8);
        mHeader.zoomOffsets[z].offset = readU32LE(e);
        mHeader.zoomOffsets[z].count  = readU32LE(e + 4);
    }
    mHeader.extensionsOffset = readU32LE(hdr + 288);

    // tile_blob_start = align4(index_offset + 20 * tile_count). With
    // index_offset = 292 (4-aligned) and 20-byte entries, this is already
    // 4-aligned for any tile_count.
    const uint64_t indexBytes64    = static_cast<uint64_t>(mHeader.tileCount) * kIndexEntrySize;
    const uint64_t tileBlobStart64 = static_cast<uint64_t>(mHeader.indexOffset) + indexBytes64;
    if (tileBlobStart64 > size) {
        return OpenResult::BadIndexBounds;
    }
    const uint32_t tileBlobStart = static_cast<uint32_t>(tileBlobStart64);

    // § 11 #18 header-resident prerequisites: extensions_offset alignment
    // and ordering (the padded-sum equality is checked after the walk).
    if ((mHeader.extensionsOffset & 3u) != 0) {
        return OpenResult::BadExtensionsOffset;
    }
    if (mHeader.extensionsOffset > size - kFooterSize) {
        return OpenResult::BadExtensionsOffset;
    }
    if (mHeader.extensionsOffset < tileBlobStart) {
        return OpenResult::BadExtensionsOffset;
    }

    // Walk the tile index one 20-byte entry at a time (bounded scratch,
    // no whole-index residency). § 11 #12, #13, #14, #15, #16, #17, #31, #32.
    uint32_t walkedPerZoom[kZoomDirCount]      = { };
    uint32_t firstOffsetPerZoom[kZoomDirCount] = { };
    bool     zoomSeen[kZoomDirCount]           = { };

    uint64_t expectedTileOffset = tileBlobStart;
    uint8_t  prevZ    = 0;
    uint32_t prevX    = 0;
    uint32_t prevY    = 0;
    bool     havePrev = false;

    for (uint32_t i = 0; i < mHeader.tileCount; ++i) {
        uint8_t e[kIndexEntrySize];
        if (!readAt(mHeader.indexOffset + static_cast<uint64_t>(i) * kIndexEntrySize, e, kIndexEntrySize)) {
            return OpenResult::IoError;
        }
        const uint8_t  z        = e[0];
        const uint8_t  comp     = e[1];
        const uint8_t  flags    = e[2];
        const uint8_t  reserved = e[3];
        const uint32_t x        = readU32LE(e + 4);
        const uint32_t y        = readU32LE(e + 8);
        const uint32_t offset   = readU32LE(e + 12);
        const uint32_t length   = readU32LE(e + 16);

        // § 11 #7: per-entry compression byte. v0.6 legalises RLE (1); the
        // decoder isn't implemented yet (class doc), so structurally-valid
        // RLE entries pass here and fail later, at readTile()/readTileRows().
        if (comp != static_cast<uint8_t>(Compression::None)
                && comp != static_cast<uint8_t>(Compression::RLE)) {
            return OpenResult::BadEnum;
        }
        // § 11 #12: flags/reserved.
        if (flags != 0 || reserved != 0) {
            return OpenResult::BadTileEntry;
        }
        // § 11 #15: z within declared range.
        if (z < mHeader.zoomMin || z > mHeader.zoomMax) {
            return OpenResult::BadTileZoom;
        }
        if (z >= kZoomDirCount) {
            return OpenResult::BadTileZoom;
        }
        // § 11 #31: (x, y) bounded by 2^z for Quadtree.
        if (mHeader.addressing == Addressing::Quadtree) {
            const uint64_t maxCoord = (z < 32) ? (uint64_t { 1 } << z) : 0;
            if (x >= maxCoord || y >= maxCoord) {
                return OpenResult::BadTileEntry;
            }
        }
        // § 11 #13: ascending (z, x, y).
        if (havePrev) {
            if (z < prevZ) {
                return OpenResult::BadTileOrder;
            }
            if (z == prevZ) {
                if (x < prevX || (x == prevX && y <= prevY)) {
                    return OpenResult::BadTileOrder;
                }
            }
        }
        // § 11 #14: offset alignment + bounds + length bound.
        if ((offset & 3u) != 0) {
            return OpenResult::BadTileEntry;
        }
        if (offset < tileBlobStart) {
            return OpenResult::BadTileEntry;
        }
        if (offset >= mHeader.extensionsOffset) {
            return OpenResult::BadTileEntry;
        }
        // u64-safe form, equivalent to (c)+(d) in spec.
        if (static_cast<uint64_t>(offset) + length > mHeader.extensionsOffset) {
            return OpenResult::BadTileEntry;
        }
        // § 11 #32: offset must equal the tight tile-blob layout.
        if (offset != expectedTileOffset) {
            return OpenResult::BadTileEntry;
        }
        // § 11 #16: compression = None demands length == format-implied
        // size; compression != None (RLE) has no length constraint here —
        // the encoded length is variable, bounded only by #14 above.
        if (comp == static_cast<uint8_t>(Compression::None)) {
            const uint64_t expectedLen = static_cast<uint64_t>(mHeader.tileDimPx)
                                       * static_cast<uint64_t>(mHeader.tileDimPx)
                                       * bytesPerPixel(mHeader.pixelFormat);
            if (length != expectedLen) {
                return OpenResult::BadTileEntry;
            }
        }

        // Track per-zoom first offset / counts for § 11 #17.
        if (!zoomSeen[z]) {
            zoomSeen[z]           = true;
            firstOffsetPerZoom[z] = mHeader.indexOffset + i * static_cast<uint32_t>(kIndexEntrySize);
        }
        ++walkedPerZoom[z];

        expectedTileOffset += align4(length);

        prevZ = z; prevX = x; prevY = y; havePrev = true;
    }

    // § 11 #18: extensions_offset = tile_blob_start + Σ padded_length(i).
    if (expectedTileOffset != mHeader.extensionsOffset) {
        return OpenResult::BadExtensionsOffset;
    }

    // § 11 #17: zoom_offsets[z] consistent with the walked index.
    for (std::size_t z = 0; z < kZoomDirCount; ++z) {
        const uint32_t walked = walkedPerZoom[z];
        if (mHeader.zoomOffsets[z].count != walked) {
            return OpenResult::BadZoomDirectory;
        }
        if (walked == 0) {
            if (mHeader.zoomOffsets[z].offset != 0) {
                return OpenResult::BadZoomDirectory;
            }
        } else if (mHeader.zoomOffsets[z].offset != firstOffsetPerZoom[z]) {
            return OpenResult::BadZoomDirectory;
        }
    }

    // § 11 #23: SingleImage structural rules.
    if (mHeader.addressing == Addressing::SingleImage) {
        if (mHeader.tileCount != 1) {
            return OpenResult::BadSingleImage;
        }
        if (mHeader.zoomMin != 0 || mHeader.zoomMax != 0) {
            return OpenResult::BadSingleImage;
        }
        if (mHeader.axis != Axis::XYZ) {
            return OpenResult::BadSingleImage;
        }
        uint8_t e[kIndexEntrySize];
        if (!readAt(mHeader.indexOffset, e, kIndexEntrySize)) {
            return OpenResult::IoError;
        }
        if (e[0] != 0 || readU32LE(e + 4) != 0 || readU32LE(e + 8) != 0) {
            return OpenResult::BadSingleImage;
        }
        if (mHeader.zoomOffsets[0].count != 1
                || mHeader.zoomOffsets[0].offset != mHeader.indexOffset) {
            return OpenResult::BadSingleImage;
        }
        for (std::size_t z = 1; z < kZoomDirCount; ++z) {
            if (mHeader.zoomOffsets[z].offset != 0 || mHeader.zoomOffsets[z].count != 0) {
                return OpenResult::BadSingleImage;
            }
        }
    }

    // § 11 #19-#20,#22,#26-#29,#34-#38: extension-section framing + payload
    // contents (AFFN/NAME/SRCD/ATTR) and duplicate-tag detection.
    const uint32_t crcStart = static_cast<uint32_t>(size - kFooterSize);
    OpenResult extRes = walkExtensions(mHeader.extensionsOffset, crcStart);
    if (extRes != OpenResult::Ok) {
        return extRes;
    }

    // § 11 #24: CRC-32 footer. Caller-asserted trust (spec § 10) when
    // skipCrcVerify was set by an explicit opt-in -- see openFromFile()'s
    // doc comment. Every other § 11 rule above still ran unconditionally;
    // only this O(file_size) scan is skippable.
    return skipCrcVerify ? OpenResult::Ok : verifyCrc();
}

OpenResult Container::walkExtensions(uint32_t extensionsOffset, uint32_t crcStart)
{
    bool seenAffn = false;
    bool seenAttr = false;
    bool seenSrcd = false;

    // A re-walk of a Container that already had a pack open must not leave
    // the previous pack's attribution behind.
    mAttrOffset = 0;
    mAttrLength = 0;

    uint32_t pos = extensionsOffset;
    while (pos < crcStart) {
        if (crcStart - pos < 8) {
            return OpenResult::BadExtensionFraming;
        }
        uint8_t tag[8];
        if (!readAt(pos, tag, 8)) {
            return OpenResult::IoError;
        }
        // § 11 #27 / #28: tag byte 1 in [A-Z, a-z], bytes 2-4 printable ASCII.
        const uint8_t b0    = tag[0];
        const bool    upper = (b0 >= 'A' && b0 <= 'Z');
        const bool    lower = (b0 >= 'a' && b0 <= 'z');
        if (!upper && !lower) {
            return OpenResult::BadExtensionTag;
        }
        for (int j = 1; j < 4; ++j) {
            if (tag[j] < 0x20 || tag[j] > 0x7E) {
                return OpenResult::BadExtensionTag;
            }
        }
        const uint32_t length = readU32LE(tag + 4);
        // § 11 #19: overflow-safe upper bound.
        if (length > (crcStart - pos - 8)) {
            return OpenResult::BadExtensionFraming;
        }
        const uint32_t paddedLen      = align4(length);
        if (paddedLen > (crcStart - pos - 8)) {
            return OpenResult::BadExtensionFraming;
        }
        const uint32_t payloadOffset = pos + 8;
        // Padding bytes after payload MUST be 0x00.
        for (uint32_t p = length; p < paddedLen; ++p) {
            uint8_t padByte;
            if (!readAt(payloadOffset + p, &padByte, 1)) {
                return OpenResult::IoError;
            }
            if (padByte != 0) {
                return OpenResult::BadExtensionFraming;
            }
        }

        const bool isNAME = tag[0] == 'N' && tag[1] == 'A' && tag[2] == 'M' && tag[3] == 'E';
        const bool isSRCD = tag[0] == 'S' && tag[1] == 'R' && tag[2] == 'C' && tag[3] == 'D';
        const bool isATTR = tag[0] == 'A' && tag[1] == 'T' && tag[2] == 'T' && tag[3] == 'R';
        const bool isAFFN = tag[0] == 'A' && tag[1] == 'F' && tag[2] == 'F' && tag[3] == 'N';

        if (upper) {
            // § 11 #20: reject unknown upper-case tags.
            if (!isNAME && !isSRCD && !isATTR && !isAFFN) {
                return OpenResult::BadExtensionTag;
            }
            // § 11 #29: each upper-case tag (except NAME) at most once.
            if (isSRCD) {
                if (seenSrcd) {
                    return OpenResult::DuplicateExtensionTag;
                }
                seenSrcd     = true;
                OpenResult r = validateUtf8Only(payloadOffset, length);
                if (r != OpenResult::Ok) {
                    return r;
                }
            } else if (isATTR) {
                if (seenAttr) {
                    return OpenResult::DuplicateExtensionTag;
                }
                seenAttr     = true;
                OpenResult r = validateAttrText(payloadOffset, length);
                if (r != OpenResult::Ok) {
                    return r;
                }
                // Remember where it is. Validation already proved the bytes
                // are well-formed; recording the location is what lets a map
                // app show the credit the pack's licence obliges it to.
                mAttrOffset = payloadOffset;
                mAttrLength = length;
            } else if (isAFFN) {
                if (seenAffn) {
                    return OpenResult::DuplicateExtensionTag;
                }
                seenAffn = true;
                // § 11 #36: AFFN only legal under LocalLinear.
                if (mHeader.projection != Projection::LocalLinear) {
                    return OpenResult::UnexpectedAffn;
                }
                OpenResult r = validateAffn(payloadOffset, length);
                if (r != OpenResult::Ok) {
                    return r;
                }
            } else { // isNAME — cardinality is per-locale, checked inside validateName.
                OpenResult r = validateName(pos, payloadOffset, length, extensionsOffset);
                if (r != OpenResult::Ok) {
                    return r;
                }
            }
        }
        pos += 8 + paddedLen;
    }
    // Last section's padded end MUST equal file_size − 4.
    if (pos != crcStart) {
        return OpenResult::BadExtensionFraming;
    }

    // § 11 #22: LocalLinear requires exactly the AFFN section this walk
    // would have already rejected as UnexpectedAffn if projection disagreed.
    if (mHeader.projection == Projection::LocalLinear && !seenAffn) {
        return OpenResult::MissingAffn;
    }
    return OpenResult::Ok;
}

bool Container::utf8ValidateRangeImpl(uint32_t payloadOffset, uint32_t length, bool checkAttrRules) const
{
    Utf8State state;
    uint8_t   chunk[kChunkSize];
    uint32_t  remaining = length;
    uint32_t  pos       = payloadOffset;
    uint8_t   lastByte  = 0;
    bool      any       = false;

    while (remaining > 0) {
        const uint32_t take = remaining < kChunkSize ? remaining : static_cast<uint32_t>(kChunkSize);
        if (!readAt(pos, chunk, take)) {
            return false;
        }
        for (uint32_t i = 0; i < take; ++i) {
            const uint8_t b = chunk[i];
            bool          completed = false;
            if (!feedUtf8Byte(state, b, completed)) {
                return false;
            }
            if (completed && checkAttrRules) {
                const uint32_t cp             = state.codepoint;
                const bool     isBadC0Control = (cp >= 0x01 && cp <= 0x1F && cp != 0x0A);
                if (isBadC0Control || cp == 0x7F || cp == 0x85 || cp == 0x2028 || cp == 0x2029) {
                    return false;
                }
            }
            lastByte = b;
            any      = true;
        }
        pos       += take;
        remaining -= take;
    }
    if (state.remaining != 0) {
        return false; // truncated multi-byte sequence
    }
    if (checkAttrRules) {
        if (!any) {
            return false; // § 11 #38(b): zero-length ATTR payload
        }
        if (lastByte == 0x0A) {
            return false; // § 11 #38(c): trailing LF
        }
    }
    return true;
}

OpenResult Container::validateUtf8Only(uint32_t payloadOffset, uint32_t length) const
{
    if (!utf8ValidateRangeImpl(payloadOffset, length, false)) {
        return OpenResult::BadSrcdOrAttrText;
    }
    return OpenResult::Ok;
}

OpenResult Container::validateAttrText(uint32_t payloadOffset, uint32_t length) const
{
    if (!utf8ValidateRangeImpl(payloadOffset, length, true)) {
        return OpenResult::BadSrcdOrAttrText;
    }
    return OpenResult::Ok;
}

OpenResult Container::validateAffn(uint32_t payloadOffset, uint32_t length) const
{
    // § 11 #34: length must be exactly 48 (six f64 coefficients).
    if (length != 48) {
        return OpenResult::BadAffnLength;
    }
    uint8_t buf[48];
    if (!readAt(payloadOffset, buf, 48)) {
        return OpenResult::IoError;
    }
    // § 11 #39: land 64-bit payload values in an aligned local before
    // interpreting them — a section may start 4-aligned-not-8-aligned.
    for (int i = 0; i < 6; ++i) {
        const uint64_t bits = readU64LE(buf + i * 8);
        double         v;
        std::memcpy(&v, &bits, sizeof(v));
        // § 11 #35: all six coefficients must be finite.
        if (!std::isfinite(v)) {
            return OpenResult::BadAffnNotFinite;
        }
    }
    return OpenResult::Ok;
}

OpenResult Container::validateName(uint32_t sectionStart, uint32_t payloadOffset, uint32_t length,
                                    uint32_t extensionsOffset) const
{
    // § 11 #26: payload must hold at least the tag_length byte, and
    // 1 + tag_length must not exceed the payload.
    if (length < 1) {
        return OpenResult::BadNameLength;
    }
    uint8_t tagLenByte;
    if (!readAt(payloadOffset, &tagLenByte, 1)) {
        return OpenResult::IoError;
    }
    const uint32_t tagLength = tagLenByte;
    if (1u + tagLength > length) {
        return OpenResult::BadNameLength;
    }

    // § 11 #37: bcp47_tag restricted to "" / "xx" / "xx-XX".
    uint8_t tagBuf[5];
    if (tagLength > 0) {
        if (tagLength != 2 && tagLength != 5) {
            return OpenResult::BadNameText;
        }
        if (!readAt(payloadOffset + 1, tagBuf, tagLength)) {
            return OpenResult::IoError;
        }
        if (tagLength == 2) {
            if (!(tagBuf[0] >= 'a' && tagBuf[0] <= 'z' && tagBuf[1] >= 'a' && tagBuf[1] <= 'z')) {
                return OpenResult::BadNameText;
            }
        } else {
            if (!(tagBuf[0] >= 'a' && tagBuf[0] <= 'z' && tagBuf[1] >= 'a' && tagBuf[1] <= 'z'
                    && tagBuf[2] == '-'
                    && tagBuf[3] >= 'A' && tagBuf[3] <= 'Z' && tagBuf[4] >= 'A' && tagBuf[4] <= 'Z')) {
                return OpenResult::BadNameText;
            }
        }
    }

    // § 11 #37: name field must be valid UTF-8.
    const uint32_t nameOffset = payloadOffset + 1 + tagLength;
    const uint32_t nameLength = length - 1 - tagLength;
    if (!utf8ValidateRangeImpl(nameOffset, nameLength, false)) {
        return OpenResult::BadNameText;
    }

    // § 11 #29: no two NAME sections may share a bcp47_tag. Re-walk earlier
    // extension sections comparing tags — O(1) extra memory (no growing set
    // of seen locales to store), O(n^2) time over extension sections, and n
    // is always tiny in a real pack (a handful of locales at most).
    uint32_t pos = extensionsOffset;
    while (pos < sectionStart) {
        uint8_t otherTag[8];
        if (!readAt(pos, otherTag, 8)) {
            return OpenResult::IoError;
        }
        const uint32_t otherLength       = readU32LE(otherTag + 4);
        const uint32_t otherPaddedLength = align4(otherLength);
        if (otherTag[0] == 'N' && otherTag[1] == 'A' && otherTag[2] == 'M' && otherTag[3] == 'E') {
            uint8_t otherTagLenByte;
            if (!readAt(pos + 8, &otherTagLenByte, 1)) {
                return OpenResult::IoError;
            }
            const uint32_t otherTagLength = otherTagLenByte;
            if (otherTagLength == tagLength) {
                bool same = true;
                if (tagLength > 0) {
                    uint8_t otherTagBuf[5];
                    if (!readAt(pos + 8 + 1, otherTagBuf, tagLength)) {
                        return OpenResult::IoError;
                    }
                    same = std::memcmp(otherTagBuf, tagBuf, tagLength) == 0;
                }
                if (same) {
                    return OpenResult::DuplicateExtensionTag;
                }
            }
        }
        pos += 8 + otherPaddedLength;
    }
    return OpenResult::Ok;
}

OpenResult Container::verifyCrc() const
{
    const uint64_t size     = backendSize();
    const uint32_t crcStart = static_cast<uint32_t>(size - kFooterSize);

    uint32_t crc = 0xFFFFFFFFu;
    uint8_t  chunk[kChunkSize];
    uint32_t pos = 0;
    while (pos < crcStart) {
        const uint32_t take = (crcStart - pos) < kChunkSize ? (crcStart - pos)
                                                             : static_cast<uint32_t>(kChunkSize);
        if (!readAt(pos, chunk, take)) {
            return OpenResult::IoError;
        }
        crc = crc32Update(crc, chunk, take);
        pos += take;
    }
    crc ^= 0xFFFFFFFFu;

    uint8_t footer[kFooterSize];
    if (!readAt(crcStart, footer, kFooterSize)) {
        return OpenResult::IoError;
    }
    const uint32_t storedCrc = readU32LE(footer);
    return (storedCrc == crc) ? OpenResult::Ok : OpenResult::CrcMismatch;
}

bool Container::declaredCrc32(uint32_t &out) const
{
    if (!isOpen()) {
        return false;
    }
    const uint64_t size     = backendSize();
    const uint32_t crcStart = static_cast<uint32_t>(size - kFooterSize);
    uint8_t footer[kFooterSize];
    if (!readAt(crcStart, footer, kFooterSize)) {
        return false;
    }
    out = readU32LE(footer);
    return true;
}

bool Container::attribution(char *dst, size_t dstSize) const
{
    if (!isOpen() || dst == nullptr || mAttrLength == 0) {
        return false;
    }
    // Refuse rather than truncate: half a credit is worse than none, because
    // it looks like the obligation was met. See the header's note.
    if (dstSize < static_cast<size_t>(mAttrLength) + 1) {
        return false;
    }
    if (!readAt(mAttrOffset, dst, mAttrLength)) {
        return false;
    }
    dst[mAttrLength] = '\0';
    return true;
}

TileInfo Container::findTile(uint8_t z, uint32_t x, uint32_t y) const
{
    TileInfo out;
    if (!isOpen()) {
        return out;
    }
    // § 5.3 step 1: callers passing z ≥ 24 MUST NOT index past the directory.
    if (z >= kZoomDirCount) {
        return out;
    }
    const auto &dir = mHeader.zoomOffsets[z];
    if (dir.count == 0) {
        return out;
    }

    // Binary search the (x, y)-ordered slice of the index for this zoom,
    // one 20-byte entry read at a time.
    uint32_t lo = 0;
    uint32_t hi = dir.count;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        uint8_t        e[kIndexEntrySize];
        if (!readAt(dir.offset + static_cast<uint64_t>(mid) * kIndexEntrySize, e, kIndexEntrySize)) {
            return TileInfo { };
        }
        const uint32_t mx = readU32LE(e + 4);
        const uint32_t my = readU32LE(e + 8);
        if (mx < x || (mx == x && my < y)) {
            lo = mid + 1;
        } else if (mx > x || (mx == x && my > y)) {
            hi = mid;
        } else {
            out.found       = true;
            out.z           = e[0];
            out.x           = mx;
            out.y           = my;
            out.compression = static_cast<Compression>(e[1]);
            out.offset      = readU32LE(e + 12);
            out.length      = readU32LE(e + 16);
            return out;
        }
    }
    return out;
}

TileInfo Container::tileAtIndex(uint32_t i) const
{
    TileInfo out;
    if (!isOpen() || i >= mHeader.tileCount) {
        return out;
    }
    uint8_t e[kIndexEntrySize];
    if (!readAt(mHeader.indexOffset + static_cast<uint64_t>(i) * kIndexEntrySize, e, kIndexEntrySize)) {
        return out;
    }
    out.found       = true;
    out.z           = e[0];
    out.compression = static_cast<Compression>(e[1]);
    out.x           = readU32LE(e + 4);
    out.y           = readU32LE(e + 8);
    out.offset      = readU32LE(e + 12);
    out.length      = readU32LE(e + 16);
    return out;
}

uint32_t Container::tileCountAtZoom(uint8_t z) const
{
    if (!isOpen() || z >= kZoomDirCount) {
        return 0;
    }
    return mHeader.zoomOffsets[z].count;
}

ReadResult Container::readTile(const TileInfo &info, uint8_t *dst, size_t dstSize) const
{
    if (!isOpen() || !info.valid()) {
        return ReadResult::NotFound;
    }
    if (info.compression != Compression::None) {
        return ReadResult::UnsupportedCompression;
    }
    const size_t need = decodedTileSize();
    if (dstSize < need || info.length != need) {
        return ReadResult::BufferTooSmall;
    }
    if (!readAt(info.offset, dst, need)) {
        return ReadResult::IoError;
    }
    return ReadResult::Ok;
}

ReadResult Container::readTileRows(const TileInfo &info, uint16_t firstRow, uint16_t rowCount,
                                   uint8_t *dst, size_t dstSize) const
{
    if (!isOpen() || !info.valid()) {
        return ReadResult::NotFound;
    }
    if (info.compression != Compression::None) {
        return ReadResult::UnsupportedCompression;
    }
    const uint32_t dim = mHeader.tileDimPx;
    if (firstRow >= dim || rowCount == 0 || static_cast<uint32_t>(firstRow) + rowCount > dim) {
        return ReadResult::RowOutOfRange;
    }
    const uint32_t rowBytes = dim * bytesPerPixel(mHeader.pixelFormat);
    const size_t   need     = static_cast<size_t>(rowBytes) * rowCount;
    if (dstSize < need) {
        return ReadResult::BufferTooSmall;
    }
    const uint64_t start = static_cast<uint64_t>(info.offset) + static_cast<uint64_t>(firstRow) * rowBytes;
    if (!readAt(start, dst, need)) {
        return ReadResult::IoError;
    }
    return ReadResult::Ok;
}

const char* Container::describeResult(OpenResult r)
{
    switch (r) {
        case OpenResult::Ok:                    return "ok";
        case OpenResult::FileNotFound:           return "file not found";
        case OpenResult::FileTooShort:           return "file shorter than 296 bytes (§ 11 #1)";
        case OpenResult::FileTooLarge:           return "file > 2^32-1 bytes (§ 11 #30)";
        case OpenResult::BadMagic:               return "magic ≠ 'RAWT' (§ 11 #2)";
        case OpenResult::BadVersion:             return "format_version_major ≠ 1 (§ 11 #3)";
        case OpenResult::BadUuid:                return "pack_uuid == 0 or parent_uuid ≠ 0 (§ 11 #5/#6)";
        case OpenResult::BadEnum:                return "reserved enum value (§ 11 #7)";
        case OpenResult::BadEnumPair:             return "illegal projection × addressing pair (§ 11 #8)";
        case OpenResult::BadDimensions:           return "tile_dim_px == 0 (§ 11 #9)";
        case OpenResult::BadZoomRange:            return "zoom_max ≥ 24 or zoom_min > zoom_max (§ 11 #10)";
        case OpenResult::BadBbox:                 return "bbox out of range or inverted (§ 11 #11)";
        case OpenResult::BadIndexOffset:          return "index_offset ≠ 292 (§ 11 #25)";
        case OpenResult::BadIndexBounds:          return "tile_count exceeds file bounds (§ 11.5)";
        case OpenResult::BadTileEntry:            return "tile-index entry violates § 11 #12/#14/#16/#31/#32";
        case OpenResult::BadTileOrder:            return "tile index not strictly ascending (§ 11 #13)";
        case OpenResult::BadTileZoom:             return "tile z outside [zoom_min, zoom_max] (§ 11 #15)";
        case OpenResult::BadZoomDirectory:        return "zoom_offsets[z] inconsistent with index (§ 11 #17)";
        case OpenResult::BadExtensionsOffset:     return "extensions_offset misaligned or wrong (§ 11 #18)";
        case OpenResult::BadExtensionFraming:     return "extension-section framing violates § 7.1 (§ 11 #19)";
        case OpenResult::BadExtensionTag:         return "unknown upper-case tag or invalid tag bytes (§ 11 #20/#27/#28)";
        case OpenResult::BadSingleImage:          return "SingleImage structural rules violated (§ 11 #23)";
        case OpenResult::CrcMismatch:             return "CRC-32 footer mismatch (§ 11 #24)";
        case OpenResult::BadNameLength:           return "NAME payload too short for tag_length (§ 11 #26)";
        case OpenResult::BadNameText:             return "NAME name not UTF-8, or bcp47_tag outside v1 subset (§ 11 #37)";
        case OpenResult::DuplicateExtensionTag:   return "duplicate upper-case tag or NAME locale (§ 11 #29)";
        case OpenResult::MissingAffn:             return "LocalLinear without an AFFN section (§ 11 #22)";
        case OpenResult::UnexpectedAffn:          return "AFFN present but projection ≠ LocalLinear (§ 11 #36)";
        case OpenResult::BadAffnLength:           return "AFFN payload length ≠ 48 (§ 11 #34)";
        case OpenResult::BadAffnNotFinite:        return "AFFN coefficient is NaN or ±∞ (§ 11 #35)";
        case OpenResult::BadSrcdOrAttrText:       return "SRCD/ATTR not UTF-8, or ATTR text rules violated (§ 11 #38)";
        case OpenResult::IoError:                 return "I/O error";
    }
    return "unknown";
}

const char* Container::describeResult(ReadResult r)
{
    switch (r) {
        case ReadResult::Ok:                     return "ok";
        case ReadResult::NotFound:                return "tile not found (invalid TileInfo)";
        case ReadResult::BufferTooSmall:          return "destination buffer too small";
        case ReadResult::RowOutOfRange:            return "row range outside [0, tile_dim_px)";
        case ReadResult::UnsupportedCompression:  return "compression not decodable by this reader yet (RLE)";
        case ReadResult::IoError:                 return "I/O error (seek/read failed or returned short)";
    }
    return "unknown";
}

} // namespace RawTiles
} // namespace SDK
