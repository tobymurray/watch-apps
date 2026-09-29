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
    // Derived in Docs/2026-09-06-units-offset.md, from the settings parser
    // rather than the constructor: metric is the default, so the constructor's
    // memset is the only thing that writes it.
    .unitsImperialOffset      = 4u,
    .watchFaceIdOffset        = 8u,
    // Every offset below is read out of the settings parser at 0x080abcd6,
    // which names each key it stores; Docs/2026-09-07-live-settings-struct.md
    // is the derivation, with the constructor's own defaults as a second source
    // for the four that have one.
    .heartRateZonesOffset     = 0x10u,
    .activityMinutesOffset    = 0x18u,
    .stepsOffset              = 0x1cu,
    .floorsOffset             = 0x20u,
    .heightOffset             = 0x24u,
    .weightOffset             = 0x28u,
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

// Kernel 1.5.0, same ABI (3) as 1.4.0 but a different build: the File class
// moved (each wrapper's __LINE__ log constant shifted by exactly 10, which is
// how the relocated functions were matched one-to-one), and the live settings
// struct was both relocated and repacked. The File-object layout constants
// (0x104 open flag, 0x108 FIL, 0x250 zero-fill) are byte-identical to 1.4.0.
//
// The struct base and every offset below were measured from an on-device SRAM
// dump, cross-checked against the wearer's own settings.json: heartRateZones
// [92,110,129,147,166,184], activityMinutes 30, steps 5000, floors 5, height
// 190, weight 90.0f all read back at these offsets. units and notifications
// stay at +4/+5 as on 1.4.0; the u64 watchFaceId 1.4.0 kept at +8 is gone from
// the struct (the +8 slot is now the zones), so this row sets no watchFaceId
// anchor.
constexpr Signature kSignatures_1_5_0[] = {
    // Open
    {0x080a376cu, {0xF0, 0xB5, 0x05, 0x46, 0x85, 0xB0, 0x00, 0xF5, 0x84, 0x77, 0x06, 0x1D, 0x0A, 0xB3, 0x00, 0x29}},
    // Read
    {0x080a3a1cu, {0x1F, 0xB5, 0x29, 0xB9, 0x8F, 0x21, 0x0F, 0x4B, 0x0F, 0x4A, 0x10, 0x48, 0xC7, 0xF7, 0x66, 0xFC}},
    // Write
    {0x080a3878u, {0x30, 0xB5, 0x14, 0x46, 0x1D, 0x46, 0x85, 0xB0, 0x29, 0xB9, 0x9F, 0x21, 0x0F, 0x4B, 0x10, 0x4A}},
    // Close
    {0x080a3994u, {0x1F, 0xB5, 0x00, 0xF5, 0x84, 0x70, 0x38, 0xF0, 0x83, 0xFC, 0x88, 0xB1, 0x09, 0x4B, 0x01, 0x22}},
    // Release
    {0x080a37e4u, {0x30, 0xB5, 0x90, 0xF8, 0x04, 0x31, 0x04, 0x46, 0x85, 0xB0, 0x03, 0xB3, 0x00, 0xF5, 0x84, 0x75}},
    // SetPath -- unmoved from 1.4.0; every other File function relocated.
    {0x0802b3eau, {0x01, 0x39, 0x03, 0x46, 0x10, 0xB5, 0x32, 0xB1, 0x11, 0xF8, 0x01, 0x4F, 0x01, 0x3A, 0x03, 0xF8}},
    // Exists
    {0x080a3ad4u, {0x10, 0xB5, 0x04, 0x46, 0x28, 0xB9, 0xE2, 0x21, 0x06, 0x4B, 0x07, 0x4A, 0x07, 0x48, 0xC7, 0xF7}},
    // Delete
    {0x080a3b7cu, {0x10, 0xB5, 0x04, 0x46, 0x28, 0xB9, 0xF0, 0x21, 0x06, 0x4B, 0x07, 0x4A, 0x07, 0x48, 0xC7, 0xF7}},
    // Rename
    {0x080a3b0cu, {0x70, 0xB5, 0x0D, 0x46, 0x04, 0x46, 0x28, 0xB9, 0xE8, 0x21, 0x09, 0x4B, 0x09, 0x4A, 0x0A, 0x48}},
};

