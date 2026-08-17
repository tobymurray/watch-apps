/**
 ******************************************************************************
 * @file    FlashDumper.hpp
 * @brief   Resumable, self-verifying dump of a memory region into chunk files.
 ******************************************************************************
 *
 * The app's whole job. A port of the chunked-dump routine in
 * `service-cpp-instrumentation-sweep7.cpp` (una-sdk@research -- see the
 * README), which produced the one 4 MB dump that has been verified end to end,
 * with three things added that a one-off instrumentation hack did not need:
 * it yields instead of blocking, it resumes instead of restarting, and it
 * reports what it is doing instead of only logging it.
 *
 *
 * ## Reading memory as a file
 *
 * Internal flash is memory-mapped, so "reading" it is a pointer dereference
 * and there is no read call that can fail or come up short -- which is why
 * there is no read-error state below. That is not the same as saying reads
 * cannot fail: an address that does not decode raises a BusFault, and with no
 * handler of our own that escalates to a HardFault and takes the app down.
 * There is no in-app error path for it and there cannot be one; the symptom is
 * the app dying mid-dump, and the manifest's last complete line is what says
 * where. For the default flash region this is moot (flash is a confident base,
 * already read whole once), and it is the reason a non-default region from a
 * config file is worth being careful with.
 *
 * The window pointer, not a raw address, is what this class dereferences.
 * On the watch it is `reinterpret_cast<const uint8_t*>(region.base)` -- the
 * identity mapping. On the host it points at a synthetic buffer, which is what
 * makes the chunk/CRC/manifest/resume logic testable at all: everything below
 * except that one cast is platform-independent. The manifest still records the
 * real `region.base`, so a host-generated manifest is shaped exactly like a
 * device one.
 *
 *
 * ## One pass, in order, hashing each byte once
 *
 * The whole-image CRC is a single running value chained across every chunk in
 * ascending order, exactly as the reference implementation computed it. That
 * ordering constraint is what shapes the loop: chunks cannot be skipped for
 * hashing purposes even when their file is already on disk, or the running
 * value would be computed over the wrong byte sequence.
 *
 * So every chunk is always read from memory and hashed. What resume saves is
 * the *write*, which is the expensive half on this storage:
 *
 *   - A chunk whose file is absent, or the wrong size, is written.
 *   - A chunk whose file is the right size is read back and hashed while the
 *     memory is hashed, and if the two agree the write is skipped. That is a
 *     stronger check than trusting the size: it proves what is on disk is what
 *     is in memory, which is the only thing that makes skipping it honest.
 *   - A chunk that fails that comparison is rewritten. Its CRCs are already
 *     computed by then, so the rewrite deliberately does not hash again --
 *     see Phase::Rewriting.
 *
 * The consequence worth knowing: a resumed run produces a *complete* manifest
 * rather than one patched from a previous run's lines, because every line is
 * regenerated from a fresh read. There is no state carried between runs except
 * the chunk files themselves.
 *
 *
 * ## Yielding
 *
 * step() does a bounded amount of work and returns, so the service can service
 * messages between slices. Nothing here blocks and nothing recurses; the whole
 * pass is driven by repeated step() calls from Service::run(). A long
 * synchronous loop is what tripped the app-liveness watchdog in MapManager, and
 * although this runs on the service thread rather than the GUI thread, a
 * service that cannot answer COMMAND_APP_STOP is its own problem.
 *
 ******************************************************************************
 */

#ifndef FLASH_DUMPER_HPP
#define FLASH_DUMPER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include "DumpManifest.hpp"
#include "DumpRegion.hpp"

class FlashDumper
{
public:
    /// Where the dump has got to. Mirrored onto the wire by Commands.hpp's
    /// DumpState, which is a separate enum on purpose -- that one's numeric
    /// values are wire format.
    enum class State : uint8_t {
        Idle,     ///< Nothing running. May follow a completed scan.
        Checking, ///< Counting which chunk files are already present.
        Dumping,  ///< The pass is running.
        Done,     ///< Every chunk verified, manifest complete.
        Error,    ///< Stopped, and will not finish without intervention.
    };

