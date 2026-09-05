#include "FirmwareGate.hpp"

#include "SDK/Interfaces/IKernel.hpp"

#include <cstring>

#include "DebugLog.hpp"
#include "LiveSettings.hpp"
#include "SettingsPersist.hpp"
#include "SettingsSplice.hpp"

namespace FirmwareGate
{

namespace
{

// The 4MB bank this part executes from: a signature is a load, so it is
// bounded to somewhere a load belongs.
constexpr uintptr_t kFlashBase = 0x08000000u;
constexpr uintptr_t kFlashEnd  = 0x08400000u;

/// Compares the bytes at each address against what was recorded from the
/// firmware the row was derived on. Nothing is called, which is what lets this
/// refuse a moved function before anything executes.
bool signaturesMatch(SDK::Interface::IFileSystem &fs, const SettingsAddresses::AddressSet &addrs)
{
    for (size_t i = 0; i < addrs.signatureCount; ++i) {
        const SettingsAddresses::Signature &sig = addrs.signatures[i];

        if (sig.address < kFlashBase ||
            sig.address + SettingsAddresses::kSignatureBytes > kFlashEnd) {
            DebugLog::appendf(fs, "signature %u: address 0x%08lX is outside flash -- refusing",
                               static_cast<unsigned>(i), static_cast<unsigned long>(sig.address));
            return false;
        }

        const uint8_t *found = reinterpret_cast<const uint8_t *>(sig.address);
        if (std::memcmp(found, sig.bytes, SettingsAddresses::kSignatureBytes) != 0) {
            DebugLog::appendf(fs,
                               "signature %u at 0x%08lX MISMATCH: expected %02X %02X %02X %02X, found %02X %02X %02X %02X",
                               static_cast<unsigned>(i), static_cast<unsigned long>(sig.address),
                               sig.bytes[0], sig.bytes[1], sig.bytes[2], sig.bytes[3],
                               found[0], found[1], found[2], found[3]);
            return false;
        }
    }
    DebugLog::appendf(fs, "signatures: all %u match", static_cast<unsigned>(addrs.signatureCount));
    return true;
}

} // namespace

const SettingsAddresses::AddressSet *resolve(SDK::Interface::IFileSystem &fs, uint32_t kernelAbi,
                                             bool wantsPersistence)
{
    DebugLog::appendf(fs, "gate: kernel ABI %lu, built against %d, saving %s",
                       static_cast<unsigned long>(kernelAbi), KERNEL_INTERFACE_VERSION,
                       wantsPersistence ? "ON" : "off");

    // system.cpp already exits on an ABI below this, so the case left is a
    // newer interface, which none of these addresses were derived against.
    if (kernelAbi != static_cast<uint32_t>(KERNEL_INTERFACE_VERSION)) {
        DebugLog::append(fs, "gate: ABI is not the one this build was made for -- refusing");
        return nullptr;
    }

    const SettingsAddresses::AddressSet *candidate = SettingsAddresses::resolve(kernelAbi);
    if (candidate == nullptr) {
        DebugLog::append(fs, "gate: no address row for this ABI -- refusing");
        return nullptr;
    }
    DebugLog::appendf(fs, "gate: candidate row derived on firmware %s", candidate->derivedFrom);

    // An ABI is shared by every firmware version that ships it, so this is what
    // tells one of them from another -- and it runs before anything is called.
    if (!signaturesMatch(fs, *candidate)) {
        DebugLog::append(fs, "gate: this is not the firmware those addresses came from -- refusing");
        return nullptr;
    }

    // Reading the file below already exercises everything the read-only mode
    // calls, so only the mode that writes has to prove the write path.
    if (wantsPersistence) {
        if (!SettingsPersist::validatePrimitives(fs, *candidate)) {
            DebugLog::append(fs, "gate: the File primitives did not behave -- refusing");
            return nullptr;
        }
    } else {
        DebugLog::append(fs, "gate: saving is off, so the write path is neither used nor tested");
    }

    char buf[SettingsPersist::kBufferCapacity];
    size_t len = 0;
    if (SettingsPersist::readSettingsFile(fs, *candidate, buf, len) != SettingsPersist::Status::Ok) {
        DebugLog::append(fs, "gate: could not read settings.json to cross-check -- refusing");
        return nullptr;
    }

    bool fileNotifications = false;
    uint32_t fileWatchFaceId = 0;
    if (!SettingsSplice::readNotifications(buf, len, fileNotifications) ||
        !SettingsSplice::readUnsigned(buf, len, "watchFaceId", 11, fileWatchFaceId)) {
        DebugLog::append(fs, "gate: settings.json is not the shape this app understands -- refusing");
        return nullptr;
    }

    if (!LiveSettings::matchesFile(fs, *candidate, fileNotifications, fileWatchFaceId)) {
        DebugLog::append(fs, "gate: the live struct disagrees with the file -- refusing");
        return nullptr;
    }

    DebugLog::append(fs, "gate: firmware accepted");
    return candidate;
}

} // namespace FirmwareGate
