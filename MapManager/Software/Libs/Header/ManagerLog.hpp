#ifndef MANAGER_LOG_HPP
#define MANAGER_LOG_HPP

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/Kernel.hpp"

/**
 * @brief On-device diagnostic log for MapManager's background verification
 *        passes -- device-side file sink for diagnostic data, in its own
 *        directory, retrievable over the same USB-MSC connection used to
 *        deploy, no debug-UART rig required.
 *
 * Every call is a fresh, self-contained open-append-write-close round-trip
 * (not a held-open handle), so nothing is lost if the process dies mid-run.
 * Best-effort: failures are silently swallowed (this is a diagnostic aid, not
 * something correctness can depend on).
 *
 * Bounded on purpose. This app's service is APP_AUTOSTART and never exits, so
 * an append-only log here is append-forever across every boot the device ever
 * does -- unbounded growth on internal flash, in exchange for history nobody
 * reads. At kMaxBytes the log restarts from empty with a one-line notice. The
 * useful window is "what happened during this boot", and that fits.
 */
class ManagerLog {
public:
    /// Restart the log once it reaches this size. Sized to hold a full
    /// verification pass's worth of lines (see PackCrcVerifier::step()'s
    /// throttle) with room to spare, while staying negligible on flash.
    static constexpr size_t kMaxBytes = 64 * 1024;

    explicit ManagerLog(const SDK::Kernel& kernel) : mKernel(kernel) {}

    void logf(const char* fmt, ...) const
    {
        // mkdir is a no-op (and returns true) if the directory already
        // exists, so just always ensure it's there before opening.
        mKernel.fs.mkdir(kDir);

        std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kPath);
        if (!file) {
            return;
        }
        // wMode=true, override=false: open for writing without truncating.
        if (!file->open(true, false)) {
            return;
        }

        bool rotated = false;
        if (file->size() >= kMaxBytes) {
            // Reopen with override=true to truncate. Cheaper than copying a
            // tail forward, and the tail is not worth the flash writes.
            file->close();
            if (!file->open(true, true)) {
                return;
            }
            rotated = true;
        }

        // Explicit seek-to-end: whether a non-override write-mode open
        // positions the cursor at EOF or at 0 isn't guaranteed by the
        // interface -- don't rely on it.
        file->seek(file->size());

        if (rotated) {
            static const char kNotice[] = "-- log restarted (size cap reached) --\n";
            size_t noticeWritten = 0;
            file->write(kNotice, sizeof(kNotice) - 1, noticeWritten);
        }

        char line[256];
        int len = std::snprintf(line, sizeof(line), "[%lums] ",
                                 static_cast<unsigned long>(mKernel.sys.getTimeMs()));
        if (len > 0 && static_cast<size_t>(len) < sizeof(line)) {
            va_list args;
            va_start(args, fmt);
            std::vsnprintf(line + len, sizeof(line) - static_cast<size_t>(len), fmt, args);
            va_end(args);
        }

        size_t written = 0;
        file->write(line, std::strlen(line), written);
        file->flush();
        file->close();
    }

private:
    const SDK::Kernel& mKernel;
    static constexpr const char* kDir  = "Debug";
    static constexpr const char* kPath = "Debug/mapmanager_verify.log";
};

#endif // MANAGER_LOG_HPP
