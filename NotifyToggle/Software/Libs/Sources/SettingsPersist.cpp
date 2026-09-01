#include "SettingsPersist.hpp"

#include <cstdint>
#include <cstring>

#include "DebugLog.hpp"

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

// open()'s (flag1, flag2) -> FatFs mode, confirmed on kernel 1.4.0 (see
// SettingsAddresses.hpp/.cpp -- this part of the calling convention, unlike
// the addresses themselves, is assumed stable across the firmware versions
// this app might ever add to that table, since it's FatFs's own mode-flag
// encoding, not anything specific to one kernel build):
//   (0, _) -> FA_READ
//   (1, 1) -> FA_CREATE_ALWAYS | FA_WRITE | FA_READ
constexpr int kOpenReadOnly        = 0;
constexpr int kOpenCreateOrReplace = 1;

constexpr const char *kSettingsPath = "2:/settings.json";

// Sane upper bound on the settings file this app will read/write. The real
// file is 245 bytes today; this leaves generous headroom for the firmware
// adding fields later, while still refusing to trust an unexpectedly huge or
// zero-length read (SizeOutOfRange) rather than acting on it.
constexpr size_t kMaxSettingsFileSize = 512;
// Buffer headroom beyond kMaxSettingsFileSize: 1 byte for a null terminator
// (this app's own addition, not part of the file) + 1 byte for the "true"
// (4 chars) vs "false" (5 chars) length delta a splice can introduce.
constexpr size_t kBufferHeadroom = 8;
constexpr size_t kBufferCapacity = kMaxSettingsFileSize + kBufferHeadroom;

// Sane upper bound on AddressSet::fileObjectSize -- a fixed-capacity local
// buffer needs a compile-time size, but the real size is a per-firmware
// runtime value (SettingsAddresses::AddressSet::fileObjectSize). 1.4.0's
// real object is 856 bytes; this leaves headroom for a future firmware
// entry with a larger one while still refusing (not silently truncating or
// overflowing the stack) anything implausible.
constexpr size_t kMaxFileObjectSize = 1536;

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

void resetFile(RawFile &file, const SettingsAddresses::AddressSet &addrs)
{
    std::memset(file.bytes, 0, addrs.fileObjectSize);
    reinterpret_cast<SetPathFn>(addrs.setPathAddr)(
        reinterpret_cast<char *>(file.bytes + addrs.pathBufferOffset), kSettingsPath,
        static_cast<unsigned>(addrs.pathBufferSize));
}