constexpr AddressSet kFirmware_1_5_0 = {
    .abi                      = 3u,
    .derivedFrom              = "1.5.0",
    .settingsStructBase       = 0x2001113cu,
    .phoneNotificationsOffset = 5u,
    .unitsImperialOffset      = 4u,
    .watchFaceIdOffset        = kNoWatchFaceAnchor,
    .heartRateZonesOffset     = 0x06u,
    .activityMinutesOffset    = 0x0cu,
    .stepsOffset              = 0x10u,
    .floorsOffset             = 0x14u,
    .heightOffset             = 0x18u,
    .weightOffset             = 0x1cu,
    .fileOpenAddr             = 0x080a376cu | 1u,
    .fileReadAddr             = 0x080a3a1cu | 1u,
    .fileWriteAddr            = 0x080a3878u | 1u,
    .fileCloseAddr            = 0x080a3994u | 1u,
    .fileReleaseAddr          = 0x080a37e4u | 1u,
    .setPathAddr              = 0x0802b3eau | 1u,
    .fileExistsAddr           = 0x080a3ad4u | 1u,
    .fileDeleteAddr           = 0x080a3b7cu | 1u,
    .fileRenameAddr           = 0x080a3b0cu | 1u,
    .fileObjectSize           = 856u,
    .pathBufferOffset         = 0x04u,
    .pathBufferSize           = 0x100u,
    .fileSizeFieldOffset      = 0x118u,
    .signatures               = kSignatures_1_5_0,
    .signatureCount           = sizeof(kSignatures_1_5_0) / sizeof(kSignatures_1_5_0[0]),
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

/// True if no two fields in the row claim the same byte of the live struct.
///
/// Nine `size_t` members in a row are interchangeable to the compiler, and the
/// mistake this catches is not a wild value -- it is two plausible offsets
/// transposed, or one member left holding another's. A raw write then lands in
/// a field the app believes it never touches, on a part with no MPU.
constexpr bool noTwoFieldsOverlap(const AddressSet &a)
{
    struct Span {
        size_t at;
        size_t bytes;
    };
    Span spans[] = {
        {a.unitsImperialOffset, 1},      {a.phoneNotificationsOffset, 1},
        {a.heartRateZonesOffset, 6},     {a.activityMinutesOffset, 4},
        {a.stepsOffset, 4},              {a.floorsOffset, 4},
        {a.heightOffset, 4},             {a.weightOffset, 4},
        // watchFaceId last, so an absent one (the sentinel) is simply dropped
        // from the count rather than left claiming an out-of-range span.
        {a.watchFaceIdOffset, 8},
    };
    size_t kCount = sizeof(spans) / sizeof(spans[0]);
    if (a.watchFaceIdOffset == kNoWatchFaceAnchor) {
        --kCount;
    }
    for (size_t i = 0; i < kCount; ++i) {
        for (size_t j = i + 1; j < kCount; ++j) {
            const bool disjoint = spans[i].at + spans[i].bytes <= spans[j].at ||
                                  spans[j].at + spans[j].bytes <= spans[i].at;
            if (!disjoint) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool isWellFormed(const AddressSet &a)
{
    return a.abi > 0u && a.derivedFrom != nullptr && signaturesPairWithAddresses(a) &&
           a.settingsStructBase >= kSramBase && a.settingsStructBase < kSramEnd &&
           a.phoneNotificationsOffset < kMaxStructOffset &&
           a.unitsImperialOffset < kMaxStructOffset &&
           (a.watchFaceIdOffset == kNoWatchFaceAnchor ||
            a.watchFaceIdOffset < kMaxStructOffset) &&
           a.heartRateZonesOffset < kMaxStructOffset &&
           a.activityMinutesOffset < kMaxStructOffset &&
           a.stepsOffset < kMaxStructOffset &&
           a.floorsOffset < kMaxStructOffset &&
           a.heightOffset < kMaxStructOffset &&
           a.weightOffset < kMaxStructOffset && noTwoFieldsOverlap(a) &&
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
    &kFirmware_1_5_0,
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
              "where an address belongs, two live-struct fields claiming the same byte, a "
              "File layout that does not fit its own object size, or a signature filed "
              "under the wrong function.");

/// True if two rows carry the same signature at every index.
constexpr bool signaturesIdentical(const AddressSet &a, const AddressSet &b)
{
    if (a.signatureCount != b.signatureCount) {
        return false;
    }
    for (size_t i = 0; i < a.signatureCount; ++i) {
        if (a.signatures[i].address != b.signatures[i].address) {
            return false;
        }
        for (size_t k = 0; k < kSignatureBytes; ++k) {
            if (a.signatures[i].bytes[k] != b.signatures[i].bytes[k]) {
                return false;
            }
        }
    }
    return true;
}

/// True when no two rows share an ABI and identical signatures.
constexpr bool sameAbiRowsAreDistinguishable()
{
    constexpr size_t n = sizeof(kSupported) / sizeof(kSupported[0]);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            if (kSupported[i]->abi == kSupported[j]->abi &&
                signaturesIdentical(*kSupported[i], *kSupported[j])) {
                return false;
            }
        }
    }
    return true;
}

static_assert(sameAbiRowsAreDistinguishable(),
              "Two rows share an ABI and carry identical signatures. The gate tells "
              "same-ABI firmware versions apart by reading the bytes at each row's "
              "addresses, so rows it must choose between have to differ somewhere.");

} // namespace

size_t rowCount()
{
    return sizeof(kSupported) / sizeof(kSupported[0]);
}

const AddressSet *rowAt(size_t index)
{
    if (index >= rowCount()) {
        return nullptr;
    }
    return kSupported[index];
}

} // namespace SettingsAddresses
