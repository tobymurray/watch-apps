#include "SettingsPersist.hpp"

#include <cstdint>
#include <cstring>

#include "DebugLog.hpp"

namespace SettingsPersist
{

namespace
{

// See SettingsPersist.hpp for the full derivation and provenance. Valid only
// for kernel 1.4.0 on this exact unit. Bit 0 set on every function address:
// these are Thumb code, and a function pointer to Thumb code must carry the
// Thumb bit for BLX interworking to dispatch correctly.
constexpr uintptr_t kFileOpenAddr    = 0x0809b254u | 1u;
constexpr uintptr_t kFileReadAddr    = 0x0809b4e8u | 1u;
constexpr uintptr_t kFileWriteAddr   = 0x0809b334u | 1u;
constexpr uintptr_t kFileCloseAddr   = 0x0809b450u | 1u;
constexpr uintptr_t kFileReleaseAddr = 0x0809b2ccu | 1u;
constexpr uintptr_t kSetPathAddr     = 0x0802b3eau | 1u;

using FileOpenFn    = int (*)(void *self, int flag1, int flag2);
using FileReadFn    = int (*)(void *self, void *buf, uint32_t len, uint32_t *bytesReadOut);
using FileWriteFn   = int (*)(void *self, const void *buf, uint32_t len, uint32_t *bytesWrittenOut);
using FileCloseFn   = void (*)(void *self);
using FileReleaseFn = void (*)(void *self);
using SetPathFn     = char *(*)(char *dst, const char *src, unsigned maxlen);

FileOpenFn fileOpen        = reinterpret_cast<FileOpenFn>(kFileOpenAddr);
FileReadFn fileRead        = reinterpret_cast<FileReadFn>(kFileReadAddr);
FileWriteFn fileWrite      = reinterpret_cast<FileWriteFn>(kFileWriteAddr);
FileCloseFn fileClose      = reinterpret_cast<FileCloseFn>(kFileCloseAddr);
FileReleaseFn fileRelease  = reinterpret_cast<FileReleaseFn>(kFileReleaseAddr);
SetPathFn setPath          = reinterpret_cast<SetPathFn>(kSetPathAddr);

// File object layout (856 bytes / 0x358, confirmed via a constructor-shaped
// helper calling a zero-fill helper with literal 856). The vtable slot at
// +0x00 is deliberately left zero -- safe as long as nothing here ever calls
// the one File method (setPath, 0x0809b4a8) that dispatches through it. We
// use the plain bounded-copy helper (0x0802b3ea) to set the path instead,
// which has no vtable dependency.
constexpr size_t kFileObjectSize      = 856;
constexpr size_t kPathBufferOffset    = 0x04;
constexpr size_t kPathBufferSize      = 0x100; // 256 bytes, +0x04..+0x103
constexpr size_t kFileSizeFieldOffset = 0x118; // FIL.fsize, 8-byte FSIZE_t

// open()'s (flag1, flag2) -> FatFs mode, confirmed:
//   (0, _) -> FA_READ
//   (1, 1) -> FA_CREATE_ALWAYS | FA_WRITE | FA_READ
constexpr int kOpenReadOnly       = 0;
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

/// A raw, zero-initialized stand-in for the kernel's internal `File` object.
/// Never constructed or destructed as a real C++ object -- just a correctly
/// sized, correctly aligned byte buffer passed as `this` to the confirmed
/// non-virtual methods above. 8-byte aligned because the FIL region embeds a
/// 64-bit FSIZE_t field.
struct alignas(8) RawFile {
    uint8_t bytes[kFileObjectSize];

    void *self() { return bytes; }
};

void setFilePath(RawFile &file)
{
    setPath(reinterpret_cast<char *>(file.bytes + kPathBufferOffset), kSettingsPath, kPathBufferSize);
}

/// Reads the real current content of 2:/settings.json into `outBuf` (capacity
/// `kMaxSettingsFileSize`), refusing rather than guessing if the reported
/// size is 0 or larger than that cap.
Status readCurrentFile(SDK::Interface::IFileSystem &fs, char *outBuf, size_t &outLen)
{
    RawFile file{};
    std::memset(file.bytes, 0, sizeof(file.bytes));
    setFilePath(file);

    if (fileOpen(file.self(), kOpenReadOnly, 0) == 0) {
        DebugLog::append(fs, "SettingsPersist: read-open failed");
        return Status::ReadOpenFailed;
    }

    uint64_t fileSize = 0;
    std::memcpy(&fileSize, file.bytes + kFileSizeFieldOffset, sizeof(fileSize));

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

Status writeWholeFile(SDK::Interface::IFileSystem &fs, const char *buf, size_t len)
{
    RawFile file{};
    std::memset(file.bytes, 0, sizeof(file.bytes));
    setFilePath(file);

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

Status persistNotificationsFlag(SDK::Interface::IFileSystem &fs, bool newEnabled)
{
    char buf[kBufferCapacity];
    size_t len = 0;

    Status status = readCurrentFile(fs, buf, len);
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

    status = writeWholeFile(fs, buf, len);
    if (status != Status::Ok) {
        return status;
    }

    char verifyBuf[kBufferCapacity];
    size_t verifyLen = 0;
    status = readCurrentFile(fs, verifyBuf, verifyLen);
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
