#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <gui/common/FrontendApplication.hpp>

#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Port/TouchGFX/TouchGFXCommandProcessor.hpp"

#include <cstdio>
#include <cstring>

#define LOG_MODULE_PRX      "Model"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

using namespace MapLab;

namespace
{

// ---------------------------------------------------------------------------
// The buffers. File-static, so the linker arbitrates them -- see Model.hpp.
//
//   canvas   57,600 B  240 x 240 ABGR2222. The candidate architecture: render
//                      the whole viewport once per fix, blit it as one bitmap.
//   tile     65,536 B  a 256 x 256 ABGR2222 raster tile, and the filesystem
//                      read buffer. Shared deliberately: they are the same
//                      64 KiB and never live at once, and a lab app that
//                      claimed 128 KiB for two things it uses alternately
//                      would misrepresent what a renderer needs.
//   scene    24,576 B  one encoded vector tile. The densest generated preset
//                      measured 16,787 B on the host, so this is headroom
//                      rather than a limit being tested.
//   scratch   2,048 B  the decoder's point buffer, kMaxPointsPerFeature deep.
//
// 149,760 B in total, and every byte of it is reported by the build's own map
// file. That total is this app's, not a renderer's: a renderer needs the
// canvas, the scene buffer and the scratch (84,224 B), and reads tiles into a
// buffer it can size to the format's per-tile cap.
// ---------------------------------------------------------------------------
constexpr uint32_t kCanvasBytes = 240u * 240u;
constexpr uint32_t kTileBytes   = 256u * 256u;
constexpr uint32_t kSceneBytes  = 24u * 1024u;

uint8_t      gCanvas[kCanvasBytes];
uint8_t      gTileAndIo[kTileBytes];
uint8_t      gScene[kSceneBytes];
MapLab::Pt   gScratch[MapLab::kMaxPointsPerFeature];

BenchBuffers makeBuffers()
{
    BenchBuffers b;
    b.canvas     = gCanvas;
    b.scene      = gScene;
    b.sceneCap   = kSceneBytes;
    b.scratch    = gScratch;
    b.scratchCap = MapLab::kMaxPointsPerFeature;
    b.io         = gTileAndIo;
    b.ioCap      = kTileBytes;
    return b;
}

#ifdef BUILD_VERSION_STR
constexpr const char *kBuildVersion = BUILD_VERSION_STR;
#else
constexpr const char *kBuildVersion = "dev";
#endif

void copyInto(char *dst, size_t cap, const char *src)
{
    if (cap == 0) {
        return;
    }
    std::snprintf(dst, cap, "%s", (src != nullptr) ? src : "");
}

} // namespace

Model::Model()
    : mKernel(SDK::KernelProviderGUI::GetInstance().getKernel())
    , mCanvas(gCanvas, 240, 240)
    , mLog(mKernel)
    , mSuite(mKernel, makeBuffers(), mLog)
{
    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(this);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(this);

    setCapabilities();

    // Before anything else opens a file, including the log. "The first
    // filesystem touch after app start costs ~113 ms once, ~4 ms after" is a
    // measurement from the rawtiles device proof, and it is load-bearing for
    // the renderer's first frame -- so it is re-taken here on 1.4 firmware,
    // once, and never again in this launch.
    mStatus.coldTouchMs = measureColdTouch(mKernel);
    mSuite.setColdTouchMs(mStatus.coldTouchMs);
    mStatus.benchTotal  = mSuite.count();

    // A scene exists from the start so the card view has something to draw
    // before any bench has run.
    mSuite.buildScene(SceneParams::suburban());
}

Model::~Model()
{
    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(nullptr);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(nullptr);
}

int32_t Model::measureColdTouch(const SDK::Kernel &kernel)
{
    const uint32_t t0 = kernel.sys.getTimeMs();
    auto file = kernel.fs.file("maplab_cold.tmp");
    if (!file) {
        return -1;
    }
    // Creating rather than reading: a read of a file that does not exist can
    // fail early and cheaply, which would time the failure rather than the
    // filesystem waking up.
    if (!file->open(true, true)) {
        return -1;
    }
    size_t written = 0;
    const char byte = 'x';
    file->write(&byte, 1, written);
    file->flush();
    file->close();
    const int32_t ms = static_cast<int32_t>(kernel.sys.getTimeMs() - t0);
    kernel.fs.remove("maplab_cold.tmp");
    return ms;
}

FrontendApplication &Model::application()
{
    return *static_cast<FrontendApplication *>(touchgfx::Application::getInstance());
}

const uint8_t *Model::canvasPixels() const { return gCanvas; }
const uint8_t *Model::tileBytes()    const { return gTileAndIo; }

