#include "FirmwareGate.hpp"

#include "SDK/Interfaces/IKernel.hpp"

#include <cstring>

#include "DebugLog.hpp"
#include "LiveSettings.hpp"
#include "SDK/Messages/CommandMessages.hpp"

#include "SettingsPersist.hpp"

namespace FirmwareGate
{

namespace
{

constexpr uint32_t kSettingsTimeoutMs = 1000;

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

const SettingsAddresses::AddressSet *resolve(SDK::Kernel &kernel, uint32_t kernelAbi,
                                             Outcome &outcome)
{
    outcome = Outcome::UnknownFirmware;
    SDK::Interface::IFileSystem &fs = kernel.fs;

    DebugLog::appendf(fs, "gate: kernel ABI %lu, built against %d",
                       static_cast<unsigned long>(kernelAbi), KERNEL_INTERFACE_VERSION);

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

    auto *settings = kernel.comm.allocateMessage<SDK::Message::RequestSystemSettings>();
    if (settings == nullptr) {
        DebugLog::append(fs, "gate: could not allocate RequestSystemSettings -- refusing");
        outcome = Outcome::SettingsUnreadable;
        return nullptr;
    }
    const bool answered = kernel.comm.sendMessage(settings, kSettingsTimeoutMs) &&
                          settings->getResult() == SDK::MessageResult::SUCCESS;
    const uint32_t kernelActivity = settings->activityMin;
    const uint32_t kernelSteps    = settings->steps;
    kernel.comm.releaseMessage(settings);

    if (!answered) {
        DebugLog::append(fs, "gate: the kernel would not report its settings -- refusing");
        outcome = Outcome::SettingsUnreadable;
        return nullptr;
    }

    if (!LiveSettings::matchesKernel(fs, *candidate, kernelActivity, kernelSteps)) {
        DebugLog::append(fs, "gate: the struct does not hold what the kernel reports -- refusing");
        outcome = Outcome::SettingsUnreadable;
        return nullptr;
    }

    DebugLog::append(fs, "gate: firmware accepted");
    outcome = Outcome::Accepted;
    return candidate;
}

} // namespace FirmwareGate
