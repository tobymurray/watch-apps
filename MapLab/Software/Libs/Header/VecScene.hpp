/**
 ******************************************************************************
 * @file    VecScene.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   A draft of the vector tile wire format, its decoder, and a
 *          deterministic scene generator to feed the benchmarks.
 ******************************************************************************
 *
 * **This is not the format.** It is a stand-in shaped like the one
 * `MapKit/Docs/VECTOR-PIPELINE-PROMPT.md` § 5 specifies, built so that what
 * the benchmarks measure is the real cost of the real access pattern rather
 * than the cost of a hand-waved one. If the numbers say go, the format that
 * gets written down inherits these decisions and their reasons; if they say
 * stop, this file is the cheapest artifact in the project to throw away.
 *
 * Three properties are load-bearing for the measurement, and each is here for
 * a stated reason rather than by convention:
 *
 *   1. **A per-tile layer directory.** The renderer must be able to seek to
 *      one layer of one tile and read only that, because painter's order runs
 *      across the whole viewport: every water polygon in every visible tile
 *      before any road in any of them. Without a directory the alternative is
 *      holding every visible tile's geometry resident at once, which the RAM
 *      budget does not have. So the directory is in the format, and the I/O
 *      benches measure the seek-per-(tile, layer) pattern it implies.
 *   2. **Classes, not attributes.** One byte per feature, drawn from the
 *      palette slots in `Palette.hpp` -- which is MAP_CARTOGRAPHY_SPEC.md's
 *      rule R4 ("one code per feature class, no exceptions") made structural.
 *      No tags, no key-value maps, no per-feature style. A renderer that had
 *      to interpret attributes would be measuring a different, larger thing.
 *   3. **A power-of-two extent.** Tile-local coordinates run 0..extent, and
 *      the transform to screen pixels is then a multiply and a shift rather
 *      than a divide per point. On a Cortex-M33 that is a few cycles per
 *      coordinate, and there are tens of thousands of coordinates per frame.
 *
 * Bytes:
 *
 *     header    'V', version, u16 extent, u8 layerCount, u8 reserved(0)
 *     directory layerCount x { u8 class, u8 kind, u16 offset, u16 length,
 *                              u16 featureCount }
 *     payload   per layer: featureCount x feature
 *     feature   varint pointCount, then pointCount x (zigzag dx, zigzag dy)
 *
 * Deltas are from the previous point, first point from (0, 0). Little-endian
 * where endianness applies; varints are LEB128-style, seven bits per byte,
 * high bit continues.
 *
 * ---------------------------------------------------------------------------
 * The generator, and why synthetic geometry is the right subject here
 *
 * A real extract would be better evidence about *one place*. What the gates
 * need is a *curve* -- render cost against feature count -- so that a real
 * pack's numbers can be predicted rather than re-measured for every city, and
 * so that "dense" is a number somebody can argue with rather than a place
 * somebody chose. `SceneParams` is that number, the presets say what they are
 * meant to represent, and the honest caveat is in the README: the presets are
 * judgements until somebody counts features in a real z14 tile of a European
 * city centre.
 *
 * Deterministic by construction (a named LCG, no rand(), no clock), so two
 * runs of a bench measure the same geometry and a regression is a regression.
 *
 * Pure: no SDK, no kernel, no filesystem. Host-tested.
 ******************************************************************************
 */

#ifndef MAPLAB_VECSCENE_HPP
#define MAPLAB_VECSCENE_HPP

#include "Canvas.hpp"

#include <cstdint>

namespace MapLab
{

/// Wire constants. `kExtent` is a power of two so the screen transform is a
/// shift; 4096 matches the conventional choice elsewhere and gives 6 cm of
/// resolution across a z14 tile, well under a pixel.
constexpr uint8_t  kSceneMagic   = 'V';
constexpr uint8_t  kSceneVersion = 1;
constexpr uint16_t kExtent       = 4096;
constexpr int      kExtentShift  = 12;
static_assert((1 << kExtentShift) == kExtent, "extent must be 2^kExtentShift");

constexpr int kMaxLayers          = 12;
constexpr int kHeaderBytes        = 6;
constexpr int kDirEntryBytes      = 8;
/// Points a single feature may carry. Sizes the decoder's scratch buffer, and
/// is the cap a writer must split against rather than exceed.
constexpr int kMaxPointsPerFeature = 512;

enum class Kind : uint8_t { Polyline = 0, Polygon = 1, Point = 2 };

struct LayerInfo {
    uint8_t  klass        = 0;  ///< A MapLab::Slot, as a byte.
    Kind     kind         = Kind::Polyline;
    uint16_t offset       = 0;  ///< From the start of the tile.
    uint16_t length       = 0;
    uint16_t featureCount = 0;
};

/**
 * @brief Writes one tile. Layers are written in order and the directory is
 *        patched in at finish(), so the writer is single-pass over a caller's
 *        buffer with no allocation.
 */
class SceneWriter
{
public:
    SceneWriter(uint8_t* buf, uint32_t capacity, uint16_t extent = kExtent);

