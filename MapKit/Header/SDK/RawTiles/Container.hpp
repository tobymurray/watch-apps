/*
 * VENDORED, byte-for-byte apart from this notice, from una-sdk branch
 * feat/rawtiles-container @ b957aa62. It is app-private on purpose: the
 * rawtiles spec is still v0.x, and vendoring freezes no SDK surface while
 * it settles. Do not edit here -- fix it upstream on that branch and
 * re-vendor. When rawtiles reaches 1.0 this pair of files should become an
 * SDK library and this copy should be deleted; see MapKit/README.md.
 */
/**
 ******************************************************************************
 * @file    Container.hpp
 * @brief   Reader for the rawtiles binary tile-pack format.
 *
 * Implements the on-disk format defined at
 *   https://github.com/tobymurray/rawtiles (spec v0.6, wire format (1, 0))
 * to the extent required by a Quadtree/WebMercator or SingleImage/LocalLinear
 * reader over `ABGR2222` or `RGB565` pixels with `compression = None`.
 *
 * The reader enforces every § 11 rule that gates safe pointer/offset
 * arithmetic, tile lookup, and extension-payload contents (#1-#20, #22-#29,
 * #31, #32, #34-#38); see describeResult() for the exact rule cited by each
 * result code. `compression = RLE` (§ 9.11) tile-index entries are accepted
 * structurally at open (RLE is a legal v1 enum value; rejecting it outright
 * would be a false reject) but are not decodable yet: readTile() /
 * readTileRows() return ReadResult::UnsupportedCompression for them. Per-tile
 * padding non-zero-ness (§ 11 #33) is not checked: this reader never reads
 * padding bytes (row-streaming access pattern), which § 11.2 explicitly
 * exempts from the obligation.
 *
 * Two open() entry points back the same validation and tile-serving code
 * against different byte sources:
 *   - openFromMemory(): borrows a caller-owned buffer (host tests, fixtures).
 *   - openFromFile(): owns a `SDK::Interface::IFile` and streams via
 *     seek()+read(), never holding more than one section's worth of bytes
 *     resident. This is the device path: `IFile` is absolute-seek + `char*`
 *     read with no positioned-read (pread) primitive, so "streaming" means
 *     seek+read bookkeeping, and served tile bytes are always copied into a
 *     caller-owned buffer (no mmap on FatFs/eMMC).
 ******************************************************************************
 */

#ifndef __SDK_RAWTILES_CONTAINER_HPP
#define __SDK_RAWTILES_CONTAINER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"

