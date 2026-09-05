#include "SettingsPersist.hpp"

#include <cstdint>
#include <cstring>

#include "DebugLog.hpp"
#include "SettingsSplice.hpp"

namespace SettingsPersist
{

namespace
{

using FileOpenFn    = int (*)(void *self, int flag1, int flag2);
using FileReadFn    = int (*)(void *self, void *buf, uint32_t len, uint32_t *bytesReadOut);
using FileWriteFn   = int (*)(void *self, const void *buf, uint32_t len, uint32_t *bytesWrittenOut);
using FileCloseFn   = void (*)(void *self);
using FileReleaseFn = void (*)(void *self);
using SetPathFn     = char *(*)(char *dst, const char *src, unsigned maxlen);
using FileExistsFn  = int (*)(const char *path);
using FileDeleteFn  = int (*)(const char *path);
using FileRenameFn  = int (*)(const char *oldPath, const char *newPath);

// open()'s (flag1, flag2) -> FatFs mode, confirmed on kernel 1.4.0 (see
// SettingsAddresses.hpp/.cpp -- this part of the calling convention, unlike
// the addresses themselves, is assumed stable across the firmware versions
// this app might ever add to that table, since it's FatFs's own mode-flag
// encoding, not anything specific to one kernel build):
//   (0, _) -> FA_READ
//   (1, 1) -> FA_CREATE_ALWAYS | FA_WRITE | FA_READ
constexpr int kOpenReadOnly        = 0;
constexpr int kOpenCreateOrReplace = 1;

constexpr const char *kSettingsPath    = "2:/settings.json";
constexpr const char *kSettingsTmpPath = "2:/settings.json.tmp";
// This app's own scratch copy, deliberately not "2:/settings.json.bak": the
// firmware maintains that one itself (observed rotating it, alongside
// local_settings.json.bak, on USB connect), and overwriting it would spend the
// wearer's only firmware-made backup on this app's convenience.
constexpr const char *kSettingsPrevPath = "2:/settings.json.ntprev";

// Sane upper bound on AddressSet::fileObjectSize -- a fixed-capacity local
// buffer needs a compile-time size, but the real size is a per-firmware
// runtime value (SettingsAddresses::AddressSet::fileObjectSize). 1.4.0's
// real object is 856 bytes; this leaves headroom for a future firmware
// entry with a larger one while still refusing (not silently truncating or
// overflowing the stack) anything implausible.
constexpr size_t kMaxFileObjectSize = 1536;

/// Identifies file content in a log line without reproducing it: settings.json
/// holds height, weight, gender and date of birth, and a diagnostic must not
/// leave a plaintext copy of them on the watch. Defined in every build because
/// its callers are DebugLog arguments, which still have to compile when
/// DebugLog is compiled out.
uint32_t contentHash(const char *data, size_t len)
{
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < len; ++i) {
        h = (h ^ static_cast<uint8_t>(data[i])) * 0x01000193u;
    }
    return h;
}

const char *statusName(Status status)
{
    switch (status) {
        case Status::Ok:               return "Ok";
        case Status::ReadOpenFailed:   return "ReadOpenFailed";
        case Status::ReadFailed:       return "ReadFailed";
        case Status::SizeOutOfRange:   return "SizeOutOfRange";
        case Status::FieldNotFound:    return "FieldNotFound";
        case Status::WriteOpenFailed:  return "WriteOpenFailed";
        case Status::WriteFailed:      return "WriteFailed";
        case Status::CommitFailed:     return "CommitFailed";
        case Status::ReadbackMismatch: return "ReadbackMismatch";
    }
    return "?";
}

/// A raw, zero-initialized stand-in for the kernel's internal `File` object.
/// Never constructed or destructed as a real C++ object -- just a correctly
/// sized, correctly aligned byte buffer passed as `this` to the confirmed
/// non-virtual methods this firmware's AddressSet points at. Only the first
/// `addrs.fileObjectSize` bytes are ever touched. 8-byte aligned because the
/// FIL region embeds a 64-bit FSIZE_t field.
struct alignas(8) RawFile {
    uint8_t bytes[kMaxFileObjectSize];

