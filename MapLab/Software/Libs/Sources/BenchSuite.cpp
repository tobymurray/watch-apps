#include "BenchSuite.hpp"

#include "Palette.hpp"
#include "SceneRender.hpp"

#include "SDK/RawTiles/Container.hpp"

#include <cstring>

namespace MapLab
{
namespace
{

/// The I/O fixture. One megabyte, in the app's own folder: big enough that a
/// 64 KiB read is a sixteenth of it rather than the whole thing, small enough
/// that creating it is seconds rather than minutes.
constexpr char     kFixturePath[]  = "maplab_io.bin";
constexpr uint32_t kFixtureBytes   = 1024u * 1024u;
constexpr uint32_t kFixtureChunk   = 16u * 1024u;

/// Where Map Manager's verified packs live. Sandbox-relative: absolute,
/// volume-prefixed paths never resolve from inside an app on hardware.
constexpr char kMapsDir[] = "../SharedData/maps";

struct BenchDef {
    const char* group;
    const char* id;
    const char* name;
    bool        draw;      ///< must be timed inside TouchGFX draw()
    uint32_t    repeats;   ///< for draw benches
};

/// The list, in run order. Micro-benches first so that a run interrupted early
/// still says something, and the pack benches last because they are the ones
/// that may be skipped for want of a pack.
constexpr BenchDef kBenches[] = {
    { "R", "R01", "clear",        false, 0 },
    { "R", "R02", "dots 3px",     false, 0 },
    { "R", "R03", "line 2px",     false, 0 },
    { "R", "R04", "poly fill",    false, 0 },
    { "R", "R05", "decode only",  false, 0 },
    { "R", "R06", "render rural", false, 0 },
    { "R", "R07", "render subrb", false, 0 },
    { "R", "R08", "render city",  false, 0 },
    { "R", "R09", "lut apply",    false, 0 },
    { "B", "B01", "blit 240sq",   true,  32 },
    { "B", "B02", "blit 256tile", true,  32 },
    { "I", "I01", "open+close",   false, 0 },
    { "I", "I02", "cold touch",   false, 0 },
    { "I", "I03", "read 256B",    false, 0 },
    { "I", "I04", "read 4K",      false, 0 },
    { "I", "I05", "read 16K",     false, 0 },
    { "I", "I06", "read 64K",     false, 0 },
    { "I", "I07", "seek+512B",    false, 0 },
    { "I", "I08", "dir scan",     false, 0 },
    { "I", "I09", "append 4K",    false, 0 },
    { "I", "I10", "pack open",    false, 0 },
    { "I", "I11", "pack tile",    false, 0 },
};

constexpr int kBenchCount = static_cast<int>(sizeof(kBenches) / sizeof(kBenches[0]));

/// A fixed polyline for the line and dot micro-benches: real geometry has
/// varied segment lengths and directions, and a benchmark drawn along one axis
/// would measure the cheapest case Bresenham has.
const Pt kMicroLine[] = {
    { 12, 20 }, { 60, 44 }, { 96, 30 }, { 140, 88 }, { 176, 64 },
    { 200, 120 }, { 168, 160 }, { 120, 148 }, { 78, 188 }, { 30, 160 },
};
constexpr int kMicroLineCount = static_cast<int>(sizeof(kMicroLine) / sizeof(kMicroLine[0]));

/// A blob with concave bits, so the scanline fill does more than two crossings
/// per row.
const Pt kMicroPoly[] = {
    { 40, 40 }, { 140, 30 }, { 120, 80 }, { 190, 70 },
    { 200, 150 }, { 120, 130 }, { 130, 190 }, { 60, 170 },
};
constexpr int kMicroPolyCount = static_cast<int>(sizeof(kMicroPoly) / sizeof(kMicroPoly[0]));

} // namespace

int BenchSuite::count() const { return kBenchCount; }

const char* BenchSuite::nameOf(int i) const
{
    return (i >= 0 && i < kBenchCount) ? kBenches[i].name : "?";
}

const char* BenchSuite::idOf(int i) const
{
    return (i >= 0 && i < kBenchCount) ? kBenches[i].id : "?";
}

bool BenchSuite::needsDraw(int i) const
{
    return (i >= 0 && i < kBenchCount) && kBenches[i].draw;
}

uint32_t BenchSuite::drawRepeats(int i) const
{
    return (i >= 0 && i < kBenchCount) ? kBenches[i].repeats : 0;
}

uint32_t BenchSuite::buildScene(const SceneParams& p)
{
    mSceneBytes = generateScene(mBuf.scene, mBuf.sceneCap, p);
    return mSceneBytes;
}

// ---------------------------------------------------------------------------
// Render benches
// ---------------------------------------------------------------------------

BenchRow BenchSuite::renderBench(const char* id, const char* name, const SceneParams& p)
{
    BenchRow row;
    row.group = "R";
    row.id    = id;
    row.name  = name;

    if (buildScene(p) == 0) {
        row.note = "scene-too-large";
        mLog.write(row);
        return row;
    }

    SceneReader scene;
    if (!scene.open(mBuf.scene, mSceneBytes)) {
        row.note = "scene-open-failed";
        mLog.write(row);
        return row;
    }

    Canvas canvas(mBuf.canvas, 240, 240);
    RenderStats last;
    const BenchResult r = measure(mClock, [&]() -> uint32_t {
        canvas.clear(code(Slot::Paper));
        last = renderScene(scene, canvas, mBuf.scratch, mBuf.scratchCap, 0, 0, 240);
        return last.points;
    });

    row.iterations = r.iterations;
    row.elapsedMs  = r.elapsedMs;
    row.usPerOp    = r.usPerOp;
    row.valid      = r.valid;
    row.a          = static_cast<int32_t>(last.features);
    row.b          = static_cast<int32_t>(last.points);
    row.c          = static_cast<int32_t>(mSceneBytes);
    // A render that dropped spans or clipped a feature drew less than it was
    // asked to, so its timing is not a measurement of the specified map.
    row.note       = (last.droppedSpans == 0 && last.clipped == 0) ? "" : "INCOMPLETE";
    mLog.write(row);
    return row;
}

// ---------------------------------------------------------------------------
// Filesystem benches
// ---------------------------------------------------------------------------

BenchRow BenchSuite::prepareFixture()
{
    BenchRow row;
    row.group = "I";
    row.id    = "I00";
    row.name  = "fixture write";

    if (mKernel.fs.exist(kFixturePath)) {
        SDK::Interface::IFileSystem::ObjectInfo info{};
        if (mKernel.fs.objectInfo(kFixturePath, info) && info.size >= kFixtureBytes) {
            mFixtureReady = true;
            row.note      = "already-present";
            row.a         = static_cast<int32_t>(info.size);
            mLog.write(row);
            return row;
        }
    }

    for (uint32_t i = 0; i < kFixtureChunk && i < mBuf.ioCap; ++i) {
        // Not zeros: a filesystem or a future compression step could get an
        // unrepresentative rate out of a megabyte of one byte value.
        mBuf.io[i] = static_cast<uint8_t>(i * 31u + 7u);
    }

    auto file = mKernel.fs.file(kFixturePath);
    if (!file || !file->open(true, true)) {
        row.note = "open-failed";
        mLog.write(row);
        return row;
    }

    const uint32_t t0    = mKernel.sys.getTimeMs();
    uint32_t       total = 0;
    bool           ok    = true;
    while (total < kFixtureBytes && ok) {
        size_t written = 0;
        ok = file->write(reinterpret_cast<const char*>(mBuf.io), kFixtureChunk, written);
        total += static_cast<uint32_t>(written);
        if (written == 0) {
            ok = false;
        }
    }
    file->flush();
    file->close();
    const uint32_t elapsed = mKernel.sys.getTimeMs() - t0;

    mFixtureReady  = ok && total >= kFixtureBytes;
    row.iterations = total / kFixtureChunk;
    row.elapsedMs  = elapsed;
    row.usPerOp    = (row.iterations > 0) ? (elapsed * 1000u) / row.iterations : 0;
    row.valid      = elapsed > 0;
    row.a          = static_cast<int32_t>(total);
    row.b          = static_cast<int32_t>(kFixtureChunk);
    row.note       = mFixtureReady ? "" : "write-failed";
    mLog.write(row);
    return row;
}

BenchRow BenchSuite::readBench(const char* id, const char* name,
                               uint32_t chunk, bool randomSeek)
{
    BenchRow row;
    row.group = "I";
    row.id    = id;
    row.name  = name;
    row.b     = static_cast<int32_t>(chunk);

    if (!mFixtureReady) {
        row.note = "no-fixture";
        mLog.write(row);
        return row;
    }
    if (chunk > mBuf.ioCap) {
        row.note = "buffer-too-small";
        mLog.write(row);
        return row;
    }

    auto file = mKernel.fs.file(kFixturePath);
    if (!file || !file->open(false, false)) {
        row.note = "open-failed";
        mLog.write(row);
        return row;
    }

    // The file is opened once and the reads are timed: an open per read would
    // measure the open, which is I01's job. This is the loop a renderer runs
    // -- one handle, many seeks -- and the layer directory in VecScene.hpp is
    // the reason the random-seek variant matters as much as the sequential one.
    uint32_t offset = 0;
    uint32_t seed   = 12345u;
    const BenchResult r = measure(mClock, [&]() -> uint32_t {
        if (randomSeek) {
            seed   = seed * 1664525u + 1013904223u;
            offset = (seed % (kFixtureBytes / chunk)) * chunk;
        } else {
            offset += chunk;
            if (offset + chunk > kFixtureBytes) {
                offset = 0;
            }
        }
        if (!file->seek(offset)) {
            return 0;
        }
        size_t got = 0;
        if (!file->read(reinterpret_cast<char*>(mBuf.io), chunk, got)) {
            return 0;
        }
        return static_cast<uint32_t>(got) + mBuf.io[0];
    }, kDefaultFloorMs);
    file->close();

    row.iterations = r.iterations;
    row.elapsedMs  = r.elapsedMs;
    row.usPerOp    = r.usPerOp;
    row.valid      = r.valid;
    // KB/s, which is the number every other measurement of this device is
    // quoted in (Map Manager's 2.9 MB/s verification pass, the 6-9 ms 64 KiB
    // tile read), so they can be compared without arithmetic.
    row.a = (r.usPerOp > 0) ? static_cast<int32_t>((static_cast<uint64_t>(chunk) * 1000000u)
                                                   / (r.usPerOp * 1024u))
                            : -1;
    row.c = randomSeek ? 1 : 0;
    mLog.write(row);
    return row;
}

BenchRow BenchSuite::packBench(const char* id, const char* name, bool readTiles)
{
    BenchRow row;
    row.group = "I";
    row.id    = id;
    row.name  = name;

    // Find the first .rawtiles pack in the shared directory. There may be
    // none, and that is an ordinary outcome rather than a failure: this bench
    // exists to tie MapLab's numbers to the raster path's already-measured
    // ones, and a watch with no pack simply cannot contribute that.
    char packPath[SDK::Interface::IFileSystem::skMaxPathLen];
    packPath[0] = '\0';
    {
        auto dir = mKernel.fs.dir(kMapsDir);
        if (dir && dir->open()) {
            SDK::Interface::IFileSystem::ObjectInfo info{};
            while (dir->readNext(info)) {
                if (info.isDir) {
                    continue;
                }
                const size_t len = std::strlen(info.name);
                if (len > 9 && std::strcmp(info.name + len - 9, ".rawtiles") == 0) {
                    // Refuse a path that would not fit rather than truncate it:
                    // a truncated path names a different file, and PackCatalog
                    // carries a test pinning the same behaviour for the same
                    // reason. Skipping means this pack is not benched; opening
                    // the wrong path would mean benching the wrong thing.
                    const size_t dirLen = std::strlen(kMapsDir);
                    if (dirLen + 1 + len + 1 > sizeof(packPath)) {
                        continue;
                    }
                    // Built by hand rather than with snprintf: the compiler
                    // cannot prove the bound on a 255-byte directory entry and
                    // warns, and silencing that warning by widening the buffer
                    // would be hiding the question rather than answering it.
                    std::memcpy(packPath, kMapsDir, dirLen);
                    packPath[dirLen] = '/';
                    std::memcpy(packPath + dirLen + 1, info.name, len + 1);
                    break;
                }
            }
            dir->close();
        }
    }
    if (packPath[0] == '\0') {
        row.note = "no-pack";
        mLog.write(row);
        return row;
    }

    if (!readTiles) {
        // Structural open only, with skipCrcVerify -- the same call the map
        // apps make on the GUI thread, for the same reason: a mandatory CRC
        // pass froze the GUI for ~10 s at 45 MB.
        SDK::RawTiles::Container c;
        const uint32_t t0 = mKernel.sys.getTimeMs();
        const auto res = c.openFromFile(mKernel.fs, packPath, true);
        const uint32_t elapsed = mKernel.sys.getTimeMs() - t0;

        row.iterations = 1;
        row.elapsedMs  = elapsed;
        row.usPerOp    = elapsed * 1000u;
        row.valid      = true;
        row.a          = static_cast<int32_t>(res == SDK::RawTiles::OpenResult::Ok ? 1 : 0);
        row.b          = c.isOpen() ? static_cast<int32_t>(c.header().tileCount) : -1;
        row.c          = c.isOpen() ? static_cast<int32_t>(c.header().tileDimPx) : -1;
        row.note       = (res == SDK::RawTiles::OpenResult::Ok) ? "" : "open-failed";
        mLog.write(row);
        return row;
    }

    SDK::RawTiles::Container c;
    if (c.openFromFile(mKernel.fs, packPath, true) != SDK::RawTiles::OpenResult::Ok) {
        row.note = "open-failed";
        mLog.write(row);
        return row;
    }
    const uint32_t decoded = c.decodedTileSize();
    if (decoded == 0 || decoded > mBuf.ioCap) {
        row.note = "tile-too-large";
        row.a    = static_cast<int32_t>(decoded);
        mLog.write(row);
        return row;
    }

    // Walk the pack's own index rather than guessing coordinates: a tile that
    // is not there reads in no time at all and would flatter the number.
    const SDK::RawTiles::Header& h = c.header();
    uint32_t found = 0;
    const BenchResult r = measure(mClock, [&]() -> uint32_t {
        const SDK::RawTiles::TileInfo info =
            c.tileAtIndex(found % (h.tileCount ? h.tileCount : 1u));
        ++found;
        if (!info.valid()) {
            return 0;
        }
        if (c.readTile(info, mBuf.io, decoded) != SDK::RawTiles::ReadResult::Ok) {
            return 0;
        }
        return mBuf.io[0] + 1u;
    }, kDefaultFloorMs);

    row.iterations = r.iterations;
    row.elapsedMs  = r.elapsedMs;
    row.usPerOp    = r.usPerOp;
    row.valid      = r.valid;
    row.a          = static_cast<int32_t>(decoded);
    row.b          = static_cast<int32_t>(h.tileCount);
    row.c          = static_cast<int32_t>(h.tileDimPx);
    mLog.write(row);
    return row;
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

BenchRow BenchSuite::run(int index)
{
    BenchRow row;
    if (index < 0 || index >= kBenchCount) {
        return row;
    }
    const BenchDef& def = kBenches[index];
    row.group = def.group;
    row.id    = def.id;
    row.name  = def.name;

    if (def.draw) {
        row.note = "deferred-to-draw";
        return row; // the view times this one; nothing logged here
    }

    Canvas canvas(mBuf.canvas, 240, 240);

    if (std::strcmp(def.id, "R01") == 0) {
        const BenchResult r = measure(mClock, [&]() -> uint32_t {
            canvas.clear(code(Slot::Paper));
            return canvas.pixels()[0];
        });
        row.iterations = r.iterations; row.elapsedMs = r.elapsedMs;
        row.usPerOp = r.usPerOp; row.valid = r.valid;
        row.a = static_cast<int32_t>(canvas.byteCount());
        mLog.write(row);
        return row;
    }

    if (std::strcmp(def.id, "R02") == 0) {
        // A thousand 3 px stamps: the GPS trace's own primitive, so this
        // number is directly comparable with what the shipped map apps pay.
        const BenchResult r = measure(mClock, [&]() -> uint32_t {
            for (int i = 0; i < 1000; ++i) {
                canvas.fillRect(static_cast<int16_t>(i % 236), static_cast<int16_t>((i * 7) % 236),
                                3, 3, code(Slot::Trace));
            }
            return 1000;
        });
        row.iterations = r.iterations; row.elapsedMs = r.elapsedMs;
        row.usPerOp = r.usPerOp; row.valid = r.valid;
        row.a = 1000;
        mLog.write(row);
        return row;
    }

    if (std::strcmp(def.id, "R03") == 0) {
        const BenchResult r = measure(mClock, [&]() -> uint32_t {
            canvas.polyline(kMicroLine, kMicroLineCount, 2, code(Slot::RoadMinor));
            return kMicroLineCount;
        });
        row.iterations = r.iterations; row.elapsedMs = r.elapsedMs;
        row.usPerOp = r.usPerOp; row.valid = r.valid;
        row.a = kMicroLineCount - 1;   // segments
        mLog.write(row);
        return row;
    }

    if (std::strcmp(def.id, "R04") == 0) {
        canvas.resetDropped();
        const BenchResult r = measure(mClock, [&]() -> uint32_t {
            canvas.fillPolygon(kMicroPoly, kMicroPolyCount, code(Slot::Water));
            return kMicroPolyCount;
        });
        row.iterations = r.iterations; row.elapsedMs = r.elapsedMs;
        row.usPerOp = r.usPerOp; row.valid = r.valid;
        row.a = kMicroPolyCount;
        row.b = static_cast<int32_t>(canvas.dropped());
        row.note = (canvas.dropped() == 0) ? "" : "INCOMPLETE";
        mLog.write(row);
        return row;
    }

    if (std::strcmp(def.id, "R05") == 0) {
        // Decode and transform with no drawing at all. The difference between
        // this and R08 is what the rasteriser costs, and the ratio decides
        // whether a faster wire format would buy anything worth having.
        if (buildScene(SceneParams::cityCentre()) == 0) {
            row.note = "scene-too-large";
            mLog.write(row);
            return row;
        }
        SceneReader scene;
        if (!scene.open(mBuf.scene, mSceneBytes)) {
            row.note = "scene-open-failed";
            mLog.write(row);
            return row;
        }
        uint32_t points = 0;
        const BenchResult r = measure(mClock, [&]() -> uint32_t {
            uint32_t n = 0;
            for (int i = 0; i < scene.layerCount(); ++i) {
                scene.forEachFeature(i, mBuf.scratch, mBuf.scratchCap, 0, 0, 240,
                                     [&](const Pt* pts, int cnt) {
                                         n += static_cast<uint32_t>(cnt);
                                         if (cnt > 0) {
                                             n += static_cast<uint32_t>(pts[0].x & 1);
                                         }
                                     });
            }
            points = n;
            return n;
        });
        row.iterations = r.iterations; row.elapsedMs = r.elapsedMs;
        row.usPerOp = r.usPerOp; row.valid = r.valid;
        row.a = static_cast<int32_t>(points);
        row.c = static_cast<int32_t>(mSceneBytes);
        mLog.write(row);
        return row;
    }

    if (std::strcmp(def.id, "R06") == 0) { return renderBench("R06", def.name, SceneParams::rural()); }
    if (std::strcmp(def.id, "R07") == 0) { return renderBench("R07", def.name, SceneParams::suburban()); }
    if (std::strcmp(def.id, "R08") == 0) { return renderBench("R08", def.name, SceneParams::cityCentre()); }

    if (std::strcmp(def.id, "R09") == 0) {
        // X7 from MAP_CARTOGRAPHY_SPEC.md § 9: the per-frame cost of a restyle
        // LUT, which has never been measured on this hardware.
        uint8_t lut[kLutEntries];
        buildLut(Variant::Night, lut);
        const BenchResult r = measure(mClock, [&]() -> uint32_t {
            canvas.applyLut(lut);
            return canvas.pixels()[0];
        });
        row.iterations = r.iterations; row.elapsedMs = r.elapsedMs;
        row.usPerOp = r.usPerOp; row.valid = r.valid;
        row.a = static_cast<int32_t>(canvas.byteCount());
        mLog.write(row);
        return row;
    }

    if (std::strcmp(def.id, "I01") == 0) {
        const BenchResult r = measure(mClock, [&]() -> uint32_t {
            auto f = mKernel.fs.file(kFixturePath);
            if (!f || !f->open(false, false)) {
                return 0;
            }
            const uint32_t sz = static_cast<uint32_t>(f->size());
            f->close();
            return sz;
        }, kDefaultFloorMs);
        row.iterations = r.iterations; row.elapsedMs = r.elapsedMs;
        row.usPerOp = r.usPerOp; row.valid = r.valid;
        mLog.write(row);
        return row;
    }

    if (std::strcmp(def.id, "I02") == 0) {
        // Not measured here: measured once, before this app opened anything.
        // Re-measuring after a run of I/O benches would report a warm number
        // and call it cold.
        row.iterations = 1;
        row.elapsedMs  = (mColdTouchMs >= 0) ? static_cast<uint32_t>(mColdTouchMs) : 0;
        row.usPerOp    = row.elapsedMs * 1000u;
        row.valid      = mColdTouchMs >= 0;
        row.a          = mColdTouchMs;
        row.note       = (mColdTouchMs >= 0) ? "first-touch-of-launch" : "not-captured";
        mLog.write(row);
        return row;
    }

    if (std::strcmp(def.id, "I03") == 0) { return readBench("I03", def.name, 256,        false); }
    if (std::strcmp(def.id, "I04") == 0) { return readBench("I04", def.name, 4  * 1024,  false); }
    if (std::strcmp(def.id, "I05") == 0) { return readBench("I05", def.name, 16 * 1024,  false); }
    if (std::strcmp(def.id, "I06") == 0) { return readBench("I06", def.name, 64 * 1024,  false); }
    if (std::strcmp(def.id, "I07") == 0) { return readBench("I07", def.name, 512,        true);  }

    if (std::strcmp(def.id, "I08") == 0) {
        uint32_t entries = 0;
        const BenchResult r = measure(mClock, [&]() -> uint32_t {
            auto dir = mKernel.fs.dir(kMapsDir);
            if (!dir || !dir->open()) {
                return 0;
            }
            uint32_t n = 0;
            SDK::Interface::IFileSystem::ObjectInfo info{};
            while (dir->readNext(info)) {
                ++n;
            }
            dir->close();
            entries = n;
            return n + 1;
        }, kDefaultFloorMs);
        row.iterations = r.iterations; row.elapsedMs = r.elapsedMs;
        row.usPerOp = r.usPerOp; row.valid = r.valid;
        row.a = static_cast<int32_t>(entries);
        mLog.write(row);
        return row;
    }

    if (std::strcmp(def.id, "I09") == 0) {
        constexpr uint32_t kChunk = 4096;
        auto f = mKernel.fs.file("maplab_scratch.bin");
        if (!f || !f->open(true, true)) {
            row.note = "open-failed";
            mLog.write(row);
            return row;
        }
        const BenchResult r = measure(mClock, [&]() -> uint32_t {
            size_t written = 0;
            f->write(reinterpret_cast<const char*>(mBuf.io), kChunk, written);
            return static_cast<uint32_t>(written);
        }, kDefaultFloorMs);
        f->flush();
        f->close();
        mKernel.fs.remove("maplab_scratch.bin");
        row.iterations = r.iterations; row.elapsedMs = r.elapsedMs;
        row.usPerOp = r.usPerOp; row.valid = r.valid;
        row.a = (r.usPerOp > 0) ? static_cast<int32_t>((static_cast<uint64_t>(kChunk) * 1000000u)
                                                       / (r.usPerOp * 1024u)) : -1;
        row.b = static_cast<int32_t>(kChunk);
        mLog.write(row);
        return row;
    }

    if (std::strcmp(def.id, "I10") == 0) { return packBench("I10", def.name, false); }
    if (std::strcmp(def.id, "I11") == 0) { return packBench("I11", def.name, true);  }

    row.note = "unimplemented";
    mLog.write(row);
    return row;
}

BenchRow BenchSuite::reportDraw(int index, uint32_t iterations, uint32_t elapsedMs,
                                int32_t bytesPerBlit)
{
    BenchRow row;
    if (index < 0 || index >= kBenchCount) {
        return row;
    }
    row.group      = kBenches[index].group;
    row.id         = kBenches[index].id;
    row.name       = kBenches[index].name;
    row.iterations = iterations;
    row.elapsedMs  = elapsedMs;
    row.usPerOp    = (iterations > 0) ? static_cast<uint32_t>(
                         (static_cast<uint64_t>(elapsedMs) * 1000u + iterations / 2) / iterations)
                                      : 0;
    row.valid      = elapsedMs > 0;
    row.a          = bytesPerBlit;
    // The number the canvas architecture actually turns on: how much of a
    // 100 ms frame budget one full-screen blit spends before any geometry has
    // been decoded.
    row.b          = (row.usPerOp > 0 && bytesPerBlit > 0)
                         ? static_cast<int32_t>((static_cast<uint64_t>(bytesPerBlit) * 1000000u)
                                                / (static_cast<uint64_t>(row.usPerOp) * 1024u))
                         : -1;
    mLog.write(row);
    return row;
}

// ---------------------------------------------------------------------------
// The watchdog staircase
// ---------------------------------------------------------------------------

uint32_t BenchSuite::stairMs(int step)
{
    static const uint32_t kStair[kStairSteps] = { 100, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    if (step < 0) {
        return kStair[0];
    }
    return (step < kStairSteps) ? kStair[step] : kStair[kStairSteps - 1];
}

BenchRow BenchSuite::runStair(int step)
{
    const uint32_t target = stairMs(step);

    BenchRow intent;
    intent.group = "W";
    intent.id    = "W01";
    intent.name  = "block";
    intent.a     = static_cast<int32_t>(target);
    intent.note  = "about-to-block";
    // Written and flushed *before* the block. If the watchdog fires, this row
    // is the whole finding.
    mLog.write(intent);

    Canvas canvas(mBuf.canvas, 240, 240);
    const uint32_t t0 = mKernel.sys.getTimeMs();
    uint32_t passes = 0;
    // Real work rather than a spin: a kernel that only notices a blocked app
    // when it stops asking for anything would give a different answer to an
    // empty loop, and the case that matters is a render that overran.
    while ((mKernel.sys.getTimeMs() - t0) < target) {
        canvas.clear(code(Slot::Paper));
        canvas.polyline(kMicroLine, kMicroLineCount, 2, code(Slot::RoadMinor));
        ++passes;
    }
    const uint32_t elapsed = mKernel.sys.getTimeMs() - t0;

    BenchRow row;
    row.group      = "W";
    row.id         = "W01";
    row.name       = "block";
    row.iterations = passes;
    row.elapsedMs  = elapsed;
    row.usPerOp    = (passes > 0) ? (elapsed * 1000u) / passes : 0;
    row.valid      = true;
    row.a          = static_cast<int32_t>(target);
    row.b          = static_cast<int32_t>(step);
    row.note       = "survived";
    mLog.write(row);
    return row;
}

} // namespace MapLab
