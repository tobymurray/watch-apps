/**
 ******************************************************************************
 * @file    SettingsField.hpp
 * @brief   Which field an app owns in `2:/settings.json`, the sizes the write
 *          path is bounded by, and the splice that respects them.
 ******************************************************************************
 *
 * Free of SDK types so `Tests/` can drive `isWellFormed` and `spliceWithinReadCap`
 * directly -- the two pieces of this mechanism whose mistakes are silent.
 ******************************************************************************
 */

#ifndef SETTINGS_FIELD_HPP
#define SETTINGS_FIELD_HPP

#include <cstddef>
#include <cstdint>

#include "SettingsAddresses.hpp"
#include "SettingsSplice.hpp"

namespace SettingsPersist
{

/// Upper bound on the settings file this app will read or write. The real file
/// was 245 bytes on 2026-09-05; this leaves headroom for the firmware adding
/// fields while still refusing an unexpectedly huge read.
constexpr size_t kMaxSettingsFileSize = 512;

/// The read buffer: room for a null terminator this app adds, and slack.
constexpr size_t kBufferCapacity = kMaxSettingsFileSize + 8;

/// Longest `Field::probeText` `validatePrimitives` will read back, one less
/// than the buffer it reads into.
constexpr size_t kMaxProbeTextBytes = 63;

/// Longest scratch path a `Field` may carry. `setPath` truncates silently past
/// the File object's own path buffer, which surfaces only as a primitive check
/// failing for no stated reason.
constexpr size_t kMaxScratchPathBytes = 64;

/// The file every app here rewrites.
constexpr const char *kSettingsPath = "2:/settings.json";

/// Where a rewrite is staged, and where the file being replaced waits while the
/// rename lands. One name for every app, deliberately: a commit that loses
/// power between the two renames leaves the wearer's only settings file under
/// `kScratchPrevPath`, and the app that stranded it is not necessarily the app
/// that next runs -- it may never run again. A shared name is what lets any of
/// them put it back.
///
/// Sharing is safe because the only destructive step, the stale-prev delete in
/// `commitTmpFile`, is guarded on `2:/settings.json` existing -- which is
/// exactly what a stranded file means is not true. Recovery also runs before
/// that step and at launch, so a commit never begins with a strand in place.
constexpr const char *kScratchTmpPath  = "2:/settings.json.tmp";
constexpr const char *kScratchPrevPath = "2:/settings.json.prev";

/// Names earlier versions staged under, swept by recovery so a watch already
/// holding a strand from one of them is not stuck with it. NotifyToggle 0.6.0
/// and 0.6.1 shipped `.ntprev`; UnitToggle was never released using `.utprev`,
/// and is here only because this author's watch has run it. Both can go once no
/// watch can still be carrying one.
constexpr const char *kLegacyPrevPaths[] = {
    "2:/settings.json.ntprev",
    "2:/settings.json.utprev",
};
constexpr size_t kLegacyPrevPathCount =
    sizeof(kLegacyPrevPaths) / sizeof(kLegacyPrevPaths[0]);

/// How wide the field is in the kernel's live struct, and how to read it.
/// Independent of the shape it has in the file: `weight` is an integer there
/// and an `f32` here.
enum class LiveWidth {
    U8,
    U32,
    U64,
    F32,
    U8x6,
};

/// Which `RequestSystemSettings` field reports this one, so a raw read can be
/// compared against the kernel's own answer and a raw write can be seen to
/// have moved the setting. `None` means no supported message carries it, and
/// the only evidence available is that the byte read back what was written.
enum class Witness {
    None,
    ImperialUnits,
    ActivityMinutes,
    Steps,
    Floors,
    HeightCm,
    WeightKg,
    HeartRateThresholds,
};

/// One editable field: which key in the file, what shape its value has there,
/// which member of the resolved address set holds its live offset, how wide it
/// is there, which supported message corroborates it, and the range this app
/// will let a wearer write.
///
/// The offset is a pointer-to-member rather than a number, so a row cannot
/// carry a compile-time address for a per-firmware value -- which is the whole
/// reason `SettingsAddresses` exists. Initialise with designated initialisers
/// and assert `isWellFormed`.
struct FieldDescriptor {
    const char *name;                 ///< Drawn on screen and logged.
    SettingsSplice::KeyPath key;
    SettingsSplice::JsonShape shape;
    size_t SettingsAddresses::AddressSet::*offset;
    LiveWidth width;
    Witness  witness;
    uint32_t writeMin;                ///< Inclusive, in the field's own unit.
    uint32_t writeMax;                ///< Inclusive.
};

/// The scratch paths one app's primitive self-test writes. Per app, never per
/// field: `validatePrimitives` proves the kernel's `File` primitives behave,
/// which is a property of the firmware and of nothing a field says.
struct AppIdentity {
    const char *probeAPath;  ///< Never the settings file, nor a shared scratch name.
    const char *probeBPath;
    const char *probeText;   ///< At most kMaxProbeTextBytes.
};

/// The field a single-field app owns, and the two scratch paths its primitive
/// self-test uses. Initialise with designated initialisers and assert
/// `isWellFormed`: four members are interchangeable `const char *`. The
/// commit's own scratch names are not here -- they are shared, for the reason
/// above.
///
/// An app with more than one field declares a `FieldDescriptor` per field and
/// one `AppIdentity` instead, which is the same two halves without this one's
/// per-field splice signature.
struct Field {
    /// Rewrites this field in a settings.json buffer.
    SettingsSplice::Result (*splice)(char *buf, size_t &len, size_t capacity, bool value,
                                     size_t *valueOffsetOut);
    const char *name;        ///< DebugLog only.
    const char *probeAPath;  ///< validatePrimitives scratch; never the settings file.
    const char *probeBPath;
    const char *probeText;   ///< At most kMaxProbeTextBytes.
};

namespace detail
{

using SettingsSplice::detail::constexprStrlen;

constexpr bool sameString(const char *a, const char *b)
{
    for (size_t i = 0;; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
        if (a[i] == '\0') {
            return true;
        }
    }
}

} // namespace detail

/// True if every member holds the kind of value its name promises -- not a
/// claim that any path is the right one, only that a value has not landed in
/// the wrong member.
constexpr bool isWellFormed(const AppIdentity &a)
{
    if (a.probeText == nullptr) {
        return false;
    }
    if (detail::constexprStrlen(a.probeText) > kMaxProbeTextBytes) {
        return false;
    }

    const char *const paths[] = {a.probeAPath, a.probeBPath};
    constexpr size_t kCount = 2;

    // Every path proved non-null before any of them is compared: `sameString`
    // reads both of its arguments, so a later null would be dereferenced by an
    // earlier iteration's comparison.
    for (size_t i = 0; i < kCount; ++i) {
        if (paths[i] == nullptr) {
            return false;
        }
        const size_t n = detail::constexprStrlen(paths[i]);
        if (n == 0 || n > kMaxScratchPathBytes) {
            return false;
        }
    }
    // A probe path that collided with the settings file or with either shared
    // scratch name would have the self-test delete what a commit is holding.
    const char *const reserved[] = {kSettingsPath, kScratchTmpPath, kScratchPrevPath};
    for (size_t i = 0; i < kCount; ++i) {
        for (size_t r = 0; r < 3; ++r) {
            if (detail::sameString(paths[i], reserved[r])) {
                return false;
            }
        }
        for (size_t j = i + 1; j < kCount; ++j) {
            if (detail::sameString(paths[i], paths[j])) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool isWellFormed(const Field &f)
{
    if (f.splice == nullptr || f.name == nullptr) {
        return false;
    }
    return isWellFormed(AppIdentity{f.probeAPath, f.probeBPath, f.probeText});
}

/// Widths a `JsonShape` can be stored at. A shape and a width that cannot
/// belong to the same field is the mistake this catches: a `Boolean` behind a
/// four-byte read, or a six-value array behind a one-byte one.
constexpr bool widthCanHoldShape(SettingsSplice::JsonShape shape, LiveWidth width)
{
    switch (shape) {
        case SettingsSplice::JsonShape::Boolean:
        case SettingsSplice::JsonShape::UnitsToken:
            return width == LiveWidth::U8;
        case SettingsSplice::JsonShape::Number:
            return width == LiveWidth::U32 || width == LiveWidth::U64 ||
                   width == LiveWidth::F32;
        case SettingsSplice::JsonShape::NumberArray6:
            return width == LiveWidth::U8x6;
    }
    return false;
}

/// True if every member holds the kind of value its name promises. Not a claim
/// that the key exists or that the offset is right -- only that a value has not
/// landed in the wrong member, which no compiler can see across two
/// interchangeable `const char *`, two enums and a range.
constexpr bool isWellFormed(const FieldDescriptor &f)
{
    if (f.name == nullptr || f.key.leaf == nullptr || f.offset == nullptr) {
        return false;
    }
    if (detail::constexprStrlen(f.key.leaf) == 0) {
        return false;
    }
    if (f.key.outer != nullptr && detail::constexprStrlen(f.key.outer) == 0) {
        return false;
    }
    if (!widthCanHoldShape(f.shape, f.width)) {
        return false;
    }
    // An empty range makes a field the wearer can open and never change, which
    // reads on the wrist as the app being broken.
    if (f.writeMin > f.writeMax) {
        return false;
    }
    // A one-byte live field cannot hold what a wider range would write, and the
    // truncation is silent: the kernel's own reader stores the low byte of
    // whatever the file's number parsed to.
    if ((f.width == LiveWidth::U8 || f.width == LiveWidth::U8x6) && f.writeMax > 255u) {
        return false;
    }
    return true;
}

/// Rewrites `f`'s field in `buf`, capped at what the reader will accept.
///
/// The cap is the point of this function. Handing a splice the size of the
/// buffer instead lets it produce a file that commits and is then refused by
/// every later read -- a durable change reported as unsaved, and no further
/// write possible on that watch by any app. `Tests/` drives this rather than
/// the splice directly, so the cap is what is under test.
inline SettingsSplice::Result spliceWithinReadCap(const Field &f, char *buf, size_t &len,
                                                  bool value, size_t *valueOffsetOut)
{
    return f.splice(buf, len, kMaxSettingsFileSize, value, valueOffsetOut);
}

/// Replaces `f`'s value with `token`, capped at what the reader will accept --
/// the same cap, and for the same reason, as the overload above.
inline SettingsSplice::Result spliceWithinReadCap(const FieldDescriptor &f, char *buf, size_t &len,
                                                  const char *token, size_t tokenLen,
                                                  size_t *valueOffsetOut)
{
    return SettingsSplice::replaceValue(buf, len, kMaxSettingsFileSize, f.key, f.shape, token,
                                        tokenLen, valueOffsetOut);
}

} // namespace SettingsPersist

#endif // SETTINGS_FIELD_HPP