namespace SDK
{
namespace RawTiles
{

/**
 * @brief Pixel-format enum (header byte 56). See spec § 8.1.
 */
enum class PixelFormat : uint8_t {
    ABGR2222 = 1,
    RGB565   = 2,
};

/**
 * @brief Projection enum (header byte 57). See spec § 8.2.
 */
enum class Projection : uint8_t {
    WebMercator = 1,
    LocalLinear = 3,
};

/**
 * @brief Tile-addressing-scheme enum (header byte 58). See spec § 8.3.
 */
enum class Addressing : uint8_t {
    Quadtree    = 1,
    SingleImage = 2,
};

/**
 * @brief Tile-axis convention (header byte 59). See spec § 8.4.
 */
enum class Axis : uint8_t {
    XYZ = 1, ///< Slippy-map default; y increases southward.
    TMS = 2, ///< gdal2tiles default; y increases northward.
};

/**
 * @brief Per-tile compression enum (tile-index byte 1). See spec § 8.5.
 *
 * @c RLE tiles are recognised and pass structural validation but are not
 * decodable by this reader yet; see the file-level doc comment.
 */
enum class Compression : uint8_t {
    None = 0,
    RLE  = 1,
};

/**
 * @brief On-disk uncompressed byte width of one pixel. Spec § 6.2.
 */
inline uint16_t bytesPerPixel(PixelFormat pf)
{
    return pf == PixelFormat::RGB565 ? 2 : 1;
}

/**
 * @brief Decoded header fields (host byte order). Mirrors spec § 4.
 */
struct Header {
    uint8_t     formatMajor;     ///< Wire-format major. Always 1 in v1.
    uint8_t     formatMinor;     ///< Wire-format minor.
    uint8_t     packUuid[16];    ///< Non-zero opaque pack identifier.
    uint8_t     supersedesUuid[16];
    PixelFormat pixelFormat;
    Projection  projection;
    Addressing  addressing;
    Axis        axis;
    uint16_t    tileDimPx;       ///< Pixel side length; non-zero.
    uint8_t     zoomMin;
    uint8_t     zoomMax;
    int32_t     bboxMinLonUDeg;  ///< Microdegrees (1e-6 °).
    int32_t     bboxMinLatUDeg;
    int32_t     bboxMaxLonUDeg;
    int32_t     bboxMaxLatUDeg;
    uint64_t    buildTimestamp;  ///< Unix epoch seconds; 0 = no freshness info.
    uint32_t    tileCount;
    uint32_t    indexOffset;     ///< Always 292 in v1.
    struct {
        uint32_t offset;
        uint32_t count;
    } zoomOffsets[24];
    uint32_t    extensionsOffset;
};

/**
 * @brief Index-lookup result: which tile, where, how big — no bytes yet.
 *
 * Deliberately separates "where is the tile" (index lookup, no I/O) from
 * "give me its bytes" (readTile() / readTileRows(), one seek+read on the
 * file backend). @c offset is an absolute byte offset into the pack.
 */
struct TileInfo {
    bool        found = false;
    uint8_t     z = 0;
    uint32_t    x = 0;
    uint32_t    y = 0;
    Compression compression = Compression::None;
    uint32_t    offset = 0;  ///< Absolute byte offset of the tile bytes.
    uint32_t    length = 0;  ///< On-disk byte length (encoded, for RLE).