    void *self() { return bytes; }
};

/// True if `addrs` is safe to use with RawFile -- specifically, that its
/// object size fits the fixed local buffer every RawFile actually is. A
/// future table entry with an implausible value is refused here rather than
/// silently corrupting the stack.
bool objectSizeInRange(const SettingsAddresses::AddressSet &addrs)
{
    return addrs.fileObjectSize > 0 && addrs.fileObjectSize <= kMaxFileObjectSize &&
           addrs.pathBufferOffset + addrs.pathBufferSize <= addrs.fileObjectSize &&
           addrs.fileSizeFieldOffset + sizeof(uint64_t) <= addrs.fileObjectSize;
}

void resetFile(RawFile &file, const SettingsAddresses::AddressSet &addrs, const char *path)
{
    std::memset(file.bytes, 0, addrs.fileObjectSize);
    reinterpret_cast<SetPathFn>(addrs.setPathAddr)(
        reinterpret_cast<char *>(file.bytes + addrs.pathBufferOffset), path,
        static_cast<unsigned>(addrs.pathBufferSize));
}

/// Reads the real current content of 2:/settings.json into `outBuf` (capacity
/// `kMaxSettingsFileSize`), refusing rather than guessing if the reported
/// size is 0 or larger than that cap.
Status readCurrentFile(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                        char *outBuf, size_t &outLen)
{
    RawFile file{};
    resetFile(file, addrs, kSettingsPath);

    const auto fileOpen = reinterpret_cast<FileOpenFn>(addrs.fileOpenAddr);
    const auto fileRead = reinterpret_cast<FileReadFn>(addrs.fileReadAddr);
    const auto fileClose = reinterpret_cast<FileCloseFn>(addrs.fileCloseAddr);
    const auto fileRelease = reinterpret_cast<FileReleaseFn>(addrs.fileReleaseAddr);

    const int openRet = fileOpen(file.self(), kOpenReadOnly, 0);
    DebugLog::appendf(fs, "read: open(%s, READ) -> %d", kSettingsPath, openRet);
    if (openRet == 0) {
        return Status::ReadOpenFailed;
    }

    uint64_t fileSize = 0;
    std::memcpy(&fileSize, file.bytes + addrs.fileSizeFieldOffset, sizeof(fileSize));

    if (fileSize == 0 || fileSize > kMaxSettingsFileSize) {
        DebugLog::appendf(fs, "SettingsPersist: file size %llu out of expected range (cap=%zu)",
                           static_cast<unsigned long long>(fileSize), kMaxSettingsFileSize);
        fileClose(file.self());
        fileRelease(file.self());
        return Status::SizeOutOfRange;
    }

    uint32_t bytesRead = 0;
    const int readOk = fileRead(file.self(), outBuf, static_cast<uint32_t>(fileSize), &bytesRead);
    fileClose(file.self());
    fileRelease(file.self());
    DebugLog::appendf(fs, "read: size=%llu read -> %d bytesRead=%u",
                       static_cast<unsigned long long>(fileSize), readOk, bytesRead);

    if (!readOk || bytesRead != fileSize) {
        DebugLog::appendf(fs, "SettingsPersist: read failed or short (ok=%d bytesRead=%u expected=%llu)",
                           readOk, bytesRead, static_cast<unsigned long long>(fileSize));
        return Status::ReadFailed;
    }

    outLen = static_cast<size_t>(fileSize);
    return Status::Ok;
}

/// Rewrites `phone.notifications` in `buf` to `newEnabled`, adjusting `len`.
/// Refuses rather than guessing if the file does not have the shape this app
/// knows how to edit -- SettingsSplice.hpp, exercised by `Tests/`.
Status spliceNotificationsField(char *buf, size_t &len, bool newEnabled, size_t &valueOffset)
{
    switch (SettingsSplice::setNotifications(buf, len, kBufferCapacity, newEnabled, &valueOffset)) {
        case SettingsSplice::Result::Ok:
            return Status::Ok;
        case SettingsSplice::Result::WouldNotFit:
            return Status::SizeOutOfRange;
        case SettingsSplice::Result::FieldNotFound:
            break;
    }
    return Status::FieldNotFound;
}

/// Writes `buf`/`len` to a brand-new `2:/settings.json.tmp`, never touching
/// the real file. On any failure, best-effort deletes the tmp file so a
/// half-written leftover doesn't confuse the next attempt.
Status writeTmpFile(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                     const char *buf, size_t len)
{
    RawFile file{};
    resetFile(file, addrs, kSettingsTmpPath);

    const auto fileOpen = reinterpret_cast<FileOpenFn>(addrs.fileOpenAddr);
    const auto fileWrite = reinterpret_cast<FileWriteFn>(addrs.fileWriteAddr);
    const auto fileClose = reinterpret_cast<FileCloseFn>(addrs.fileCloseAddr);
    const auto fileRelease = reinterpret_cast<FileReleaseFn>(addrs.fileReleaseAddr);
    const auto fileExists = reinterpret_cast<FileExistsFn>(addrs.fileExistsAddr);
    const auto fileDelete = reinterpret_cast<FileDeleteFn>(addrs.fileDeleteAddr);

    const int openRet = fileOpen(file.self(), kOpenCreateOrReplace, 1);
    DebugLog::appendf(fs, "write: open(%s, CREATE) -> %d", kSettingsTmpPath, openRet);
    if (openRet == 0) {
        return Status::WriteOpenFailed;
    }

    uint32_t bytesWritten = 0;
    const int writeOk = fileWrite(file.self(), buf, static_cast<uint32_t>(len), &bytesWritten);
    fileClose(file.self());
    fileRelease(file.self());
    DebugLog::appendf(fs, "write: write(%zu) -> %d bytesWritten=%u", len, writeOk, bytesWritten);

    if (!writeOk || bytesWritten != len) {
        const int existsRet = fileExists(kSettingsTmpPath);
        DebugLog::appendf(fs, "write: cleanup exists(tmp) -> %d", existsRet);
        if (existsRet) {
            DebugLog::appendf(fs, "write: cleanup delete(tmp) -> %d", fileDelete(kSettingsTmpPath));
        }
        return Status::WriteFailed;
    }

    return Status::Ok;
}

/// Moves an already-written `2:/settings.json.tmp` into place.
///
/// FatFs will not rename onto a name that exists, so the previous file has to
/// move aside first; if the commit then fails, it is moved straight back,
/// which is what keeps the wearer from being left with no settings file at
/// all. On success the scratch copy is removed. The firmware's own
/// `settings.json.bak` is never read, written or deleted here.
Status commitTmpFile(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs)
{
    const auto fileExists = reinterpret_cast<FileExistsFn>(addrs.fileExistsAddr);
    const auto fileDelete = reinterpret_cast<FileDeleteFn>(addrs.fileDeleteAddr);
    const auto fileRename = reinterpret_cast<FileRenameFn>(addrs.fileRenameAddr);

    const int realExistsRet = fileExists(kSettingsPath);
    DebugLog::appendf(fs, "commit: exists(real) -> %d", realExistsRet);
    const bool hadPrevious = realExistsRet != 0;

    if (hadPrevious) {
        const int staleRet = fileExists(kSettingsPrevPath);
        DebugLog::appendf(fs, "commit: exists(prev) -> %d", staleRet);
        if (staleRet) {
            const int delRet = fileDelete(kSettingsPrevPath);
            DebugLog::appendf(fs, "commit: delete(prev) -> %d", delRet);
            if (!delRet) {
                return Status::CommitFailed;
            }
        }
        const int asideRet = fileRename(kSettingsPath, kSettingsPrevPath);
        DebugLog::appendf(fs, "commit: rename(real -> prev) -> %d", asideRet);
        if (!asideRet) {
            return Status::CommitFailed;
        }
    }

    const int commitRet = fileRename(kSettingsTmpPath, kSettingsPath);
    DebugLog::appendf(fs, "commit: rename(tmp -> real) -> %d", commitRet);
    if (!commitRet) {
        if (hadPrevious) {
            const int restoreRet = fileRename(kSettingsPrevPath, kSettingsPath);
            DebugLog::appendf(fs, "commit: ROLLBACK rename(prev -> real) -> %d", restoreRet);
        }
        return Status::CommitFailed;
    }

    if (hadPrevious && fileExists(kSettingsPrevPath)) {
        DebugLog::appendf(fs, "commit: delete(prev) after success -> %d", fileDelete(kSettingsPrevPath));
    }

    return Status::Ok;
}

Status writeWholeFileAtomic(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                             const char *buf, size_t len)
{
    Status status = writeTmpFile(fs, addrs, buf, len);
    if (status != Status::Ok) {
        return status;
    }
    return commitTmpFile(fs, addrs);
}

} // namespace

