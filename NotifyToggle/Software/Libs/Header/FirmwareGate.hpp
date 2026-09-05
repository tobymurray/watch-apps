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
 * An ABI is a floor, not an identity -- abi_kernel_map.json maps one to the
 * *minimum* firmware providing it, so ABI 3 is 1.4.0 or anything later that
 * kept the interface. So the ABI only selects a candidate row, and three
 * further checks have to pass before anything is trusted:
 *
 *   1. the bytes at each address are the ones recorded from the firmware the
 *      row was derived against -- read, never called
 *   2. the File primitives behave, proved against scratch paths
 *   3. the live struct agrees with `2:/settings.json` on two fields
 *
 * Check 1 comes first for the reason the whole ordering exists: 2 and 3 prove
 * the addresses by using them, and on a part with no MPU a wrong address does
 * not return an error, it runs. Reading the bytes first is what makes a
 * firmware this app has never seen a clean refusal rather than a jump into
 * whatever now lives there.
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
