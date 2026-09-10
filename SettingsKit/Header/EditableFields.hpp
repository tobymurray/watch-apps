/**
 ******************************************************************************
 * @file    EditableFields.hpp
 * @brief   Every field of `2:/settings.json` this kit will edit, and the range
 *          it will write for each.
 ******************************************************************************
 *
 * Free of SDK types so `Tests/` can drive the ranges and the well-formedness
 * gate without a kernel.
 *
 * The census lives here rather than in an app because it is derived from the
 * same firmware image the addresses are, by the same hand, and checkable by no
 * compiler -- the reason `SettingsKit/README.md` gives for the addresses being
 * shared applies unchanged to the offsets, shapes and widths beside them.
 *
 * `Docs/2026-09-07-live-settings-struct.md` derives every offset and width
 * from the settings parser; `SettingsEditor/README.md` is why each range is what
 * it is, and which of them are assertions rather than measurements.
 *
 * Four fields of the file are deliberately absent, and their absence is the
 * design rather than an omission:
 *
 * - `watchFaceId` is the field `LiveSettings::readChecked` cross-checks every
 *   unwitnessed raw read against, so an app that could write it could
 *   invalidate its own gate; no supported message reports it, and no bound
 *   tighter than "plausibly small" exists for it.
 * - `gender` and `dateOfBirth` appear nowhere in the 4 MB image -- neither
 *   string is present at all -- so the kernel neither reads nor writes them and
 *   nothing on the watch would act on a change.
 * - `version` is the file's schema version. The parser ignores the file's value
 *   and stores a constant 2.
 ******************************************************************************
 */

#ifndef EDITABLE_FIELDS_HPP
#define EDITABLE_FIELDS_HPP

#include "SettingsField.hpp"

