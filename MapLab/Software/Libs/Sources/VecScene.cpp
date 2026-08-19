#include "VecScene.hpp"

namespace MapLab
{
namespace
{

/// Points the generator's own stack buffers hold. Deliberately far below
/// kMaxPointsPerFeature: the GUI thread has a 10 KB stack, and a 512-point
/// int32 pair buffer is 4 KB of it. The generator emits features well inside
/// this; a caller asking for more gets clamped, and the clamp is asserted by
/// a host test rather than left to be discovered on the device.
constexpr int kGenScratchPoints = 64;

/// Directory slots are reserved up front so the writer never has to move the
/// payload it has already written. A shipping writer would emit exactly
/// layerCount entries; the reader reads `layerCount` of them from a fixed
/// start and follows explicit offsets, so it accepts either shape.
constexpr uint32_t kPayloadStart = kHeaderBytes + kMaxLayers * kDirEntryBytes;

inline void put16(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
}

inline uint16_t get16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

/// Deterministic by design: a benchmark whose subject changes between runs
/// cannot show a regression. Numerical Recipes' LCG, named so nobody
/// substitutes rand() later and wonders why the numbers moved.
class Lcg
{
public:
    explicit Lcg(uint32_t seed) : mState(seed * 2654435761u + 1u) {}
    uint32_t next()
    {
        mState = mState * 1664525u + 1013904223u;
        return mState;
    }
    /// Uniform in [0, n). n must be non-zero.
    int32_t below(int32_t n) { return static_cast<int32_t>(next() % static_cast<uint32_t>(n)); }
    /// Uniform in [-n, n].
    int32_t jitter(int32_t n) { return below(2 * n + 1) - n; }
private:
    uint32_t mState;
};

} // namespace

// ---------------------------------------------------------------------------
// SceneWriter
// ---------------------------------------------------------------------------

SceneWriter::SceneWriter(uint8_t* buf, uint32_t capacity, uint16_t extent)
    : mBuf(buf), mCap(capacity), mExtent(extent)
{
    if (mCap < kPayloadStart) {
        mOk = false;
        return;
    }
    for (uint32_t i = 0; i < kPayloadStart; ++i) {
        mBuf[i] = 0;
    }
    mBuf[0] = kSceneMagic;
    mBuf[1] = kSceneVersion;
    put16(mBuf + 2, mExtent);
    mSize = kPayloadStart;
}

bool SceneWriter::put(uint8_t b)
{
    if (!mOk || mSize >= mCap) {
        mOk = false;
        return false;
    }
    mBuf[mSize++] = b;
    return true;
}

bool SceneWriter::putVarint(uint32_t v)
{
    while (v >= 0x80) {
        if (!put(static_cast<uint8_t>(v) | 0x80)) {
            return false;
        }
        v >>= 7;
    }
    return put(static_cast<uint8_t>(v));
}

bool SceneWriter::putZigZag(int32_t v)
{
    return putVarint(static_cast<uint32_t>((v << 1) ^ (v >> 31)));
}

bool SceneWriter::beginLayer(Slot klass, Kind kind)
{
    if (!mOk || mCurrent >= 0 || mLayerCount >= kMaxLayers) {
        mOk = false;
        return false;
    }
    mCurrent    = mLayerCount;
    mLayerStart = mSize;
    mLayers[mCurrent].klass        = static_cast<uint8_t>(klass);
    mLayers[mCurrent].kind         = kind;
    mLayers[mCurrent].featureCount = 0;
    return true;
}

bool SceneWriter::addFeature(const int32_t* xy, int count)
{
    if (!mOk || mCurrent < 0 || count <= 0 || count > kMaxPointsPerFeature) {
        mOk = false;
        return false;
    }
    if (!putVarint(static_cast<uint32_t>(count))) {
        return false;
    }
    int32_t px = 0;
    int32_t py = 0;
    for (int i = 0; i < count; ++i) {
        if (!putZigZag(xy[2 * i] - px) || !putZigZag(xy[2 * i + 1] - py)) {
            return false;
        }
        px = xy[2 * i];
        py = xy[2 * i + 1];
    }
    ++mLayers[mCurrent].featureCount;
    return true;
}

bool SceneWriter::endLayer()
{
    if (!mOk || mCurrent < 0) {
        mOk = false;
        return false;
    }
    const uint32_t len = mSize - mLayerStart;
    if (mLayerStart > 0xFFFFu || len > 0xFFFFu) {
        // The directory addresses a tile with 16-bit offsets, which is a cap
        // on tile size rather than an accident. A writer that overran it must
        // split the tile; it must never emit one a reader cannot address.
        mOk = false;
        return false;
    }
    mLayers[mCurrent].offset = static_cast<uint16_t>(mLayerStart);
    mLayers[mCurrent].length = static_cast<uint16_t>(len);
    ++mLayerCount;
    mCurrent = -1;
    return true;
}

bool SceneWriter::finish()
{
    if (!mOk || mCurrent >= 0) {
        mOk = false;
        return false;
    }
    mBuf[4] = static_cast<uint8_t>(mLayerCount);
    mBuf[5] = 0;
    for (int i = 0; i < mLayerCount; ++i) {
        uint8_t* e = mBuf + kHeaderBytes + i * kDirEntryBytes;
        e[0] = mLayers[i].klass;
        e[1] = static_cast<uint8_t>(mLayers[i].kind);
        put16(e + 2, mLayers[i].offset);
        put16(e + 4, mLayers[i].length);
        put16(e + 6, mLayers[i].featureCount);
    }
    return true;
}

// ---------------------------------------------------------------------------
// SceneReader
// ---------------------------------------------------------------------------

bool SceneReader::open(const uint8_t* buf, uint32_t size)
{
    mBuf = nullptr;
    if (buf == nullptr || size < kPayloadStart) {
        return false;
    }
    if (buf[0] != kSceneMagic || buf[1] != kSceneVersion) {
        return false;
    }
    const uint16_t extent = get16(buf + 2);
    if (extent != kExtent) {
        return false; // the shift-based transform is only valid for this extent
    }
    const uint8_t n = buf[4];
    if (n > kMaxLayers) {
        return false;
    }
    for (int i = 0; i < n; ++i) {
        const uint8_t* e = buf + kHeaderBytes + i * kDirEntryBytes;
        LayerInfo L;
        L.klass        = e[0];
        L.kind         = static_cast<Kind>(e[1]);
        L.offset       = get16(e + 2);
        L.length       = get16(e + 4);
        L.featureCount = get16(e + 6);
        if (L.klass >= static_cast<uint8_t>(Slot::Count)) {
            // Unknown classes are skipped, not rejected: that is the
            // forward-compatibility hinge, so an old watch can draw a newer
            // pack minus whatever it does not understand. Marked by a zeroed
            // feature count rather than dropped from the directory, so the
            // layer still accounts for its bytes.
            L.featureCount = 0;
        }
        if (static_cast<uint32_t>(L.offset) + L.length > size || L.offset < kPayloadStart) {
            return false;
        }
        mLayers[i] = L;
    }
    mBuf        = buf;
    mSize       = size;
    mExtent     = extent;
    mLayerCount = n;
    return true;
}

bool SceneReader::readVarint(const uint8_t*& p, const uint8_t* end, uint32_t& out)
{
    uint32_t v     = 0;
    int      shift = 0;
    while (p < end) {
        const uint8_t b = *p++;
        v |= static_cast<uint32_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
            out = v;
            return true;
        }
        shift += 7;
        if (shift > 28) {
            return false;
        }
    }
    return false;
}

