#ifndef PACK_CRC_VERIFIER_HPP
#define PACK_CRC_VERIFIER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include "ManagerLog.hpp"

/**
 * @brief Background, resumable CRC-32/ISO-HDLC verifier for one file.
 *
 * Format-agnostic on purpose: the CRC scope is "every byte from offset 0 up
 * to (but not including) the trailing 4-byte CRC" -- this class has no idea
 * what's inside the file (rawtiles or anything else), it just verifies a
 * caller-asserted-trust-style footer checksum and caches the result in a
 * PackTrustMarker. MapManager's Service constructs one instance per file
 * discovered in the shared maps directory (see Service.cpp) rather than
 * knowing about any fixed path itself -- that's the one real difference from
 * the app this pattern was first built for (AthensRun's MapPackCrcVerifier),
 * where a single instance resolved its own path from a small fixed
 * candidate list. Here the caller (Service) does the discovery; this class
 * just verifies whatever path it's given.
 *
 * Usage: construct with the exact path to verify, call start() once, then
 * call step() once per Service::run() loop iteration -- every iteration, not
 * just an idle branch, since other message traffic must not starve a
 * background pass. step() is a no-op once done().
 */
class PackCrcVerifier {
public:
    /// Size of one read()/CRC I/O chunk, and of the stack buffer step() uses.
    /// This is NOT the amount of work one step() call does -- step(maxBytes)
    /// consumes its whole budget in chunks of this size (see step()). Keep it
    /// small: it is a stack allocation in a background service task.
    static constexpr size_t kIoChunkBytes = 4096;

    /// Default step() budget. Callers that want a bigger slice pass one;
    /// Service does (see kSliceBudgetBytes in Service.hpp).
    static constexpr size_t kDefaultChunkBytes = kIoChunkBytes;

    enum class Status {
        Idle,        ///< Not started yet.
        InProgress,  ///< Scanning; call step() again.
        Verified,    ///< CRC matched (this pass, or an already-trusted
                     ///< marker found at start()); marker is up to date.
        Mismatched,  ///< Finished: CRC did not match. Bad marker written.
        IoError,     ///< Could not open/read the file or its marker.
    };

    PackCrcVerifier(const SDK::Kernel& kernel, std::string path);

    /// Opens @c path, and:
    ///   - Verified immediately, with no scan I/O, if a Good marker already
    ///     matches the file's current (size, declared CRC).
    ///   - Mismatched immediately, with no scan I/O, if a Bad marker already
    ///     matches it. A confirmed-corrupt file does not become uncorrupt by
    ///     being re-read, so re-deriving that answer on every boot is pure
    ///     cost; the marker is the cached answer for both verdicts, not just
    ///     the happy one. Replacing the file changes its (size, crc) and so
    ///     invalidates the marker on its own -- see the (size, crc) guard.
    ///   - InProgress otherwise (scan starts from byte 0).
    ///   - IoError if the file can't be opened or is too short to have a
    ///     trailing CRC.
    /// No-op (returns current status unchanged) if already InProgress.
    /// Calling start() again on a finished verifier re-evaluates from
    /// scratch, which is how Service re-arms an entry whose file changed.
    Status start();

    /// Advances the scan by up to @p maxBytes, reading in kIoChunkBytes
    /// chunks. No-op if not InProgress, or if @p maxBytes is 0.
    ///
    /// One call does the whole budget rather than a single chunk: the loop
    /// driving this is gated by a kernel message wait, so a one-chunk-per-
    /// wait design ties throughput to the wait period rather than to the
    /// storage (that is exactly the ~8KB/s bug the README describes, and
    /// shortening the wait only moved the ceiling). Budget per call, not
    /// chunk per call, decouples the two.
    ///
    /// On finishing a full pass: compares against the trailing 4-byte CRC,
    /// writes a Good marker on match or a Bad marker (with the mismatching
    /// declared CRC, for diagnostics) on mismatch.
    Status step(size_t maxBytes = kDefaultChunkBytes);

    /// Drops any scan in progress and returns to Idle, so the next start()
    /// re-evaluates the file from scratch. Closes an open handle. Does not
    /// touch the on-disk marker -- a stale marker is invalidated by its own
    /// (size, crc) guard, not by deleting it.
    void reset();

    Status             status() const { return mStatus; }
    bool                done() const   { return mStatus != Status::InProgress; }
    const std::string&  path() const   { return mPath; }

    /// Bytes scanned so far. Meaningful once start() has begun a real scan;
    /// 0 before that (including the already-trusted-via-marker case, where
    /// no scan ever runs).
    uint64_t bytesDone() const { return mBytesDone; }

    /// Size of the scannable region (file size minus the trailing CRC) --
    /// the denominator for a percent-complete display. 0 before start().
    uint64_t bytesTotal() const { return mCrcStart; }

    /// Kernel timestamp (ms) the current scan began, for an ETA computed
    /// from the actually-observed rate: elapsed = nowMs - startedAtMs().
    uint32_t startedAtMs() const { return mStartedAtMs; }

    /// Size the file had when start() last opened it, or 0 if start() has
    /// never got that far (never called, or the open itself failed). Service
    /// compares this against what the directory currently reports to notice a
    /// file that changed under a finished verdict.
    uint64_t fileSize() const { return mFileSize; }

private:
    const SDK::Kernel&                      mKernel;
    ManagerLog                              mLog;
    std::string                              mPath;
    std::unique_ptr<SDK::Interface::IFile>  mFile;
    uint64_t                                mFileSize    = 0;
    uint64_t                                mCrcStart    = 0;
    uint64_t                                mBytesDone   = 0;
    uint32_t                                mCrc         = 0xFFFFFFFFu;
    uint32_t                                mDeclaredCrc = 0; // read once in start(), compared in finish()
    Status                                  mStatus      = Status::Idle;

    // Throttling state for step()'s progress logging.
    uint64_t mLastLoggedBytes = 0;
    uint32_t mLastLoggedAtMs  = 0;
    uint32_t mStartedAtMs     = 0;

    // Seeks to mCrcStart and reads the trailing 4-byte declared CRC into
    // out. Does not restore file position -- callers that need the cursor
    // back at 0 (start(), before scanning) must seek() again afterward.
    bool readFooterCrc(uint32_t& out);
    void finish(bool matched, uint32_t declaredCrc);
    std::string markerPath() const;
};

#endif // PACK_CRC_VERIFIER_HPP