    /// Why a dump stopped. Deliberately not a bool: "storage is full" and
    /// "the region config is nonsense" want different things done about them,
    /// and neither is guessable from a watch screen.
    ///
    /// There is no faulted-read member. A read that faults does not return
    /// here to be reported -- see the class comment.
    enum class Error : uint8_t {
        None = 0,
        BadRegion,        ///< Geometry fails DumpRegion::valid().
        OpenFailed,       ///< Could not open a chunk file for writing.
        ShortWrite,       ///< write() reported fewer bytes than asked for.
        VerifyFailed,     ///< A rewritten chunk still would not read back correctly.
        ManifestFailed,   ///< Could not write dump_manifest.txt.
        ManifestOverflow, ///< The manifest outgrew its buffer.
    };

    /// Name of the manifest, and the filename pattern for chunk files. Both
    /// are fixed by the host reassembler: it looks for exactly this manifest
    /// name in the directory it is pointed at, and derives each chunk's
    /// filename from the `off` field as `dump_%06X.bin`.
    static constexpr const char* kManifestName = "dump_manifest.txt";

    /**
     * @param window Base of the region in this process's address space. Must
     *               remain valid and readable for at least region.size bytes
     *               for the lifetime of this object.
     */
    FlashDumper(const SDK::Kernel& kernel, const DumpRegion& region, const uint8_t* window);

    /// Begins the cheap presence scan: how many chunk files already exist at
    /// the expected size. Does not read or hash anything, so it is safe to run
    /// unprompted at service start -- which is what gives the idle screen its
    /// "12/32 already done" before the user commits to anything.
    void beginScan();

    /// Begins the dump pass. No-op unless Idle, Done or Error -- a second
    /// press while Dumping must not restart a run in progress.
    void beginDump();

    /**
     * @brief Advance by at most @p budgetBytes of region content.
     *
     * The budget bounds hashing and writing, not the fixed per-chunk costs
     * (opening a file, rewriting the manifest), so one call can take longer
     * than the budget alone suggests. It is honoured in whole sub-writes.
     *
     * A no-op in every state but Checking and Dumping, so the caller can call
     * it unconditionally each loop iteration.
     */
    void step(size_t budgetBytes);

    /// Abandons any pass in progress, closes the open handle and returns to
    /// Idle with the presence counts stale. Chunk files already written are
    /// left alone -- they are the point.
    void cancel();

    State state() const { return mState; }
    Error error() const { return mError; }

    /// Chunk the error happened on. Only meaningful when state() is Error.
    unsigned errorChunk() const { return mErrorChunk; }

    const DumpRegion& region() const { return mRegion; }

    /// Chunks fully finished this pass, i.e. written or re-verified *and*
    /// recorded in the manifest. The numerator of the progress display.
    unsigned chunksDone() const { return mChunksDone; }

    /// How many of chunksDone() were skipped because the file on disk already
    /// matched memory. Reported so a resumed run visibly says so instead of
    /// looking implausibly fast.
    unsigned chunksVerified() const { return mChunksVerified; }

    /// Chunk files found by the last presence scan. Stale until beginScan()
    /// has run to completion; scanComplete() says whether to trust it.
    unsigned chunksPresent() const { return mChunksPresent; }
    bool     scanComplete() const { return mScanComplete; }

    /// Bytes of the region hashed so far this pass, including the partial
    /// chunk in flight.
    uint64_t bytesDone() const;

    /// Bytes in the region: the denominator, and a constant.
    uint64_t bytesTotal() const { return mRegion.size; }

    /// The finished whole-image CRC-32. Only meaningful once state() is Done;
    /// 0 before that, since a running value is not a CRC of anything.
    uint32_t wholeCrc() const { return mWholeCrcFinal; }

    /// The manifest as it currently stands, for tests and for anything that
    /// wants to see what was written without reading the file back.
    const DumpManifest& manifest() const { return mManifest; }

private:
    /// What the current chunk is doing with its file.
    enum class Mode : uint8_t {
        Writing,   ///< No usable file on disk: hash memory and write it out.
        Verifying, ///< A right-sized file exists: hash memory and the file, and compare.
    };

