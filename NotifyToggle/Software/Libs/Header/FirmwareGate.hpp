/**
 ******************************************************************************
 * @file    FirmwareGate.hpp
 * @brief   Decides whether this watch's firmware is one this app may touch
 *          through raw addresses, and refuses when it cannot prove it.
 ******************************************************************************
 *
 * The kernel will not name its own version: REQUEST_SYSTEM_INFO is declared in
 * the SDK headers and answered FAIL by kernel 1.4.0 (observed on a watch;
 * REQUEST_SYSTEM_SETTINGS succeeds in the same run, so it is that message and
 * not the mechanism). What it does expose is `gIKernel->version`, the ABI the
 * loader patched in.
 *
 * An ABI is a floor rather than an identity: `abi_kernel_map.json` maps one to
 * the *minimum* firmware providing it, so the row an ABI selects is a
 * candidate that the checks in `resolve` still have to prove.
 *
 * Those checks run in the order they do because this part has no MPU: a wrong
 * address does not return an error, it runs. Comparing the recorded bytes is a
 * load, so it can go first and make an unrecognised firmware a refusal rather
 * than a jump into whatever now lives there.
 ******************************************************************************
 */

#ifndef FIRMWARE_GATE_HPP
#define FIRMWARE_GATE_HPP

#include <cstdint>

#include "SDK/Interfaces/IFileSystem.hpp"

#include "SettingsAddresses.hpp"

namespace FirmwareGate
{

/// Why `resolve` answered as it did. The two failures are different problems
/// for the wearer: one is a firmware this app cannot work with, the other is a
/// firmware it can and a settings file it cannot.
enum class Outcome {
    Accepted,
    UnknownFirmware,
    SettingsUnreadable,
};

/// The addresses this app may use on the running firmware, or nullptr if that
/// could not be established -- in which case nothing raw may be read or
/// written, not even a read.
///
/// Reads only. Proving the write primitives is left to the first write, so
/// launching the app touches nothing on the watch -- see
/// SettingsPersist::validatePrimitives.
const SettingsAddresses::AddressSet *resolve(SDK::Interface::IFileSystem &fs, uint32_t kernelAbi,
                                             Outcome &outcome);

} // namespace FirmwareGate

#endif // FIRMWARE_GATE_HPP