bool SceneReader::readZigZag(const uint8_t*& p, const uint8_t* end, int32_t& out)
{
    uint32_t v = 0;
    if (!readVarint(p, end, v)) {
        return false;
    }
    out = static_cast<int32_t>((v >> 1) ^ (~(v & 1) + 1));
    return true;
}

// ---------------------------------------------------------------------------
// Scene generation
// ---------------------------------------------------------------------------

SceneParams SceneParams::rural()
{
    SceneParams p;
    p.seed = 11;
    p.roadMajor = 2; p.roadMinor = 8;  p.paths = 10;
    p.waterPolys = 1; p.waterLines = 2;
    p.woodPolys = 8;  p.landusePolys = 3; p.buildings = 6;
    p.contours = 30;  p.pointsPerLine = 20;
    return p;
}

SceneParams SceneParams::suburban()
{
    SceneParams p; // the struct's defaults are the suburban case
    p.seed = 22;
    return p;
}

SceneParams SceneParams::cityCentre()
{
    SceneParams p;
    p.seed = 33;
    p.roadMajor = 10; p.roadMinor = 110; p.paths = 18;
    p.waterPolys = 2; p.waterLines = 3;
    p.woodPolys = 4;  p.landusePolys = 18; p.buildings = 260;
    p.contours = 8;   p.pointsPerLine = 26;
    return p;
}

uint32_t SceneParams::featureTotal() const
{
    return static_cast<uint32_t>(roadMajor) + roadMinor + paths + waterPolys +
           waterLines + woodPolys + landusePolys + buildings + contours;
}

