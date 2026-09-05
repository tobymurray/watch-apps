/**
 ******************************************************************************
 * @file    SettingsAddresses.hpp
 * @brief   Per-firmware-version table of the raw addresses LiveSettings.cpp
 *          and SettingsPersist.cpp need. Not a supported SDK mechanism.
 ******************************************************************************
 *
 * There is no formula from a version number to an address, only a
 * reverse-engineering pass per firmware build, so this is a table with one
 * row per version that has actually had that pass run against it, and a
 * lookup that returns nothing -- never a guess -- for anything else.
 *
 * `Docs/Investigations/2026-08-31-live-settings-persistence/FINDINGS.md` is
 * what derives the live-struct entries; the rest were traced from the flash
 * image that `signatures` fingerprints.
 *
 * One row per ABI, asserted below: nothing this app can read at runtime tells
 * two firmware versions sharing an ABI apart.
 ******************************************************************************
 */

#ifndef SETTINGS_ADDRESSES_HPP
#define SETTINGS_ADDRESSES_HPP

#include <cstddef>
#include <cstdint>

namespace SettingsAddresses
{

/// Bytes this app expects to find at one address, so a firmware that moved the
/// function can be refused by *reading* rather than by calling it. 16 bytes is
/// about eight Thumb instructions -- every one of these is unique in the 4MB
/// image except setPath's, which appears twice, and that costs nothing here
/// because this verifies a known address rather than searching for one.
constexpr size_t kSignatureBytes = 16;

struct Signature {
    uintptr_t address;   ///< Thumb bit cleared: this is a load, not a call.
    uint8_t   bytes[kSignatureBytes];
};

/// Fixed order, so a row's signatures can be checked against the addresses they
/// claim to fingerprint rather than merely counted.
enum SignatureIndex {
    kSigOpen, kSigRead, kSigWrite, kSigClose, kSigRelease,
    kSigSetPath, kSigExists, kSigDelete, kSigRename,
    kSignatureCount
};

/// Everything LiveSettings.cpp and SettingsPersist.cpp need, for one exact
/// firmware build. Addresses for the internal `File` class methods already
/// carry the Thumb bit (see SettingsPersist.hpp) -- callable as-is.
struct AddressSet {
    /// The KERNEL_INTERFACE_VERSION this row was derived under, and the key it
    /// is looked up by: it is the only version the kernel exposes to a running
    /// app (`gIKernel->version`; REQUEST_SYSTEM_INFO is declared in the SDK
    /// headers but answered FAIL by kernel 1.4.0, confirmed on a watch).
    uint32_t abi;

    /// The firmware the row was actually derived and verified on. Provenance
    /// for logs, never a lookup key -- one ABI spans several firmware versions
    /// (abi_kernel_map.json maps an ABI to the *minimum* firmware providing
    /// it), so this cannot be recovered at runtime.
    const char *derivedFrom;

    // --- LiveSettings: the kernel's live, in-RAM WatchSettings struct ---
    uintptr_t settingsStructBase;
    size_t    phoneNotificationsOffset;
    size_t    watchFaceIdOffset;

    // --- SettingsPersist: the kernel's internal, non-virtual File class ---
    uintptr_t fileOpenAddr;
    uintptr_t fileReadAddr;
    uintptr_t fileWriteAddr;
    uintptr_t fileCloseAddr;
    uintptr_t fileReleaseAddr;
    uintptr_t setPathAddr;

    // --- SettingsPersist: general-purpose kernel file utilities, used for
    // the tmp-file + backup-rotate + rename atomic commit pattern. Each
    // takes a plain path string, not a File object -- these delegate
    // through a filesystem singleton the kernel resolves internally on
    // every call, not something this app has to manage. ---
    uintptr_t fileExistsAddr;
    uintptr_t fileDeleteAddr;
    uintptr_t fileRenameAddr;

    // File object layout (may differ across builds even if the functions
    // above land at different addresses but keep the same shape -- kept
    // explicit rather than assumed constant).
    size_t fileObjectSize;
    size_t pathBufferOffset;
    size_t pathBufferSize;
    size_t fileSizeFieldOffset;

    // --- FirmwareGate: what has to be at those addresses before any of them
    // is called. settingsStructBase has no entry: it is RAM, whose contents
    // are the wearer's settings rather than a fixed pattern, so the file
    // cross-check is its evidence instead. ---
    const Signature *signatures;
    size_t           signatureCount;
};

/// The row derived under kernel ABI `abi`, or nullptr if none. An ABI is a
/// coarse key -- it spans every firmware version that ships that interface --
/// so a row it returns is a candidate, not a verdict: FirmwareGate.hpp then
/// has to prove the addresses actually behave before anything calls them.
const AddressSet *resolve(uint32_t abi);

} // namespace SettingsAddresses

#endif // SETTINGS_ADDRESSES_HPP