    /// True if the tile was found in the index.
    bool valid() const { return found; }
};

/**
 * @brief Result codes for Container::open*. Anything other than @c Ok means
 *        the container is not safe to query.
 */
enum class OpenResult {
    Ok = 0,
    FileNotFound,
    FileTooShort,        ///< Less than 296 bytes (§ 11 #1).
    FileTooLarge,        ///< > 2^32 − 1 bytes (§ 11 #30).
    BadMagic,            ///< First 4 bytes ≠ "RAWT" (§ 11 #2).
    BadVersion,          ///< Major ≠ 1 (§ 11 #3).
    BadUuid,             ///< pack_uuid == 0 or parent_uuid ≠ 0 (§ 11 #5, #6).
    BadEnum,             ///< Reserved pixel/projection/addressing/axis/compression value (§ 11 #7).
    BadEnumPair,         ///< Illegal projection × addressing pair (§ 11 #8).
    BadDimensions,       ///< tile_dim_px == 0 (§ 11 #9).
    BadZoomRange,        ///< zoom_max ≥ 24 or zoom_min > zoom_max (§ 11 #10).
    BadBbox,             ///< Out-of-range or inverted bbox (§ 11 #11).
    BadIndexOffset,      ///< index_offset ≠ 292 (§ 11 #25).
    BadIndexBounds,      ///< tile_count too large for file size (§ 11.5).
    BadTileEntry,        ///< flags/reserved/offset/length/alignment/xy-range (§ 11 #12,#14,#16,#31,#32).
    BadTileOrder,        ///< Entries not strictly ascending by (z, x, y) (§ 11 #13).
    BadTileZoom,         ///< Entry z < zoom_min or > zoom_max (§ 11 #15).
    BadZoomDirectory,    ///< zoom_offsets[z] inconsistent with walked index (§ 11 #17).
    BadExtensionsOffset, ///< extensions_offset misaligned or wrong padded-sum (§ 11 #18).
    BadExtensionFraming, ///< Extension-section framing/padding/stranded-bytes (§ 11 #19).
    BadExtensionTag,     ///< Unknown upper-case tag or invalid tag bytes (§ 11 #20, #27, #28).
    BadSingleImage,      ///< SingleImage rules violated (§ 11 #23).
    CrcMismatch,         ///< Footer CRC-32 doesn't match body (§ 11 #24).
    BadNameLength,       ///< NAME payload too short for tag_length (§ 11 #26).
    BadNameText,         ///< NAME name not UTF-8, or bcp47_tag outside the v1 subset (§ 11 #37).
    DuplicateExtensionTag, ///< Duplicate upper-case tag, or duplicate NAME locale (§ 11 #29).
    MissingAffn,         ///< LocalLinear without an AFFN section (§ 11 #22).
    UnexpectedAffn,      ///< AFFN present but projection ≠ LocalLinear (§ 11 #36).
    BadAffnLength,       ///< AFFN payload length ≠ 48 (§ 11 #34).
    BadAffnNotFinite,    ///< An AFFN coefficient is NaN or ±∞ (§ 11 #35).
    BadSrcdOrAttrText,   ///< SRCD/ATTR not UTF-8, or ATTR text rules violated (§ 11 #38).
    IoError,
};

/**
 * @brief Result codes for Container::readTile / readTileRows.
 */
enum class ReadResult {
    Ok = 0,
    NotFound,               ///< @p info was not valid() (caller error).
    BufferTooSmall,         ///< @p dstSize smaller than the bytes to serve.
    RowOutOfRange,          ///< firstRow/rowCount outside [0, tile_dim_px).
    UnsupportedCompression, ///< compression == RLE; no decoder yet.
    IoError,                ///< seek/read failed, or returned fewer bytes
                            ///< than requested (a truncated file reads as
                            ///< this, not as a distinct code — the caller
                            ///< cannot act differently on the two).
};

/**
 * @brief Reads, validates, and serves tiles from a rawtiles pack.
 *
 * Usage (memory-backed, e.g. host tests):
 * @code
 *   SDK::RawTiles::Container c;
 *   if (c.openFromMemory(bytes, size) != OpenResult::Ok) { ... }
 *   auto info = c.findTile(13, 1306, 2825);
 *   if (info.valid()) {
 *       std::vector<uint8_t> buf(c.decodedTileSize());
 *       c.readTile(info, buf.data(), buf.size());
 *   }
 * @endcode
 *
 * Usage (device, streamed over IFileSystem):
 * @code
 *   SDK::RawTiles::Container c;
 *   if (c.openFromFile(fs, "stanley.rawtiles") != OpenResult::Ok) { ... }
 *   // open at app start, not lazily on first pan (measured on hardware:
 *   // the first filesystem touch after app start costs ~113 ms once,
 *   // ~4 ms after).
 *   uint8_t tileBuf[64 * 1024]; // caller-owned; no heap in the read path.
 *   auto info = c.findTile(z, x, y);
 *   if (info.valid()) c.readTile(info, tileBuf, sizeof(tileBuf));
 * @endcode
 *
 * Validation is eager and full: open() success means every § 11 rule has
 * already been checked (no rule can fire later). This costs one bounded
 * O(tile_count + extension_bytes) pass over the pack at open time — cheap
 * enough on device (measured: first read ~113 ms once, ~4-9 ms per
 * 64 KiB tile thereafter) that the lazy-validation profile § 11.1 also
 * permits isn't worth its complexity here. Open never allocates more than a
 * small, fixed-size stack scratch buffer regardless of pack size: the file
 * backend never holds the whole pack resident, and the memory backend only
 * borrows the caller's buffer.
 *
 * readTile() does not re-verify the § 14.5 per-tile hash — that is a
 * validator's job, not a renderer's; re-hashing
 * every tile on every pan would cost far more than the read itself.
 */
class Container {
public:
    Container() = default;
    ~Container() = default;

    Container(const Container&)            = delete;
    Container& operator=(const Container&) = delete;
    Container(Container&&)                 = default;
    Container& operator=(Container&&)      = default;

