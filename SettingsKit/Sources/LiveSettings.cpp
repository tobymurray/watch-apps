#include "LiveSettings.hpp"

#include <cstdint>

#include "DebugLog.hpp"

namespace LiveSettings
{

namespace
{

// A bound, not a value: the wearer picks the face, so this only catches an
// address that plainly is not the settings struct.
constexpr uint64_t kWatchFaceIdSanityMax = 1000u;

volatile uint8_t &flagByte(const SettingsAddresses::AddressSet &addrs, const Flag &flag)
{
    const uintptr_t addr = addrs.settingsStructBase + flag.offset;
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

/// Never writes: both public functions need the same refusals before either
/// can trust the byte.
Status readChecked(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                   const Flag &flag, uint8_t &outRaw)
{
    const uint8_t raw = flagByte(addrs, flag);
    const uint64_t watchFaceId = readWatchFaceIdRaw(addrs);

    DebugLog::appendf(fs, "LiveSettings: %s raw=0x%02X (addr=0x%08X) watchFaceId=%llu (addr=0x%08X)",
                       flag.name, raw,
                       static_cast<unsigned>(addrs.settingsStructBase + flag.offset),
                       static_cast<unsigned long long>(watchFaceId),
                       static_cast<unsigned>(addrs.settingsStructBase + addrs.watchFaceIdOffset));

    if (watchFaceId > kWatchFaceIdSanityMax) {
        DebugLog::append(fs, "LiveSettings: watchFaceId cross-check out of range -- refusing to trust this address");
        return Status::CrossCheckOutOfRange;
    }

    if (raw != 0 && raw != 1) {
        DebugLog::appendf(fs, "LiveSettings: %s byte is not 0/1 -- refusing to trust this address",
                           flag.name);
        return Status::UnexpectedCurrentValue;
    }

    outRaw = raw;
    return Status::Ok;
}

} // namespace

Status readFlag(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                const Flag &flag, bool &outEnabled)
{
    uint8_t raw = 0;
    const Status status = readChecked(fs, addrs, flag, raw);
    if (status != Status::Ok) {
        return status;
    }
    outEnabled = (raw != 0);
    return Status::Ok;
}

Status writeFlag(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                 const Flag &flag, bool newEnabled)
{
    uint8_t raw = 0;
    const Status status = readChecked(fs, addrs, flag, raw);
    if (status != Status::Ok) {
        return status;
    }

    const bool current = (raw != 0);
    if (current == newEnabled) {
        DebugLog::append(fs, "LiveSettings: already the requested value; not writing");
        return Status::NoChange;
    }

    const uint8_t newRaw = newEnabled ? 1 : 0;
    DebugLog::appendf(fs, "LiveSettings: writing %s raw=0x%02X to addr=0x%08X",
                       flag.name, newRaw,
                       static_cast<unsigned>(addrs.settingsStructBase + flag.offset));
    flagByte(addrs, flag) = newRaw;

    const uint8_t readBack = flagByte(addrs, flag);
    DebugLog::appendf(fs, "LiveSettings: readback raw=0x%02X", readBack);
    if (readBack != newRaw) {
        DebugLog::append(fs, "LiveSettings: readback mismatch after write");
        return Status::ReadbackMismatch;
    }

    return Status::Ok;
}

namespace
{
uint32_t readU32(const SettingsAddresses::AddressSet &addrs, size_t offset)
{
    const volatile uint8_t *src =
        reinterpret_cast<volatile uint8_t *>(addrs.settingsStructBase + offset);
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(src[i]) << (8 * i);
    }
    return value;
}
} // namespace

bool matchesKernel(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs,
                   uint32_t kernelActivityMinutes, uint32_t kernelSteps)
{
    const uint32_t liveActivity = readU32(addrs, addrs.activityMinutesOffset);
    const uint32_t liveSteps    = readU32(addrs, addrs.stepsOffset);

    const bool activityAgrees = (liveActivity == kernelActivityMinutes);
    const bool stepsAgrees    = (liveSteps == kernelSteps);

    DebugLog::appendf(fs,
                       "cross-check: activityMinutes raw=%lu kernel=%lu (%s), steps raw=%lu kernel=%lu (%s)",
                       static_cast<unsigned long>(liveActivity),
                       static_cast<unsigned long>(kernelActivityMinutes),
                       activityAgrees ? "agree" : "DISAGREE",
                       static_cast<unsigned long>(liveSteps),
                       static_cast<unsigned long>(kernelSteps),
                       stepsAgrees ? "agree" : "DISAGREE");

    return activityAgrees && stepsAgrees;
}

} // namespace LiveSettings