    /// Where the current chunk is in its life.
    enum class Phase : uint8_t {
        /// Walking memory, folding it into both CRCs, and either writing it
        /// out (Mode::Writing) or reading the file back alongside it
        /// (Mode::Verifying).
        Hashing,
        /// Re-writing a chunk whose file failed verification. The CRCs for
        /// this chunk are already final, so this phase must not touch them --
        /// hashing the same bytes into the running whole-image value twice
        /// would corrupt it for every chunk that follows.
        Rewriting,
    };

    const SDK::Kernel& mKernel;
    DumpRegion         mRegion;
    const uint8_t*     mWindow;

    State    mState      = State::Idle;
    Error    mError      = Error::None;
    unsigned mErrorChunk = 0;

    DumpManifest mManifest;

    // -- Presence scan --------------------------------------------------------
    unsigned mScanIndex     = 0;
    unsigned mChunksPresent = 0;
    bool     mScanComplete  = false;

    // -- Pass state -----------------------------------------------------------
    unsigned mChunkIndex     = 0;
    unsigned mChunksDone     = 0;
    unsigned mChunksVerified = 0;

    /// Bytes of the current chunk hashed (Hashing) or rewritten (Rewriting).
    uint32_t mChunkPos = 0;

    /// Running, unfinalised CRC of the current chunk's memory.
    uint32_t mChunkCrc = 0;

    /// Running, unfinalised CRC of the current chunk's file, in Mode::Verifying.
    uint32_t mFileCrc = 0;

    /// Running, unfinalised CRC of the whole region, chained across chunks in
    /// ascending order. Never reset mid-pass.
    uint32_t mWholeCrc = 0;

    /// The finalised value, published once the pass completes.
    uint32_t mWholeCrcFinal = 0;

    /// Bytes the filesystem confirmed written for the current chunk.
    uint32_t mChunkBw = 0;

    Mode  mMode  = Mode::Writing;
    Phase mPhase = Phase::Hashing;

    /// Set when a chunk file that claimed the right size could not actually be
    /// read to the end. Tracked as a flag rather than by poisoning mFileCrc,
    /// so that "the file is unreadable" and "the file's contents differ" reach
    /// the same rewrite path without either depending on two CRCs failing to
    /// collide.
    bool mVerifyReadFailed = false;

    std::unique_ptr<SDK::Interface::IFile> mFile;

    /// Read-back buffer for Mode::Verifying and nothing else. A member rather
    /// than a stack array: this runs on the service task, whose stack is
    /// 10 kB, and kMaxSubwrite alone is 16.
    uint8_t mReadBuf[DumpRegion::kMaxSubwrite];

    /// `dump_%06X.bin` for a chunk offset, into a caller-owned buffer. The
    /// format is the reassembler's, not ours.
    static void chunkFileName(uint32_t off, char* out, size_t outLen);

    /// Memory address of a chunk's first byte, for spot lines and logging.
    const uint8_t* chunkPtr(unsigned index) const
    {
        return mWindow + static_cast<size_t>(index) * mRegion.chunk;
    }

    void advanceScan(unsigned budgetEntries);
    void advanceDump(size_t budgetBytes);

    /// Opens the file for the chunk about to be processed and picks a Mode.
    /// Returns false having set the error state if a chunk that needs writing
    /// cannot be opened.
    bool openChunk();

    /// Ends the current chunk: decides ok, records the manifest line, closes
    /// the handle, and either advances to the next chunk or moves to
    /// Phase::Rewriting. Returns false if the dump stopped.
    bool finishChunk();

    /// Writes the manifest to storage. Returns false having set the error
    /// state if it could not be written whole, or if it has overflowed.
    bool flushManifest();

    /// Appends the closing whole-image and spot lines and finishes the pass.
    void finishPass();

    void fail(Error error);
};

#endif // FLASH_DUMPER_HPP