    /**
     * @brief Opens a pack from an in-memory byte buffer (for tests / fixtures).
     * @param data: Pointer to pack bytes; borrowed, not copied — must outlive
     *        the Container.
     * @param size: Length of @p data in bytes.
     * @param skipCrcVerify: see openFromFile()'s doc comment — same
     *        Caller-asserted-trust semantics apply here.
     */
    OpenResult openFromMemory(const uint8_t *data, std::size_t size,
                               bool skipCrcVerify = false);

    /**
     * @brief Opens a pack from a filesystem path, streaming via @c IFile.
     * @param fs: Filesystem to resolve @p path against. Not owned; must
     *        outlive the Container.
     * @param path: Path passed to @c fs.file(); no path convention is
     *        applied here. On hardware an app's filesystem is sandboxed —
     *        absolute volume paths (e.g. "N:/...") do not resolve from an
     *        app, so callers pass sandbox-relative paths.
     * @param skipCrcVerify: when @c true, skips the O(file-size) CRC-32
     *        footer check (spec § 11 #24) — "Caller-asserted trust" per
     *        rawtiles spec § 10. Every other § 11 structural rule (magic,
     *        version, uuid, enums, bbox, zoom range, tile-index bounds/
     *        order/entries, extension framing/payloads) still runs
     *        unconditionally, so a genuinely malformed pack is still
     *        rejected immediately regardless of this flag — only the CRC
     *        scan itself is skippable. Only pass @c true when the caller
     *        has independently established that the exact same bytes
     *        already passed a full CRC scan (e.g. a cached verification
     *        result keyed on file size + the footer's declared CRC, see
     *        declaredCrc32()) — an unconditionally-skipped CRC on an
     *        unverified byte stream is not a conforming use of this
     *        parameter. Defaults to @c false (eager verify, unchanged
     *        behavior for any caller that doesn't opt in).
     * @return @c OpenResult::Ok on success; the container is closed on failure.
     */
    OpenResult openFromFile(SDK::Interface::IFileSystem &fs, const char *path,
                             bool skipCrcVerify = false);

    /**
     * @brief Releases the backend (closing the file, if any) and resets
     *        header state.
     */
    void close();

    /**
     * @brief @c true if a valid pack is currently open.
     */
    bool isOpen() const { return mBackend != Backend::None; }

    /**
     * @brief Returns the decoded header. Only valid when @c isOpen() is true.
     */
    const Header& header() const { return mHeader; }

    /**
     * @brief Total on-disk size of the currently-open pack, in bytes.
     * @return 0 if @c !isOpen().
     */
    uint64_t packSize() const { return isOpen() ? backendSize() : 0; }

    /**
     * @brief Reads the footer's declared CRC-32 (spec § 10) into @p out.
     *        Cheap: reads only the trailing 4 bytes, not the full-body scan
     *        verifyCrc() performs. Intended for callers implementing their
     *        own Caller-asserted-trust cache (see openFromFile()'s
     *        @p skipCrcVerify) — compare this value (plus packSize()) against
     *        a previously-recorded good result instead of trusting blindly.
     * @return @c false if @c !isOpen() or the read fails (defensive; should
     *         not happen on a Container that already passed
     *         parseAndValidate()).
     */
    bool declaredCrc32(uint32_t &out) const;

    /**
     * @brief Byte size of one fully-decoded (compression = None) tile for
     *        this pack's @c pixel_format / @c tile_dim_px.
     */
    size_t decodedTileSize() const
    {
        return static_cast<size_t>(mHeader.tileDimPx) * mHeader.tileDimPx
             * bytesPerPixel(mHeader.pixelFormat);
    }

    /**
     * @brief Looks up the tile at @p (z, x, y) per spec § 5.3. Index-only:
     *        performs no I/O beyond the resident zoom directory.
     * @return @c valid() is @c false when the tile is absent or @p z is out
     *         of range (z ≥ 24 is handled without indexing past the
     *         directory, per § 5.3 step 1).
     */
    TileInfo findTile(uint8_t z, uint32_t x, uint32_t y) const;