Status persistNotificationsFlag(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                                 bool newEnabled)
{
    DebugLog::appendf(fs, "persist: requested notifications=%s", newEnabled ? "true" : "false");

    if (!objectSizeInRange(addrs)) {
        DebugLog::appendf(fs,
                           "persist: File layout out of range -- objectSize=%zu pathOff=%zu pathSize=%zu sizeOff=%zu",
                           addrs.fileObjectSize, addrs.pathBufferOffset, addrs.pathBufferSize,
                           addrs.fileSizeFieldOffset);
        return Status::SizeOutOfRange;
    }

    char buf[kBufferCapacity];
    size_t len = 0;

    Status status = readCurrentFile(fs, addrs, buf, len);
    if (status != Status::Ok) {
        DebugLog::appendf(fs, "persist: read failed -> %s", statusName(status));
        return status;
    }
    const uint32_t originalHash = contentHash(buf, len);
    DebugLog::appendf(fs, "persist: read %zu bytes, hash=0x%08X", len, originalHash);

    size_t valueOffset = 0;
    status = spliceNotificationsField(buf, len, newEnabled, valueOffset);
    if (status != Status::Ok) {
        DebugLog::appendf(fs, "persist: splice refused -> %s (nothing written)", statusName(status));
        return status;
    }
    DebugLog::appendf(fs, "persist: spliced at offset %zu, now %zu bytes, hash=0x%08X",
                       valueOffset, len, contentHash(buf, len));

    status = writeWholeFileAtomic(fs, addrs, buf, len);
    if (status != Status::Ok) {
        DebugLog::appendf(fs, "persist: write/commit failed -> %s", statusName(status));
        return status;
    }

    char verifyBuf[kBufferCapacity];
    size_t verifyLen = 0;
    status = readCurrentFile(fs, addrs, verifyBuf, verifyLen);
    if (status != Status::Ok) {
        DebugLog::appendf(fs, "persist: readback could not be read -> %s (the commit did happen)",
                           statusName(status));
        return Status::ReadbackMismatch;
    }

    DebugLog::appendf(fs, "persist: readback %zu bytes, hash=0x%08X", verifyLen, contentHash(verifyBuf, verifyLen));

    if (verifyLen != len || std::memcmp(verifyBuf, buf, len) != 0) {
        size_t firstDiff = 0;
        const size_t shared = verifyLen < len ? verifyLen : len;
        while (firstDiff < shared && verifyBuf[firstDiff] == buf[firstDiff]) {
            ++firstDiff;
        }
        DebugLog::appendf(fs, "persist: readback MISMATCH (expected %zu bytes, got %zu, first difference at %zu)",
                           len, verifyLen, firstDiff);
        return Status::ReadbackMismatch;
    }

    DebugLog::append(fs, "persist: verified OK -- the file on flash is what we wrote");
    return Status::Ok;
}