    bool beginLayer(Slot klass, Kind kind);
    /// `xy` is 2*count tile-local coordinates, x first. Returns false if the
    /// buffer is full or the feature exceeds kMaxPointsPerFeature -- never a
    /// silent truncation, because a benchmark that quietly drew half a scene
    /// would report half a cost.
    bool addFeature(const int32_t* xy, int count);
    bool endLayer();
    bool finish();

    uint32_t size() const { return mSize; }
    bool     ok()   const { return mOk; }

private:
    bool put(uint8_t b);
    bool putVarint(uint32_t v);
    bool putZigZag(int32_t v);

    uint8_t*  mBuf;
    uint32_t  mCap;
    uint32_t  mSize = 0;
    uint16_t  mExtent;
    LayerInfo mLayers[kMaxLayers];
    int       mLayerCount   = 0;
    int       mCurrent      = -1;
    uint32_t  mLayerStart   = 0;
    bool      mOk           = true;
};

/**
 * @brief Reads a tile written by SceneWriter, decoding straight into canvas
 *        coordinates.
 *
 * Deliberately has no "give me the geometry" call. Decode and transform are
 * one pass into a caller-provided scratch buffer, because that is what a
 * renderer inside this RAM budget can afford -- and measuring a decode that
 * materialised the whole tile first would measure an architecture nobody can
 * ship.
 */
class SceneReader
{
public:
    /// Validates the header and directory. Every rule checked here is one a
    /// device reader would have to check anyway; a benchmark that skipped
    /// validation would be timing a reader nobody could ship.
    bool open(const uint8_t* buf, uint32_t size);

    uint8_t          layerCount() const { return mLayerCount; }
    const LayerInfo& layer(int i) const { return mLayers[i]; }
    uint16_t         extent()     const { return mExtent; }

    /// Decode every feature of layer `i`, transforming tile-local coordinates
    /// to canvas pixels: `screen = origin + (local * tilePx >> kExtentShift)`.
    /// `visit(const Pt*, int count)` is called once per feature.
    ///
    /// Returns the number of features visited, or -1 on malformed payload.
    template <class Visitor>
    int forEachFeature(int i, Pt* scratch, int scratchCap,
                       int16_t originX, int16_t originY, int16_t tilePx,
                       Visitor&& visit) const
    {
        if (i < 0 || i >= mLayerCount) {
            return -1;
        }
        const LayerInfo& L = mLayers[i];
        const uint8_t*   p = mBuf + L.offset;
        const uint8_t*   end = p + L.length;
        int seen = 0;

        for (uint16_t f = 0; f < L.featureCount; ++f) {
            uint32_t n = 0;
            if (!readVarint(p, end, n) || n == 0 || n > static_cast<uint32_t>(kMaxPointsPerFeature)) {
                return -1;
            }
            int32_t x = 0;
            int32_t y = 0;
            const int keep = (static_cast<int>(n) < scratchCap) ? static_cast<int>(n) : scratchCap;
            for (uint32_t k = 0; k < n; ++k) {
                int32_t dx = 0;
                int32_t dy = 0;
                if (!readZigZag(p, end, dx) || !readZigZag(p, end, dy)) {
                    return -1;
                }
                x += dx;
                y += dy;
                if (static_cast<int>(k) < keep) {
                    scratch[k].x = static_cast<int16_t>(originX + ((x * tilePx) >> kExtentShift));
                    scratch[k].y = static_cast<int16_t>(originY + ((y * tilePx) >> kExtentShift));
                }
            }
            visit(scratch, keep);
            ++seen;
        }
        return seen;
    }

private:
    static bool readVarint(const uint8_t*& p, const uint8_t* end, uint32_t& out);
    static bool readZigZag(const uint8_t*& p, const uint8_t* end, int32_t& out);

    const uint8_t* mBuf = nullptr;
    uint32_t       mSize = 0;
    uint16_t       mExtent = kExtent;
    uint8_t        mLayerCount = 0;
    LayerInfo      mLayers[kMaxLayers];
};

/**
 * @brief How much map is in a tile.
 *
 * The three presets are the benchmark's definition of rural / suburban / city
 * centre. They are judgements, not counts from a real extract, and the README
 * says so; `Tools/maplab_report.py` reports cost per feature as well as per
 * scene precisely so a corrected preset does not invalidate the measurement.
 */
struct SceneParams {
    uint16_t seed          = 1;
    uint16_t roadMajor     = 6;    ///< long polylines, cased
    uint16_t roadMinor     = 40;   ///< the street grid
    uint16_t paths         = 12;   ///< dashed
    uint16_t waterPolys    = 2;
    uint16_t waterLines    = 4;
    uint16_t woodPolys     = 6;
    uint16_t landusePolys  = 10;
    uint16_t buildings     = 60;   ///< small quads; the count that explodes in a city
    uint16_t contours      = 24;
    uint16_t pointsPerLine = 24;

    static SceneParams rural();
    static SceneParams suburban();
    static SceneParams cityCentre();

    /// Total features, for the per-feature column in the report.
    uint32_t featureTotal() const;
};

/// Build a scene into `buf`. Returns bytes written, or 0 if the buffer is too
/// small -- never a partial tile.
uint32_t generateScene(uint8_t* buf, uint32_t capacity, const SceneParams& p);

} // namespace MapLab

#endif // MAPLAB_VECSCENE_HPP
