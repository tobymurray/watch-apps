#include "LiveSettings.hpp"

#include <cstdint>

#include "DebugLog.hpp"

namespace LiveSettings
{

namespace
{

// watchFaceId is a small selection index, not a large/garbage-looking
// 64-bit value; settings.json read "watchFaceId":0 on this unit. This is a
// sanity bound, not an exact match -- the user can pick a different face --
// so it exists only to catch "this address plainly isn't what we think it
// is", not to assert a specific value. Not part of AddressSet: it's a
// validation constant, not a derived address, and stays the same shape of
// check regardless of which firmware's addresses are in play.
constexpr uint64_t kWatchFaceIdSanityMax = 1000u;

volatile uint8_t &notificationsByte(const SettingsAddresses::AddressSet &addrs)
{
    const uintptr_t addr = addrs.settingsStructBase + addrs.phoneNotificationsOffset;
    return *reinterpret_cast<volatile uint8_t *>(addr);
}

uint64_t readWatchFaceIdRaw(const SettingsAddresses::AddressSet &addrs)
{
    const uintptr_t addr = addrs.settingsStructBase + addrs.watchFaceIdOffset;
    uint64_t value = 0;
    const volatile uint8_t *src = reinterpret_cast<volatile uint8_t *>(addr);
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(src[i]) << (8 * i);
    }
    return value;
}

/// Reads the live byte and cross-check field, and applies the same
/// fail-closed checks readNotificationsFlag and writeNotificationsFlag both
/// need. Never writes anything.
Status readChecked(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs, uint8_t &outRaw)
{
    const uint8_t raw = notificationsByte(addrs);
    const uint64_t watchFaceId = readWatchFaceIdRaw(addrs);

    DebugLog::appendf(fs, "LiveSettings: read raw=0x%02X (addr=0x%08X) watchFaceId=%llu (addr=0x%08X)",
                       raw, static_cast<unsigned>(addrs.settingsStructBase + addrs.phoneNotificationsOffset),
                       static_cast<unsigned long long>(watchFaceId),
                       static_cast<unsigned>(addrs.settingsStructBase + addrs.watchFaceIdOffset));

    if (watchFaceId > kWatchFaceIdSanityMax) {
        DebugLog::append(fs, "LiveSettings: watchFaceId cross-check out of range -- refusing to trust this address");
        return Status::CrossCheckOutOfRange;
    }

    if (raw != 0 && raw != 1) {
        DebugLog::append(fs, "LiveSettings: notifications byte is not 0/1 -- refusing to trust this address");
        return Status::UnexpectedCurrentValue;
    }

    outRaw = raw;
    return Status::Ok;
}

} // namespace

Status readNotificationsFlag(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs, bool &outEnabled)
{
    uint8_t raw = 0;
    const Status status = readChecked(fs, addrs, raw);
    if (status != Status::Ok) {
        return status;
    }
    outEnabled = (raw != 0);
    return Status::Ok;
}

Status writeNotificationsFlag(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs, bool newEnabled)
{
    uint8_t raw = 0;
    const Status status = readChecked(fs, addrs, raw);
    if (status != Status::Ok) {
        return status;
    }

    const bool current = (raw != 0);
    if (current == newEnabled) {
        DebugLog::append(fs, "LiveSettings: already the requested value; not writing");
        return Status::NoChange;
    }

    const uint8_t newRaw = newEnabled ? 1 : 0;
    DebugLog::appendf(fs, "LiveSettings: writing raw=0x%02X to addr=0x%08X",
                       newRaw, static_cast<unsigned>(addrs.settingsStructBase + addrs.phoneNotificationsOffset));
    notificationsByte(addrs) = newRaw;

    const uint8_t readBack = notificationsByte(addrs);
    DebugLog::appendf(fs, "LiveSettings: readback raw=0x%02X", readBack);
    if (readBack != newRaw) {
        DebugLog::append(fs, "LiveSettings: readback mismatch after write");
        return Status::ReadbackMismatch;
    }

    return Status::Ok;
}

bool matchesFile(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                 bool fileNotifications, uint32_t fileWatchFaceId)
{
    const uint8_t rawFlag = notificationsByte(addrs);
    const uint64_t liveFace = readWatchFaceIdRaw(addrs);

    const bool flagAgrees = (rawFlag == (fileNotifications ? 1u : 0u));
    const bool faceAgrees = (liveFace == fileWatchFaceId);

    DebugLog::appendf(fs,
                       "cross-check: notifications live=%u file=%u (%s), watchFaceId live=%llu file=%lu (%s)",
                       rawFlag, fileNotifications ? 1u : 0u, flagAgrees ? "agree" : "DISAGREE",
                       static_cast<unsigned long long>(liveFace),
                       static_cast<unsigned long>(fileWatchFaceId),
                       faceAgrees ? "agree" : "DISAGREE");

    return flagAgrees && faceAgrees;
}

} // namespace LiveSettings
