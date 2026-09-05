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

/// The addresses this app may use on the running firmware, or nullptr if that
/// could not be established -- in which case nothing raw may be read or
/// written, not even a read.
///
/// `wantsPersistence` says whether the wearer asked for settings.json to be
/// written (AppConfigFields.hpp). With it false the scratch-file check is
/// skipped, because reading settings.json and finding it agrees with the live
/// struct already proves every primitive that mode uses -- and skipping it is
/// what makes the default configuration write nothing to the watch at all.
const SettingsAddresses::AddressSet *resolve(SDK::Interface::IFileSystem &fs, uint32_t kernelAbi,
                                             bool wantsPersistence);

} // namespace FirmwareGate

#endif // FIRMWARE_GATE_HPP