namespace SettingsPersist
{

/// The eight editable fields, in the order a wearer pages through them.
namespace fields
{

constexpr FieldDescriptor kUnits = {
    .name     = "UNITS",
    .key      = {nullptr, "units"},
    .shape    = SettingsSplice::JsonShape::UnitsToken,
    .offset   = &SettingsAddresses::AddressSet::unitsImperialOffset,
    .width    = LiveWidth::U8,
    .witness  = Witness::ImperialUnits,
    .writeMin = 0u,
    .writeMax = 1u,   ///< The parser recognises two spellings and no third.
};

constexpr FieldDescriptor kNotifications = {
    .name     = "NOTIFICATIONS",
    .key      = {"phone", "notifications"},
    .shape    = SettingsSplice::JsonShape::Boolean,
    .offset   = &SettingsAddresses::AddressSet::phoneNotificationsOffset,
    .width    = LiveWidth::U8,
    .witness  = Witness::None,
    .writeMin = 0u,
    .writeMax = 1u,
};

/// The maximum a day can hold. Not the range offered -- `writeMax` is -- but
/// the range offered has to sit inside it, which a test asserts.
constexpr uint32_t kMinutesInADay = 1440u;

constexpr FieldDescriptor kActivityMinutes = {
    .name     = "ACTIVE MINUTES",
    .key      = {"dailyGoals", "activityMinutes"},
    .shape    = SettingsSplice::JsonShape::Number,
    .offset   = &SettingsAddresses::AddressSet::activityMinutesOffset,
    .width    = LiveWidth::U32,
    .witness  = Witness::ActivityMinutes,
    .writeMin = 5u,
    .writeMax = 240u,
};

constexpr FieldDescriptor kSteps = {
    .name     = "STEP GOAL",
    .key      = {"dailyGoals", "steps"},
    .shape    = SettingsSplice::JsonShape::Number,
    .offset   = &SettingsAddresses::AddressSet::stepsOffset,
    .width    = LiveWidth::U32,
    .witness  = Witness::Steps,
    .writeMin = 500u,
    .writeMax = 50000u,
};

constexpr FieldDescriptor kFloors = {
    .name     = "FLOOR GOAL",
    .key      = {"dailyGoals", "floors"},
    .shape    = SettingsSplice::JsonShape::Number,
    .offset   = &SettingsAddresses::AddressSet::floorsOffset,
    .width    = LiveWidth::U32,
    .witness  = Witness::Floors,
    .writeMin = 1u,
    .writeMax = 200u,
};

constexpr FieldDescriptor kHeight = {
    .name     = "HEIGHT",
    .key      = {nullptr, "height"},
    .shape    = SettingsSplice::JsonShape::Number,
    .offset   = &SettingsAddresses::AddressSet::heightOffset,
    .width    = LiveWidth::U32,
    .witness  = Witness::HeightCm,
    .writeMin = 100u,
    .writeMax = 250u,
};

/// Whole kilograms, though the live field is an `f32` and the parser reads the
/// token with `strtod`: a wearer's phone may have written `90.5`, which the
/// splice replaces rather than refuses.
constexpr FieldDescriptor kWeight = {
    .name     = "WEIGHT",
    .key      = {nullptr, "weight"},
    .shape    = SettingsSplice::JsonShape::Number,
    .offset   = &SettingsAddresses::AddressSet::weightOffset,
    .width    = LiveWidth::F32,
    .witness  = Witness::WeightKg,
    .writeMin = 25u,
    .writeMax = 250u,
};

/// The smallest maximum whose ladder still has a gap between every pair of
/// floors -- two equal floors being a zone no heart rate can be in. The six
/// values are 50/60/70/80/90/100% of the maximum rounded half up, so the gap is
/// a tenth of it, and 10 bpm is where that last reaches 1:
/// `[5,6,7,8,9,10]` still increases and `[4,5,6,6,7,8]` at 9 does not.
constexpr uint32_t kSmallestSpreadableMaximum = 10u;

/// The range the file's six bytes can hold at all.
constexpr uint32_t kWidestStorableMaximum = 255u;

/// One number, not six. The wearer edits their maximum heart rate and the
/// whole ladder is rewritten from it by the watch's own rule -- so `writeMin`
/// and `writeMax` are bpm of maximum, and the array is replaced whole.
constexpr FieldDescriptor kMaxHeartRate = {
    .name     = "MAX HEART RATE",
    .key      = {nullptr, "heartRateZones"},
    .shape    = SettingsSplice::JsonShape::NumberArray6,
    .offset   = &SettingsAddresses::AddressSet::heartRateZonesOffset,
    .width    = LiveWidth::U8x6,
    .witness  = Witness::HeartRateThresholds,
    .writeMin = 90u,
    .writeMax = 220u,
};

} // namespace fields

/// Paging order. UP and DOWN move through this list and nothing else, so the
/// order is the navigation.
constexpr const FieldDescriptor *kEditable[] = {
    &fields::kUnits,      &fields::kNotifications, &fields::kActivityMinutes,
    &fields::kSteps,      &fields::kFloors,        &fields::kHeight,
    &fields::kWeight,     &fields::kMaxHeartRate,
};
constexpr size_t kEditableCount = sizeof(kEditable) / sizeof(kEditable[0]);

namespace detail
{

constexpr bool everyEditableFieldIsWellFormed()
{
    for (const auto *f : kEditable) {
        if (f == nullptr || !isWellFormed(*f)) {
            return false;
        }
    }
    return true;
}

/// True if no two rows name the same key. Two rows for one key would give a
/// wearer two screens that disagree about the same value.
constexpr bool everyKeyAppearsOnce()
{
    for (size_t i = 0; i < kEditableCount; ++i) {
        for (size_t j = i + 1; j < kEditableCount; ++j) {
            const auto &a = kEditable[i]->key;
            const auto &b = kEditable[j]->key;
            const bool sameOuter = (a.outer == nullptr) == (b.outer == nullptr) &&
                                   (a.outer == nullptr || SettingsPersist::detail::sameString(
                                                              a.outer, b.outer));
            if (sameOuter && SettingsPersist::detail::sameString(a.leaf, b.leaf)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace detail

static_assert(detail::everyEditableFieldIsWellFormed(),
              "An editable field has a value in the wrong member: a shape its live width "
              "cannot hold, an empty range, or a range a one-byte field would truncate.");

static_assert(detail::everyKeyAppearsOnce(),
              "Two editable fields name the same key, so a wearer would get two screens "
              "for one value.");

} // namespace SettingsPersist

#endif // EDITABLE_FIELDS_HPP