bool recoverInterruptedCommit(SDK::Interface::IFileSystem &fs,
                              const SettingsAddresses::AddressSet &addrs)
{
    if (!objectSizeInRange(addrs)) {
        return false;
    }

    const auto fileExists = reinterpret_cast<FileExistsFn>(addrs.fileExistsAddr);
    const auto fileRename = reinterpret_cast<FileRenameFn>(addrs.fileRenameAddr);

    if (fileExists(kSettingsPath) || !fileExists(kSettingsPrevPath)) {
        return false;
    }

    const int ret = fileRename(kSettingsPrevPath, kSettingsPath);
    DebugLog::appendf(fs, "recover: no settings.json but a scratch copy exists; rename back -> %d", ret);
    return ret != 0;
}

Status readSettingsFile(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                        char *outBuf, size_t &outLen)
{
    if (!objectSizeInRange(addrs)) {
        return Status::SizeOutOfRange;
    }
    return readCurrentFile(fs, addrs, outBuf, outLen);
}

bool validatePrimitives(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs)
{
    static constexpr const char *kProbeA = "2:/nt-probe-a.tmp";
    static constexpr const char *kProbeB = "2:/nt-probe-b.tmp";
    static constexpr const char *kProbeText = "NotifyToggle primitive self-test";
    const size_t probeLen = std::strlen(kProbeText);

    DebugLog::append(fs, "=== validating primitives: scratch paths only, settings.json not written ===");

    if (!objectSizeInRange(addrs)) {
        DebugLog::append(fs, "validate: File layout out of range -- refusing");
        return false;
    }

    const auto fileOpen    = reinterpret_cast<FileOpenFn>(addrs.fileOpenAddr);
    const auto fileRead    = reinterpret_cast<FileReadFn>(addrs.fileReadAddr);
    const auto fileWrite   = reinterpret_cast<FileWriteFn>(addrs.fileWriteAddr);
    const auto fileClose   = reinterpret_cast<FileCloseFn>(addrs.fileCloseAddr);
    const auto fileRelease = reinterpret_cast<FileReleaseFn>(addrs.fileReleaseAddr);
    const auto fileExists  = reinterpret_cast<FileExistsFn>(addrs.fileExistsAddr);
    const auto fileDelete  = reinterpret_cast<FileDeleteFn>(addrs.fileDeleteAddr);
    const auto fileRename  = reinterpret_cast<FileRenameFn>(addrs.fileRenameAddr);

    fileDelete(kProbeA);
    fileDelete(kProbeB);

    bool ok = true;
    auto check = [&](const char *what, bool passed) {
        DebugLog::appendf(fs, "validate: %s -> %s", what, passed ? "ok" : "FAILED");
        ok = ok && passed;
        return passed;
    };

    check("exists(absent) is zero", fileExists(kProbeA) == 0);

    RawFile file{};
    resetFile(file, addrs, kProbeA);
    const char *pathInObject = reinterpret_cast<const char *>(file.bytes + addrs.pathBufferOffset);
    check("setPath round-trips through the object", std::strcmp(pathInObject, kProbeA) == 0);

    if (check("open(CREATE) succeeds", fileOpen(file.self(), kOpenCreateOrReplace, 1) != 0)) {
        uint32_t wrote = 0;
        const int wRet = fileWrite(file.self(), kProbeText, static_cast<uint32_t>(probeLen), &wrote);
        check("write reports the full length", wRet != 0 && wrote == probeLen);
        fileClose(file.self());
        fileRelease(file.self());
    }

    check("exists(present) is non-zero", fileExists(kProbeA) != 0);

    resetFile(file, addrs, kProbeA);
    if (check("open(READ) succeeds", fileOpen(file.self(), kOpenReadOnly, 0) != 0)) {
        uint64_t reported = 0;
        std::memcpy(&reported, file.bytes + addrs.fileSizeFieldOffset, sizeof(reported));
        check("the size field reads back the written length", reported == probeLen);

        char readBuf[64] = {};
        uint32_t got = 0;
        const int rRet = fileRead(file.self(), readBuf, static_cast<uint32_t>(probeLen), &got);
        check("content round-trips", rRet != 0 && got == probeLen &&
                                      std::memcmp(readBuf, kProbeText, probeLen) == 0);
        fileClose(file.self());
        fileRelease(file.self());
    }

    check("rename onto a free name succeeds", fileRename(kProbeA, kProbeB) != 0);
    check("rename moved the file", fileExists(kProbeA) == 0 && fileExists(kProbeB) != 0);

    // Logged, not required: whether a rename may land on an occupied name is
    // FatFs policy rather than evidence about these addresses, and the commit
    // sequence moves the previous file aside either way.
    resetFile(file, addrs, kProbeA);
    if (fileOpen(file.self(), kOpenCreateOrReplace, 1)) {
        uint32_t wrote = 0;
        fileWrite(file.self(), kProbeText, static_cast<uint32_t>(probeLen), &wrote);
        fileClose(file.self());
        fileRelease(file.self());
    }
    DebugLog::appendf(fs, "validate: rename onto an OCCUPIED name -> %d (informational)",
                       fileRename(kProbeB, kProbeA));

    check("delete(present) is non-zero", fileDelete(kProbeA) != 0);
    check("delete(absent) is zero", fileDelete(kProbeA) == 0);
    fileDelete(kProbeB);
    check("scratch files are gone", fileExists(kProbeA) == 0 && fileExists(kProbeB) == 0);

    DebugLog::appendf(fs, "=== primitives %s ===", ok ? "VALIDATED" : "REJECTED");
    return ok;
}


} // namespace SettingsPersist