/// Reads the real current content of 2:/settings.json into `outBuf` (capacity
/// `kMaxSettingsFileSize`), refusing rather than guessing if the reported
/// size is 0 or larger than that cap.
Status readCurrentFile(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                        char *outBuf, size_t &outLen)
{
    RawFile file{};
    resetFile(file, addrs);

    const auto fileOpen = reinterpret_cast<FileOpenFn>(addrs.fileOpenAddr);
    const auto fileRead = reinterpret_cast<FileReadFn>(addrs.fileReadAddr);
    const auto fileClose = reinterpret_cast<FileCloseFn>(addrs.fileCloseAddr);
    const auto fileRelease = reinterpret_cast<FileReleaseFn>(addrs.fileReleaseAddr);

    if (fileOpen(file.self(), kOpenReadOnly, 0) == 0) {
        DebugLog::append(fs, "SettingsPersist: read-open failed");
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

    if (!readOk || bytesRead != fileSize) {
        DebugLog::appendf(fs, "SettingsPersist: read failed or short (ok=%d bytesRead=%u expected=%llu)",
                           readOk, bytesRead, static_cast<unsigned long long>(fileSize));
        return Status::ReadFailed;
    }

    outLen = static_cast<size_t>(fileSize);
    return Status::Ok;
}

/// Replaces the one `"notifications":true`/`"notifications":false` substring
/// in `buf` (length `*len`, capacity kBufferCapacity) with the value matching
/// `newEnabled`, updating `*len` for the (possible) 1-byte length delta.
/// Every other byte is left untouched. Refuses (FieldNotFound) rather than
/// writing anything if neither exact spelling is present -- this app's
/// format assumption not holding is a reason to stop, not to guess.
Status spliceNotificationsField(char *buf, size_t &len, bool newEnabled)
{
    constexpr const char *kFieldTrue  = "\"notifications\":true";
    constexpr const char *kFieldFalse = "\"notifications\":false";
    const size_t trueLen  = std::strlen(kFieldTrue);
    const size_t falseLen = std::strlen(kFieldFalse);

    buf[len] = '\0'; // safe: len < kMaxSettingsFileSize < kBufferCapacity, always room for +1

    char *found = std::strstr(buf, kFieldTrue);
    size_t oldNeedleLen = trueLen;
    if (!found) {
        found = std::strstr(buf, kFieldFalse);
        oldNeedleLen = falseLen;
    }
    if (!found) {
        return Status::FieldNotFound;
    }

    const char *replacement = newEnabled ? kFieldTrue : kFieldFalse;
    const size_t newNeedleLen = newEnabled ? trueLen : falseLen;

    const size_t prefixLen = static_cast<size_t>(found - buf);
    const size_t tailLen   = len - (prefixLen + oldNeedleLen);
    const size_t newLen    = len - oldNeedleLen + newNeedleLen;

    if (newLen > kBufferCapacity) {
        return Status::SizeOutOfRange;
    }

    std::memmove(found + newNeedleLen, found + oldNeedleLen, tailLen);
    std::memcpy(found, replacement, newNeedleLen);

    len = newLen;
    return Status::Ok;
}

Status writeWholeFile(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                       const char *buf, size_t len)
{
    RawFile file{};
    resetFile(file, addrs);

    const auto fileOpen = reinterpret_cast<FileOpenFn>(addrs.fileOpenAddr);
    const auto fileWrite = reinterpret_cast<FileWriteFn>(addrs.fileWriteAddr);
    const auto fileClose = reinterpret_cast<FileCloseFn>(addrs.fileCloseAddr);
    const auto fileRelease = reinterpret_cast<FileReleaseFn>(addrs.fileReleaseAddr);

    if (fileOpen(file.self(), kOpenCreateOrReplace, 1) == 0) {
        DebugLog::append(fs, "SettingsPersist: write-open failed");
        return Status::WriteOpenFailed;
    }

    uint32_t bytesWritten = 0;
    const int writeOk = fileWrite(file.self(), buf, static_cast<uint32_t>(len), &bytesWritten);
    fileClose(file.self());
    fileRelease(file.self());

    if (!writeOk || bytesWritten != len) {
        DebugLog::appendf(fs, "SettingsPersist: write failed or short (ok=%d bytesWritten=%u expected=%zu)",
                           writeOk, bytesWritten, len);
        return Status::WriteFailed;
    }

    return Status::Ok;
}

} // namespace

Status persistNotificationsFlag(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                                 bool newEnabled)
{
    if (!objectSizeInRange(addrs)) {
        DebugLog::append(fs, "SettingsPersist: AddressSet's File object layout is out of range -- refusing");
        return Status::SizeOutOfRange;
    }

    char buf[kBufferCapacity];
    size_t len = 0;

    Status status = readCurrentFile(fs, addrs, buf, len);
    if (status != Status::Ok) {
        return status;
    }
    DebugLog::appendBytes(fs, "SettingsPersist: read", buf, len);

    status = spliceNotificationsField(buf, len, newEnabled);
    if (status != Status::Ok) {
        DebugLog::append(fs, "SettingsPersist: field not found or splice out of range -- refusing to write");
        return status;
    }
    DebugLog::appendBytes(fs, "SettingsPersist: about to write", buf, len);

    status = writeWholeFile(fs, addrs, buf, len);
    if (status != Status::Ok) {
        return status;
    }

    char verifyBuf[kBufferCapacity];
    size_t verifyLen = 0;
    status = readCurrentFile(fs, addrs, verifyBuf, verifyLen);
    if (status != Status::Ok) {
        DebugLog::append(fs, "SettingsPersist: post-write readback failed");
        return Status::ReadbackMismatch;
    }

    if (verifyLen != len || std::memcmp(verifyBuf, buf, len) != 0) {
        DebugLog::append(fs, "SettingsPersist: post-write readback MISMATCH");
        return Status::ReadbackMismatch;
    }

    DebugLog::append(fs, "SettingsPersist: write verified OK");
    return Status::Ok;
}

} // namespace SettingsPersist