void Model::setCapabilities()
{
    auto *msg = mKernel.comm.allocateMessage<SDK::Message::RequestSetCapabilities>();
    if (msg) {
        msg->enUsbChargingScreen = true;
        msg->enPhoneNotification = false;
        msg->enMusicControl      = false;
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}

void Model::tick()
{
    if (mStatus.mode == Mode::Running && mPendingBlit < 0 && !mStatus.complete) {
        stepSuite();
    }
    if (mInvalidate) {
        mInvalidate = false;
        application().invalidate();
    }
}

void Model::stepSuite()
{
    // Index -1 is the fixture: a megabyte written once, which every read bench
    // afterwards depends on and which is itself the write-throughput number.
    if (mStatus.benchIndex < 0) {
        const BenchRow row = mSuite.prepareFixture();
        copyInto(mStatus.lastId,   sizeof(mStatus.lastId),   row.id);
        copyInto(mStatus.lastName, sizeof(mStatus.lastName), row.name);
        copyInto(mStatus.lastNote, sizeof(mStatus.lastNote), row.note);
        mStatus.lastUsPerOp = row.usPerOp;
        mStatus.lastValid   = row.valid;
        mStatus.benchIndex  = 0;
        publish();
        return;
    }

    if (mStatus.benchIndex >= mSuite.count()) {
        mStatus.complete = true;
        publish();
        return;
    }

    const int i = mStatus.benchIndex;
    if (mSuite.needsDraw(i)) {
        // Handed to the view: blitCopy is only legal inside draw(), and a blit
        // timed anywhere else would not be going through the framebuffer path
        // the whole canvas architecture rests on.
        mPendingBlit = i;
        publish();
        return;
    }

    const BenchRow row = mSuite.run(i);
    copyInto(mStatus.lastId,   sizeof(mStatus.lastId),   row.id);
    copyInto(mStatus.lastName, sizeof(mStatus.lastName), row.name);
    copyInto(mStatus.lastNote, sizeof(mStatus.lastNote), row.note);
    mStatus.lastUsPerOp = row.usPerOp;
    mStatus.lastValid   = row.valid;
    mStatus.benchIndex  = i + 1;
    mStatus.logRows     = mLog.rowsWritten();
    mStatus.logFailures = mLog.failures();
    publish();
}

bool Model::blitPending(int &benchIndex, uint32_t &repeats, const uint8_t *&source,
                        int16_t &srcW, int16_t &srcH, bool &mosaic)
{
    if (mPendingBlit < 0) {
        return false;
    }
    benchIndex = mPendingBlit;
    repeats    = mSuite.drawRepeats(mPendingBlit);

    const char *id = mSuite.idOf(mPendingBlit);
    if (std::strcmp(id, "B02") == 0) {
        // The raster baseline. The tile's contents do not change what a blit
        // costs, but they do decide whether the screenshot of this bench is
        // legible, so it carries a coarse pattern rather than whatever the
        // last filesystem read left behind.
        for (uint32_t i = 0; i < kTileBytes; ++i) {
            const uint32_t x = i % 256u;
            const uint32_t y = i / 256u;
            gTileAndIo[i] = static_cast<uint8_t>(0xC0 | (((x >> 5) + (y >> 5)) & 0x3F));
        }
        source = gTileAndIo;
        srcW   = 256;
        srcH   = 256;
        mosaic = true;
        return true;
    }

    // B01: a real rendered viewport, so what is timed is what would be shown.
    drawCard(Card::SceneNative, mCanvas, gScene, mSuite.sceneBytes(), gScratch,
             MapLab::kMaxPointsPerFeature);
    source = gCanvas;
    srcW   = 240;
    srcH   = 240;
    mosaic = false;
    return true;
}

void Model::blitComplete(int benchIndex, uint32_t iterations, uint32_t elapsedMs,
                         int32_t bytesPerBlit)
{
    const BenchRow row = mSuite.reportDraw(benchIndex, iterations, elapsedMs, bytesPerBlit);
    copyInto(mStatus.lastId,   sizeof(mStatus.lastId),   row.id);
    copyInto(mStatus.lastName, sizeof(mStatus.lastName), row.name);
    copyInto(mStatus.lastNote, sizeof(mStatus.lastNote), row.note);
    mStatus.lastUsPerOp = row.usPerOp;
    mStatus.lastValid   = row.valid;
    mStatus.benchIndex  = benchIndex + 1;
    mStatus.logRows     = mLog.rowsWritten();
    mStatus.logFailures = mLog.failures();
    mPendingBlit        = -1;
    publish();
}

void Model::drawCurrentCard()
{
    const Card c = static_cast<Card>(mStatus.card);
    drawCard(c, mCanvas, gScene, mSuite.sceneBytes(), gScratch, MapLab::kMaxPointsPerFeature);
    mInvalidate = true;
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

void Model::up()
{
    switch (mStatus.mode) {
        case Mode::Menu:
            if (mStatus.menuIndex > 0) {
                --mStatus.menuIndex;
            }
            break;
        case Mode::Cards:
            // Wraps, unlike Map Manager's pack list: there are twelve cards
            // and they are looked at in a loop, not scanned for one entry.
            mStatus.card = static_cast<uint8_t>(
                (mStatus.card + static_cast<uint8_t>(Card::Count) - 1) %
                static_cast<uint8_t>(Card::Count));
            drawCurrentCard();
            break;
        case Mode::Stair:
            if (mStatus.stairStep > 0) {
                --mStatus.stairStep;
                mStatus.stairMs = BenchSuite::stairMs(mStatus.stairStep);
            }
            break;
        default:
            break;
    }
    publish();
}

void Model::down()
{
    switch (mStatus.mode) {
        case Mode::Menu:
            if (mStatus.menuIndex + 1 < static_cast<uint8_t>(MenuItem::Count)) {
                ++mStatus.menuIndex;
            }
            break;
        case Mode::Cards:
            mStatus.card = static_cast<uint8_t>((mStatus.card + 1) %
                                                static_cast<uint8_t>(Card::Count));
            drawCurrentCard();
            break;
        case Mode::Stair:
            if (mStatus.stairStep + 1 < BenchSuite::kStairSteps) {
                ++mStatus.stairStep;
                mStatus.stairMs = BenchSuite::stairMs(mStatus.stairStep);
            }
            break;
        default:
            break;
    }
    publish();
}

void Model::select()
{
    switch (mStatus.mode) {
        case Mode::Menu:
            switch (static_cast<MenuItem>(mStatus.menuIndex)) {
                case MenuItem::RunAll:
                    mStatus.runIndex   = mLog.beginRun(kBuildVersion, "suite");
                    mStatus.mode       = Mode::Running;
                    mStatus.benchIndex = -1;   // fixture first
                    mStatus.complete   = false;
                    mRunStarted        = true;
                    break;
                case MenuItem::Cards:
                    mStatus.mode = Mode::Cards;
                    mStatus.card = 0;
                    drawCurrentCard();
                    break;
                case MenuItem::Stair:
                    mStatus.runIndex    = mLog.beginRun(kBuildVersion, "watchdog");
                    mStatus.mode        = Mode::Stair;
                    mStatus.stairStep   = 0;
                    mStatus.stairMs     = BenchSuite::stairMs(0);
                    mStatus.stairArmed  = true;
                    break;
                case MenuItem::Exit:
                default:
                    exitApp();
                    return;
            }
            break;

        case Mode::Cards:
            down();
            return;

        case Mode::Stair: {
            // Deliberate, one step at a time, never a sequence: the expected
            // outcome of a late step is that the device restarts, and a loop
            // would take the remaining steps with it.
            const BenchRow row = mSuite.runStair(mStatus.stairStep);
            copyInto(mStatus.lastId,   sizeof(mStatus.lastId),   row.id);
            copyInto(mStatus.lastName, sizeof(mStatus.lastName), row.name);
            copyInto(mStatus.lastNote, sizeof(mStatus.lastNote), row.note);
            mStatus.lastUsPerOp = row.elapsedMs * 1000u;
            mStatus.lastValid   = row.valid;
            mStatus.logRows     = mLog.rowsWritten();
            if (mStatus.stairStep + 1 < BenchSuite::kStairSteps) {
                ++mStatus.stairStep;
                mStatus.stairMs = BenchSuite::stairMs(mStatus.stairStep);
            }
            break;
        }

        default:
            break;
    }
    publish();
}

void Model::back()
{
    if (mStatus.mode == Mode::Menu) {
        exitApp();
        return;
    }
    mStatus.mode = Mode::Menu;
    publish();
}

void Model::exitApp()
{
    LOG_INFO("Leaving MapLab; %lu rows written this launch\n",
             static_cast<unsigned long>(mLog.rowsWritten()));

    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(nullptr);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(nullptr);

    mKernel.sys.exit();
}

void Model::publish()
{
    mInvalidate = true;
    if (modelListener != nullptr) {
        modelListener->onStatusChanged(mStatus);
    }
}

// --- IGuiLifeCycleCallback --------------------------------------------------

void Model::onStart()
{
    LOG_INFO("MapLab started; build %s\n", kBuildVersion);
    publish();
}

void Model::onResume()  { publish(); }
void Model::onSuspend() {}
void Model::onStop()    {}

// --- ICustomMessageHandler --------------------------------------------------

bool Model::customMessageHandler(SDK::MessageBase *msg)
{
    // The Service sends nothing: every measurement here is taken on the GUI
    // thread because that is where a renderer lives. See Commands.hpp.
    (void)msg;
    return false;
}
