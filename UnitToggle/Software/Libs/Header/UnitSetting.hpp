/**
 ******************************************************************************
 * @file    UnitSetting.hpp
 * @brief   Which watch setting this app owns, and the scratch names it commits
 *          under.
 ******************************************************************************
 *
 * The commit's own scratch names are not here: they are shared across the apps
 * that use this mechanism, so any of them can put back a file another stranded.
 * `SettingsPersist::kScratchPrevPath` says why.
 ******************************************************************************
 */

#ifndef UNIT_SETTING_HPP
#define UNIT_SETTING_HPP

#include "LiveSettings.hpp"
#include "SettingsPersist.hpp"
#include "SettingsSplice.hpp"

namespace UnitSetting
{

/// The offset is a per-firmware runtime value, so this pairs it with the name
/// at the point the address set is already known.
inline LiveSettings::Flag liveFlag(const SettingsAddresses::AddressSet &addrs)
{
    return LiveSettings::Flag{addrs.unitsImperialOffset, "unitsImperial"};
}

constexpr SettingsPersist::Field kField = {
    .splice     = &SettingsSplice::setUnits,
    .name       = "units",
    .probeAPath = "2:/ut-probe-a.tmp",
    .probeBPath = "2:/ut-probe-b.tmp",
    .probeText  = "UnitToggle primitive self-test",
};

static_assert(SettingsPersist::isWellFormed(kField), "see SettingsPersist::isWellFormed");

} // namespace UnitSetting

#endif // UNIT_SETTING_HPP
