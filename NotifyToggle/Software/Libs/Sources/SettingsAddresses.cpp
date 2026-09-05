#include "SettingsAddresses.hpp"


namespace SettingsAddresses
{

namespace
{

// Kernel 1.4.0, derived by hand from one physical unit. SettingsAddresses.hpp
// says what is and is not written down about these values.
//
// Designated, not positional: a positional initializer binds by position and
// the member names are only comments, so inserting a member in the header
// silently shifts every value past it into the wrong member.
// Generated from the CRC32-0x14009D03 flash image; see SettingsAddresses.hpp.
constexpr Signature kSignatures_1_4_0[] = {
    // Open
    {0x0809b254u, {0xF0, 0xB5, 0x05, 0x46, 0x85, 0xB0, 0x00, 0xF5, 0x84, 0x77, 0x06, 0x1D, 0x0A, 0xB3, 0x00, 0x29}},
    // Read
    {0x0809b4e8u, {0x1F, 0xB5, 0x29, 0xB9, 0x85, 0x21, 0x0F, 0x4B, 0x0F, 0x4A, 0x10, 0x48, 0xCF, 0xF7, 0x0A, 0xFE}},
    // Write
    {0x0809b334u, {0x30, 0xB5, 0x14, 0x46, 0x1D, 0x46, 0x85, 0xB0, 0x29, 0xB9, 0x95, 0x21, 0x0F, 0x4B, 0x10, 0x4A}},
    // Close
    {0x0809b450u, {0x1F, 0xB5, 0x00, 0xF5, 0x84, 0x70, 0x36, 0xF0, 0x61, 0xFA, 0x88, 0xB1, 0x09, 0x4B, 0x01, 0x22}},
    // Release
    {0x0809b2ccu, {0x30, 0xB5, 0x90, 0xF8, 0x04, 0x31, 0x04, 0x46, 0x85, 0xB0, 0x03, 0xB3, 0x00, 0xF5, 0x84, 0x75}},
    // SetPath
    {0x0802b3eau, {0x01, 0x39, 0x03, 0x46, 0x10, 0xB5, 0x32, 0xB1, 0x11, 0xF8, 0x01, 0x4F, 0x01, 0x3A, 0x03, 0xF8}},
    // Exists
    {0x0809b5a0u, {0x10, 0xB5, 0x04, 0x46, 0x28, 0xB9, 0xD8, 0x21, 0x06, 0x4B, 0x07, 0x4A, 0x07, 0x48, 0xCF, 0xF7}},
    // Delete
    {0x0809b648u, {0x10, 0xB5, 0x04, 0x46, 0x28, 0xB9, 0xE6, 0x21, 0x06, 0x4B, 0x07, 0x4A, 0x07, 0x48, 0xCF, 0xF7}},
    // Rename
    {0x0809b5d8u, {0x70, 0xB5, 0x0D, 0x46, 0x04, 0x46, 0x28, 0xB9, 0xDE, 0x21, 0x09, 0x4B, 0x09, 0x4A, 0x0A, 0x48}},
};

constexpr AddressSet kFirmware_1_4_0 = {
    .abi                      = 3u,
    .derivedFrom              = "1.4.0",
    .settingsStructBase       = 0x20010cb0u,
    .phoneNotificationsOffset = 5u,
    .watchFaceIdOffset        = 8u,
    .fileOpenAddr             = 0x0809b254u | 1u,
    .fileReadAddr             = 0x0809b4e8u | 1u,
    .fileWriteAddr            = 0x0809b334u | 1u,
    .fileCloseAddr            = 0x0809b450u | 1u,
    .fileReleaseAddr          = 0x0809b2ccu | 1u,
    .setPathAddr              = 0x0802b3eau | 1u,
    // Real-caller counts across the full 4MB image: exists 55, delete 41,
    // rename 21 -- general kernel primitives, not anything Settings-specific.
    .fileExistsAddr           = 0x0809b5a0u | 1u,
    .fileDeleteAddr           = 0x0809b648u | 1u,
    .fileRenameAddr           = 0x0809b5d8u | 1u,
    .fileObjectSize           = 856u,
    .pathBufferOffset         = 0x04u,
    .pathBufferSize           = 0x100u,
    .fileSizeFieldOffset      = 0x118u,
    .signatures               = kSignatures_1_4_0,
    .signatureCount           = sizeof(kSignatures_1_4_0) / sizeof(kSignatures_1_4_0[0]),
};

// This part's memory map: code executes from the 4MB flash bank at
// 0x08000000, and the kernel's live structs sit in SRAM at 0x20000000.
constexpr uintptr_t kFlashBase = 0x08000000u;
constexpr uintptr_t kFlashEnd  = 0x08400000u;
constexpr uintptr_t kSramBase  = 0x20000000u;
constexpr uintptr_t kSramEnd   = 0x20080000u;

// Generous: this bounds a field offset within one struct, and exists only to
// separate an offset from an address that landed in an offset's member.
constexpr size_t kMaxStructOffset = 4096u;

constexpr bool isThumbCode(uintptr_t addr)
{
    return (addr & 1u) != 0u && addr >= kFlashBase && addr < kFlashEnd;
}

/// True if every member of `a` holds the *kind* of value its name promises --
/// a callable Thumb address where an address belongs, a small self-consistent
/// offset where an offset belongs. Not a claim that any address is correct;
/// only that a value has not landed in the wrong member.
/// True if every signature fingerprints the address it is filed under, and
/// there is one for each function this app calls. Without this a row could
/// carry signatures for a different build's addresses and still verify.
constexpr bool signaturesPairWithAddresses(const AddressSet &a)
{
    constexpr uintptr_t kNoThumb = ~static_cast<uintptr_t>(1);
    return a.signatures != nullptr && a.signatureCount == kSignatureCount &&
           a.signatures[kSigOpen].address    == (a.fileOpenAddr & kNoThumb) &&
           a.signatures[kSigRead].address    == (a.fileReadAddr & kNoThumb) &&
           a.signatures[kSigWrite].address   == (a.fileWriteAddr & kNoThumb) &&
           a.signatures[kSigClose].address   == (a.fileCloseAddr & kNoThumb) &&
           a.signatures[kSigRelease].address == (a.fileReleaseAddr & kNoThumb) &&
           a.signatures[kSigSetPath].address == (a.setPathAddr & kNoThumb) &&
           a.signatures[kSigExists].address  == (a.fileExistsAddr & kNoThumb) &&
           a.signatures[kSigDelete].address  == (a.fileDeleteAddr & kNoThumb) &&
           a.signatures[kSigRename].address  == (a.fileRenameAddr & kNoThumb);
}

constexpr bool isWellFormed(const AddressSet &a)
{
    return a.abi > 0u && a.derivedFrom != nullptr && signaturesPairWithAddresses(a) &&
           a.settingsStructBase >= kSramBase && a.settingsStructBase < kSramEnd &&
           a.phoneNotificationsOffset < kMaxStructOffset &&
           a.watchFaceIdOffset < kMaxStructOffset &&
           isThumbCode(a.fileOpenAddr) && isThumbCode(a.fileReadAddr) &&
           isThumbCode(a.fileWriteAddr) && isThumbCode(a.fileCloseAddr) &&
           isThumbCode(a.fileReleaseAddr) && isThumbCode(a.setPathAddr) &&
           isThumbCode(a.fileExistsAddr) && isThumbCode(a.fileDeleteAddr) &&
           isThumbCode(a.fileRenameAddr) &&
           a.fileObjectSize > 0u &&
           a.pathBufferSize > 0u &&
           a.pathBufferOffset + a.pathBufferSize <= a.fileObjectSize &&
           a.fileSizeFieldOffset + sizeof(uint64_t) <= a.fileObjectSize;
}

// One row per firmware version that has actually had this investigation's
// RE process run against it, cross-checked and verified live. Add a row only
// after doing that work -- never by extrapolating from a neighboring version.
constexpr const AddressSet *kSupported[] = {
    &kFirmware_1_4_0,
};

constexpr bool everyEntryIsWellFormed()
{
    for (const auto *entry : kSupported) {
        if (entry == nullptr || !isWellFormed(*entry)) {
            return false;
        }
    }
    return true;
}

static_assert(everyEntryIsWellFormed(),
              "A table entry has a value in the wrong member, or its signatures do not "
              "fingerprint its own addresses: an address where an offset belongs, an offset "
              "where an address belongs, a File layout that does not fit its own object "
              "size, or a signature filed under the wrong function.");

constexpr bool everyAbiAppearsOnce()
{
    for (size_t i = 0; i < sizeof(kSupported) / sizeof(kSupported[0]); ++i) {
        for (size_t j = i + 1; j < sizeof(kSupported) / sizeof(kSupported[0]); ++j) {
            if (kSupported[i]->abi == kSupported[j]->abi) {
                return false;
            }
        }
    }
    return true;
}

static_assert(everyAbiAppearsOnce(),
              "Two rows share an ABI. Nothing this app can read at runtime tells those "
              "firmware versions apart, so the second row could never be selected; "
              "distinguishing them needs byte signatures at each address.");

} // namespace

const AddressSet *resolve(uint32_t abi)
{
    for (const auto *entry : kSupported) {
        if (entry->abi == abi) {
            return entry;
        }
    }
    return nullptr;
}

} // namespace SettingsAddresses
