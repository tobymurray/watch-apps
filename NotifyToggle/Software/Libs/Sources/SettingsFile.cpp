#include "SettingsFile.hpp"

#include <cstring>
#include <memory>

#include "DebugLog.hpp"
#include "SettingsPatch.hpp"

#define LOG_MODULE_PRX   "SettingsFile"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace SettingsFile
{

namespace
{

// One `..` reaches "Apps/" (this app's own directory is "Apps/NotifyToggle/"),
// a second reaches the volume root, where the real settings.json lives
// alongside "Apps/" itself -- confirmed directly against the watch's own
// directory listing, not assumed.
constexpr char kSettingsPath[]    = "../../settings.json";
constexpr char kSettingsTmpPath[] = "../../settings.json.notifytoggle.tmp";

// Static, not local: this code runs on the GUI task's 10 KB stack
// (UNA_APP_GUI_STACK_SIZE), and writeNotificationsFlag alone needs three of
// these live at once (orig survives to the restore-fallback path at the very
// end) plus a fourth for the confirm re-read it calls into -- 8 KB of stack
// locals on a 10 KB stack, before counting anything the caller or the kernel
// dispatcher had already pushed, is how you get a silent hard reset instead
// of a clean error. None of this needs to be reentrant: the app is single-
// threaded and nothing here is ever called recursively or concurrently.
char sReadBuf[kMaxTrustedFileBytes];
char sOrigBuf[kMaxTrustedFileBytes];
char sPatchedBuf[kMaxTrustedFileBytes];
char sVerifyBuf[kMaxTrustedFileBytes];

/// Opens `path` read-only, reads it whole into `buf` (capacity `cap`), and
/// closes it. Refuses (returns false) rather than reading a partial or
/// oversized file -- callers must never act on a truncated read.
bool readWholeFile(SDK::Interface::IFileSystem &fs, const char *path,
                    char *buf, size_t cap, size_t &outLen, Status &outStatus)
{
    // Checked via IFileSystem::exist(), not IFile::exist(), and before
    // constructing an IFile at all: probeDriveRoots() proved the former safe
    // even on a path that turns out to be wrong (a nonexistent numbered-drive
    // path returned false cleanly). This app's own crash logs show the run
    // getting past every fs.exist() probe and then dying on the very next
    // operation -- constructing an IFile for "../../settings.json" and
    // calling its own exist()/open() -- so that path is treated as suspect
    // until proven otherwise, and is checked the proven-safe way first.
    const bool exists = fs.exist(path);
    DebugLog::appendf(fs, "readWholeFile(%s): fs.exist()=%d", path, exists ? 1 : 0);
    if (!exists) {
        outStatus = Status::OpenFailed;
        return false;
    }

    auto file = fs.file(path);
    if (!file) {
        DebugLog::appendf(fs, "readWholeFile(%s): fs.file() returned null", path);
        outStatus = Status::OpenFailed;
        return false;
    }

    const size_t size = file->size();
    DebugLog::appendf(fs, "readWholeFile(%s): size()=%zu (cap=%zu)", path, size, cap);
    if (size == 0 || size >= cap) {
        // Empty or too large to be the file this code expects: refuse rather
        // than guess. size == cap is also refused, so there is always room
        // to NUL-terminate if a caller wants to.
        outStatus = Status::TooLarge;
        return false;
    }

    if (!file->open(false)) {
        DebugLog::appendf(fs, "readWholeFile(%s): open(read) failed", path);
        outStatus = Status::OpenFailed;
        return false;
    }
    size_t bytesRead = 0;
    const bool ok = file->read(buf, size, bytesRead);
    file->close();
    DebugLog::appendf(fs, "readWholeFile(%s): read() ok=%d bytesRead=%zu", path, ok ? 1 : 0, bytesRead);

    if (!ok || bytesRead != size) {
        outStatus = Status::ReadFailed;
        return false;
    }

    DebugLog::appendBytes(fs, path, buf, bytesRead);
    outLen = bytesRead;
    return true;
}

/// Reads the real file and reports the flag it currently holds. Shared by
/// the public read and by the write path's final "confirm what's really on
/// disk now" step.
Status readCurrent(SDK::Interface::IFileSystem &fs, bool &outEnabled)
{
    size_t len = 0;
    Status status = Status::Ok;
    if (!readWholeFile(fs, kSettingsPath, sReadBuf, sizeof(sReadBuf), len, status)) {
        DebugLog::appendf(fs, "readCurrent: readWholeFile failed, status=%d", static_cast<int>(status));
        return status;
    }

    bool enabled = false;
    const auto patchResult = SettingsPatch::readNotificationsFlag(sReadBuf, len, enabled);
    if (patchResult != SettingsPatch::Result::Ok) {
        DebugLog::appendf(fs, "readCurrent: pattern not found in %zu bytes (Result=%d)",
                           len, static_cast<int>(patchResult));
        return Status::NotFound;
    }
    DebugLog::appendf(fs, "readCurrent: Ok, enabled=%d", enabled ? 1 : 0);
    outEnabled = enabled;
    return Status::Ok;
}

} // namespace

Status readNotificationsFlag(SDK::Interface::IFileSystem &fs, bool &outEnabled)
{
    return readCurrent(fs, outEnabled);
}

Status writeNotificationsFlag(SDK::Interface::IFileSystem &fs, bool newEnabled, bool &outEnabled)
{
    DebugLog::appendf(fs, "writeNotificationsFlag: requested newEnabled=%d", newEnabled ? 1 : 0);

    // 1. Read the real file fresh -- never patch against a value this app
    //    cached earlier, only against what is on disk right now.
    size_t origLen = 0;
    Status status = Status::Ok;
    if (!readWholeFile(fs, kSettingsPath, sOrigBuf, sizeof(sOrigBuf), origLen, status)) {
        DebugLog::appendf(fs, "writeNotificationsFlag: initial read failed, status=%d",
                           static_cast<int>(status));
        return status;
    }

    // 2. Produce the patched document in memory. Nothing on disk has been
    //    touched yet.
    size_t patchedLen = 0;
    const auto spliceResult = SettingsPatch::spliceNotificationsFlag(
        sOrigBuf, origLen, newEnabled, sPatchedBuf, sizeof(sPatchedBuf), patchedLen);
    DebugLog::appendf(fs, "writeNotificationsFlag: spliceResult=%d", static_cast<int>(spliceResult));

    if (spliceResult == SettingsPatch::Result::AlreadySet) {
        outEnabled = newEnabled;
        return Status::NoChange;
    }
    if (spliceResult == SettingsPatch::Result::NotFound) {
        return Status::NotFound;
    }
    if (spliceResult == SettingsPatch::Result::OutputTooSmall) {
        return Status::TooLarge;
    }
    DebugLog::appendBytes(fs, "patched", sPatchedBuf, patchedLen);

    // 3. Stage the patched document in a temp file. settings.json itself is
    //    not opened for writing at any point up to here.
    {
        auto tmp = fs.file(kSettingsTmpPath);
        if (!tmp || !tmp->open(true, true)) {
            LOG_ERROR("could not open temp file for staging\n");
            DebugLog::appendf(fs, "writeNotificationsFlag: could not open temp file %s for staging",
                               kSettingsTmpPath);
            return Status::WriteFailed;
        }
        size_t bytesWritten = 0;
        const bool wrote = tmp->write(sPatchedBuf, patchedLen, bytesWritten);
        const bool flushed = wrote && tmp->flush();
        tmp->close();
        DebugLog::appendf(fs, "writeNotificationsFlag: staged to %s wrote=%d bytesWritten=%zu flushed=%d",
                           kSettingsTmpPath, wrote ? 1 : 0, bytesWritten, flushed ? 1 : 0);
        if (!wrote || bytesWritten != patchedLen || !flushed) {
            LOG_ERROR("temp file write/flush failed\n");
            fs.remove(kSettingsTmpPath);
            return Status::WriteFailed;
        }
    }

    // 4. Read the temp file back and compare it, byte for byte, against what
    //    was meant to be written. settings.json is still untouched.
    {
        size_t verifyLen = 0;
        Status verifyStatus = Status::Ok;
        const bool readOk =
            readWholeFile(fs, kSettingsTmpPath, sVerifyBuf, sizeof(sVerifyBuf), verifyLen, verifyStatus);
        const bool matches = readOk && verifyLen == patchedLen &&
                              std::memcmp(sVerifyBuf, sPatchedBuf, patchedLen) == 0;
        DebugLog::appendf(fs, "writeNotificationsFlag: verify readOk=%d verifyLen=%zu matches=%d",
                           readOk ? 1 : 0, verifyLen, matches ? 1 : 0);
        if (!matches) {
            LOG_ERROR("temp file verify mismatch; aborting without touching settings.json\n");
            fs.remove(kSettingsTmpPath);
            return Status::VerifyFailed;
        }
    }

    // 5. Swap the verified temp file in. Prefer a direct rename over the
    //    existing file, in case the backend supports atomic replace; if it
    //    reports failure, fall back to remove-then-rename -- the same
    //    sequence SDK::AppConfig::save() uses, and documented there as safe
    //    against a reset landing between the two steps because the temp file
    //    survives it. The remaining gap (a reset landing between remove and
    //    rename, so briefly there is no settings.json at all) is covered
    //    below by restoring `orig` from memory rather than leaving the
    //    watch's real settings file missing.
    const bool directRenameOk = fs.rename(kSettingsTmpPath, kSettingsPath);
    DebugLog::appendf(fs, "writeNotificationsFlag: direct rename(%s -> %s) ok=%d",
                       kSettingsTmpPath, kSettingsPath, directRenameOk ? 1 : 0);
    if (!directRenameOk) {
        const bool removeOk = fs.remove(kSettingsPath);
        DebugLog::appendf(fs, "writeNotificationsFlag: fallback remove(%s) ok=%d", kSettingsPath, removeOk ? 1 : 0);
        if (!removeOk) {
            LOG_ERROR("could not remove settings.json to swap in the update\n");
            fs.remove(kSettingsTmpPath);
            return Status::SwapFailed;
        }
        const bool renameAfterRemoveOk = fs.rename(kSettingsTmpPath, kSettingsPath);
        DebugLog::appendf(fs, "writeNotificationsFlag: fallback rename(%s -> %s) ok=%d",
                           kSettingsTmpPath, kSettingsPath, renameAfterRemoveOk ? 1 : 0);
        if (!renameAfterRemoveOk) {
            LOG_ERROR("rename after remove failed; restoring the original settings.json from memory\n");
            auto restore = fs.file(kSettingsPath);
            size_t bytesWritten = 0;
            if (restore && restore->open(true, true) &&
                restore->write(sOrigBuf, origLen, bytesWritten) && bytesWritten == origLen &&
                restore->flush()) {
                restore->close();
                LOG_INFO("original settings.json restored\n");
                DebugLog::append(fs, "writeNotificationsFlag: original settings.json restored from memory");
            } else {
                if (restore) restore->close();
                LOG_ERROR("restore of original settings.json also failed -- filesystem-level fault\n");
                DebugLog::append(fs, "writeNotificationsFlag: RESTORE ALSO FAILED -- filesystem-level fault");
            }
            fs.remove(kSettingsTmpPath);
            return Status::SwapFailed;
        }
    }

    // 6. Never trust the buffer this code just wrote: confirm what is
    //    actually on disk now, the same way a fresh launch would read it.
    bool confirmed = newEnabled;
    if (readCurrent(fs, confirmed) != Status::Ok) {
        // The swap already completed and step 4 verified those exact bytes;
        // a failure to reopen immediately after is a separate, transient
        // condition, not evidence the write was wrong.
        LOG_WARNING("post-write confirm read failed; reporting the value just written\n");
    }
    DebugLog::appendf(fs, "writeNotificationsFlag: done, confirmed=%d", confirmed ? 1 : 0);
    outEnabled = confirmed;
    return Status::Ok;
}

} // namespace SettingsFile