namespace
{

/// A wandering polyline across the tile, entering and leaving its edges the
/// way a clipped real feature does.
void makeLine(Lcg& rng, int32_t* xy, int n, bool horizontal, int32_t wobble)
{
    const int32_t step = kExtent / (n - 1);
    int32_t       base = rng.below(kExtent);
    for (int i = 0; i < n; ++i) {
        base += rng.jitter(wobble);
        if (base < 0)       { base = 0; }
        if (base >= kExtent) { base = kExtent - 1; }
        const int32_t along = i * step;
        xy[2 * i]     = horizontal ? along : base;
        xy[2 * i + 1] = horizontal ? base  : along;
    }
}

/// A closed blob: an n-gon with jittered radius. Convex enough to fill
/// correctly, irregular enough that the fill does real work.
void makeBlob(Lcg& rng, int32_t* xy, int n, int32_t cx, int32_t cy, int32_t r)
{
    // Integer circle without trigonometry: walk a coarse polygon by
    // quadrant-symmetric offsets. Exactness does not matter here; determinism
    // and shape do.
    for (int i = 0; i < n; ++i) {
        const int32_t t   = (i * 1024) / n;          // 0..1023 around the loop
        const int32_t quad = t / 256;
        const int32_t f    = t % 256;                // 0..255 within the quadrant
        int32_t dx = 0;
        int32_t dy = 0;
        switch (quad) {
            case 0: dx = 256 - f; dy = f;             break;
            case 1: dx = -f;      dy = 256 - f;       break;
            case 2: dx = f - 256; dy = -f;            break;
            default: dx = f;      dy = f - 256;       break;
        }
        const int32_t rr = r + rng.jitter(r / 4);
        xy[2 * i]     = cx + (dx * rr) / 256;
        xy[2 * i + 1] = cy + (dy * rr) / 256;
    }
}

bool emitLines(SceneWriter& w, Lcg& rng, Slot klass, Kind kind,
               int count, int pointsPerLine, int32_t wobble)
{
    if (count <= 0) {
        return true;
    }
    if (!w.beginLayer(klass, kind)) {
        return false;
    }
    int32_t xy[2 * kGenScratchPoints];
    const int n = (pointsPerLine > kGenScratchPoints) ? kGenScratchPoints : pointsPerLine;
    for (int i = 0; i < count; ++i) {
        makeLine(rng, xy, n, (i % 2) == 0, wobble);
        if (!w.addFeature(xy, n)) {
            return false;
        }
    }
    return w.endLayer();
}

bool emitBlobs(SceneWriter& w, Lcg& rng, Slot klass, int count, int32_t radius, int corners)
{
    if (count <= 0) {
        return true;
    }
    if (!w.beginLayer(klass, Kind::Polygon)) {
        return false;
    }
    int32_t xy[2 * kGenScratchPoints];
    if (corners > kGenScratchPoints) {
        corners = kGenScratchPoints;
    }
    for (int i = 0; i < count; ++i) {
        makeBlob(rng, xy, corners, rng.below(kExtent), rng.below(kExtent),
                 radius + rng.jitter(radius / 3));
        if (!w.addFeature(xy, corners)) {
            return false;
        }
    }
    return w.endLayer();
}

bool emitBuildings(SceneWriter& w, Lcg& rng, int count)
{
    if (count <= 0) {
        return true;
    }
    if (!w.beginLayer(Slot::Building, Kind::Polygon)) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        const int32_t x = rng.below(kExtent - 200);
        const int32_t y = rng.below(kExtent - 200);
        const int32_t bw = 40 + rng.below(120);
        const int32_t bh = 40 + rng.below(120);
        const int32_t xy[8] = { x, y, x + bw, y, x + bw, y + bh, x, y + bh };
        if (!w.addFeature(xy, 4)) {
            return false;
        }
    }
    return w.endLayer();
}

} // namespace

uint32_t generateScene(uint8_t* buf, uint32_t capacity, const SceneParams& p)
{
    SceneWriter w(buf, capacity);
    Lcg rng(p.seed);

    // Layer order IS draw order, and it lives in the format rather than in the
    // app: three apps and a desktop preview have to agree on what covers what.
    const bool ok =
        emitBlobs(w, rng, Slot::Landuse,   p.landusePolys, 380, 12) &&
        emitBlobs(w, rng, Slot::Wood,      p.woodPolys,    420, 14) &&
        emitBlobs(w, rng, Slot::Water,     p.waterPolys,   500, 16) &&
        emitBuildings(w, rng, p.buildings) &&
        emitLines(w, rng, Slot::Contour,   Kind::Polyline, p.contours,   p.pointsPerLine, 90) &&
        emitLines(w, rng, Slot::WaterDark, Kind::Polyline, p.waterLines, p.pointsPerLine, 140) &&
        emitLines(w, rng, Slot::RoadMinor, Kind::Polyline, p.roadMinor,  p.pointsPerLine, 60) &&
        emitLines(w, rng, Slot::Path,      Kind::Polyline, p.paths,      p.pointsPerLine, 110) &&
        emitLines(w, rng, Slot::RoadMajor, Kind::Polyline, p.roadMajor,  p.pointsPerLine, 40) &&
        w.finish();

    return ok ? w.size() : 0;
}

} // namespace MapLab