    /**
     * @brief Fetches the @p i-th tile-index entry (0-based, file order).
     *
     * Useful for callers that want to iterate the pack without knowing the
     * coordinates in advance — picking a centre tile, building previews, etc.
     *
     * @param i: index in @c [0, header().tileCount).
     * @return A populated @c TileInfo; @c valid() is @c false when @p i is
     *         out of range.
     */
    TileInfo tileAtIndex(uint32_t i) const;

    /**
     * @brief Number of tile-index entries at zoom @p z (0 if z ≥ 24).
     */
    uint32_t tileCountAtZoom(uint8_t z) const;

    /**
     * @brief Reads the full tile into a caller-owned buffer.
     * @param info: A @c valid() TileInfo from findTile()/tileAtIndex().
     * @param dst: Destination buffer, at least @c decodedTileSize() bytes
     *        (compression = None) or at least @p info.length bytes
     *        (compression ≠ None; not yet supported — see class doc).
     * @param dstSize: Capacity of @p dst in bytes.
     */
    ReadResult readTile(const TileInfo &info, uint8_t *dst, size_t dstSize) const;

    /**
     * @brief Reads a contiguous row range of an uncompressed tile into a
     *        caller-owned buffer, without materialising the whole tile.
     *
     * Only @c Compression::None is supported today (a straight byte-range
     * copy); @c RLE rows would need mid-run suspension (spec § 9.11) that
     * this reader does not implement yet (see class doc).
     *
     * @param firstRow, rowCount: Row range in [0, tile_dim_px).
     * @param dst: Destination buffer, at least
     *        @c rowCount * tile_dim_px * bytesPerPixel(pixelFormat) bytes.
     */
    ReadResult readTileRows(const TileInfo &info, uint16_t firstRow, uint16_t rowCount,
                            uint8_t *dst, size_t dstSize) const;

    /**
     * @brief Human-readable diagnostic for an @c OpenResult.
     */
    static const char* describeResult(OpenResult r);

    /**
     * @brief Human-readable diagnostic for a @c ReadResult.
     */
    static const char* describeResult(ReadResult r);

private:
    enum class Backend : uint8_t { None, Memory, File };

    Backend                              mBackend = Backend::None;
    const uint8_t                       *mMemData = nullptr;
    size_t                                mMemSize = 0;
    std::unique_ptr<SDK::Interface::IFile> mFile;
    uint64_t                              mFileSize = 0;
    Header                                mHeader { };

    /// Reads exactly @p len bytes at absolute @p offset into @p dst from
    /// whichever backend is active. @c false on any short read, seek
    /// failure, or out-of-bounds access — the one place both backends'
    /// I/O errors converge, so validation and tile-serving share one path.
    bool readAt(uint64_t offset, void *dst, size_t len) const;

    uint64_t backendSize() const { return mBackend == Backend::Memory ? mMemSize : mFileSize; }

    OpenResult parseAndValidate(bool skipCrcVerify);
    OpenResult verifyCrc() const;
    OpenResult walkExtensions(uint32_t extensionsOffset, uint32_t crcStart) const;
    OpenResult validateAffn(uint32_t payloadOffset, uint32_t length) const;
    OpenResult validateName(uint32_t sectionStart, uint32_t payloadOffset, uint32_t length,
                             uint32_t extensionsOffset) const;
    OpenResult validateUtf8Only(uint32_t payloadOffset, uint32_t length) const;
    OpenResult validateAttrText(uint32_t payloadOffset, uint32_t length) const;

    /// Shared streaming UTF-8 validator behind validateUtf8Only()/
    /// validateAttrText()/validateName(); @p checkAttrRules also enforces
    /// § 11 #38's C0-control/DEL/NEL/LS/PS/empty/trailing-LF rules.
    bool utf8ValidateRangeImpl(uint32_t payloadOffset, uint32_t length, bool checkAttrRules) const;
};

} // namespace RawTiles
} // namespace SDK

#endif // __SDK_RAWTILES_CONTAINER_HPP
