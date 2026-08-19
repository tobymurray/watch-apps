/**
 ******************************************************************************
 * @file    BenchSuite.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Every number the vector-map decision needs, taken on the device.
 ******************************************************************************
 *
 * The suite is a list, not a loop: `run(i)` executes exactly one bench and
 * returns. The view drives it one per tick, and that is deliberate on three
 * counts.
 *
 *   - **A suite that ran to completion inside one call would block the GUI
 *     thread for the best part of a minute** -- which is the thing this app
 *     exists to find the limit of, not a thing it should do by accident.
 *   - Each bench is bounded (`Bench.hpp`'s 200 ms floor, one iteration for the
 *     slow ones), so the longest block the app takes is one bench.
 *   - A run interrupted by a USB connection -- which terminates every running
 *     app -- loses one bench rather than all of them, because every row is
 *     appended as it completes.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS MEASURED WHERE
 *
 * Everything except the blits runs here. `blitCopy` can only be called from
 * inside a TouchGFX `draw()`, so the two blit benches are marked `needsDraw()`
 * and the view times them in its own draw pass and hands the result back. That
 * split is not cosmetic: a blit measured anywhere else would not be going
 * through the real framebuffer path, and the whole point is that the canvas
 * architecture stands or falls on the real one.
 *
 * ---------------------------------------------------------------------------
 * THE WATCHDOG STAIRCASE IS NOT PART OF "RUN ALL"
 *
 * `W01` deliberately blocks the GUI thread for progressively longer, to find
 * where the app-liveness watchdog fires. The only prior evidence is anecdotal:
 * a ~10 s freeze from a 45 MB CRC scan was survived, and a 201 MB one
 * "tripped the app-liveness watchdog and force-restarted the watch"
 * (MapKit/README.md). A render budget argued from that is folklore with a
 * number attached.
 *
 * So it is its own menu entry, it announces itself, and every step is written
 * to the log *before* it is taken -- because the expected outcome of the last
 * step is that the device restarts and takes the unwritten result with it.
 ******************************************************************************
 */

#ifndef MAPLAB_BENCHSUITE_HPP
#define MAPLAB_BENCHSUITE_HPP

#include "Bench.hpp"
#include "BenchLog.hpp"
#include "Canvas.hpp"
#include "VecScene.hpp"

#include "SDK/Kernel/Kernel.hpp"

namespace MapLab
{

/// `Bench.hpp` wants something with nowMs(); the kernel has getTimeMs().
struct KernelClock {
    const SDK::Kernel& kernel;
    uint32_t nowMs() const { return kernel.sys.getTimeMs(); }
};

/**
 * @brief The buffers the suite works in, all owned by the app.
 *
 * Passed in rather than held, for the reason `MapKit`'s TileCache is
 * file-static in every app's Model.cpp: at 57,600 bytes for the canvas alone
 * this has to be the linker's problem at build time, not the heap's at run
 * time. A suite that allocated its own would move the decision to where it
 * cannot fail the build.
 */
struct BenchBuffers {
    uint8_t* canvas     = nullptr;   ///< 240 x 240 ABGR2222.
    uint8_t* scene      = nullptr;   ///< Encoded tile.
    uint32_t sceneCap   = 0;
    Pt*      scratch    = nullptr;   ///< Decoder's point buffer.
    int      scratchCap = 0;
    uint8_t* io         = nullptr;   ///< Filesystem read/write buffer.
    uint32_t ioCap      = 0;
};

class BenchSuite
{
public:
    BenchSuite(const SDK::Kernel& kernel, const BenchBuffers& buffers, BenchLog& log)
        : mKernel(kernel), mBuf(buffers), mLog(log), mClock{kernel} {}

    /// Benches in the ordinary suite. W01 is addressed separately.
    int count() const;
    const char* nameOf(int index) const;
    const char* idOf(int index) const;

    /// True for the benches the view has to time inside draw().
    bool needsDraw(int index) const;
    /// For a needsDraw bench: how many repeats the view should perform.
    uint32_t drawRepeats(int index) const;

    /// Run bench `index`, log it, and return the row. For a needsDraw bench
    /// this returns a row marked invalid; call reportDraw() instead.
    BenchRow run(int index);

    /// Hand back a blit measurement taken inside draw().
    BenchRow reportDraw(int index, uint32_t iterations, uint32_t elapsedMs,
                        int32_t bytesPerBlit);

    /// The app's first filesystem touch, measured before anything else opened
    /// a file. -1 until set.
    void setColdTouchMs(int32_t ms) { mColdTouchMs = ms; }

    // --- the watchdog staircase, one step per call -------------------------

    static constexpr int kStairSteps = 8;
    /// Milliseconds the given step blocks the GUI thread for.
    static uint32_t stairMs(int step);
    /// Logs its intent, blocks for stairMs(step) doing real render work, logs
    /// the outcome if the device is still alive to do so.
    BenchRow runStair(int step);

    /// Prepare the fixture file the I/O benches read. Costly (it writes a
    /// megabyte), so it is its own step and its own measurement.
    BenchRow prepareFixture();
    bool     fixtureReady() const { return mFixtureReady; }

    /// A generated tile is regenerated when the preset changes; the current
    /// one is what the cards draw.
    uint32_t buildScene(const SceneParams& p);
    uint32_t sceneBytes() const { return mSceneBytes; }

private:
    BenchRow renderBench(const char* id, const char* name, const SceneParams& p);
    BenchRow readBench(const char* id, const char* name, uint32_t chunk, bool randomSeek);
    BenchRow packBench(const char* id, const char* name, bool readTiles);

    const SDK::Kernel& mKernel;
    BenchBuffers       mBuf;
    BenchLog&          mLog;
    KernelClock        mClock;

    uint32_t mSceneBytes  = 0;
    int32_t  mColdTouchMs = -1;
    bool     mFixtureReady = false;
};

} // namespace MapLab

#endif // MAPLAB_BENCHSUITE_HPP
