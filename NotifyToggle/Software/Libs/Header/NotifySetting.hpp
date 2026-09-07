/**
 ******************************************************************************
 * @file    NotifySetting.hpp
 * @brief   Which watch setting this app owns, and the scratch names it commits
 *          under.
 ******************************************************************************
 *
 * The commit's own scratch names are not here: they are shared across the apps
 * that use this mechanism, so any of them can put back a file another stranded.
 * `SettingsPersist::kScratchPrevPath` says why.
 ******************************************************************************
 */

#ifndef NOTIFY_SETTING_HPP
#define NOTIFY_SETTING_HPP

#include "LiveSettings.hpp"
#include "SettingsPersist.hpp"
#include "SettingsSplice.hpp"

namespace NotifySetting
{

/// The offset is a per-firmware runtime value, so this pairs it with the name
/// at the point the address set is already known.
inline LiveSettings::Flag liveFlag(const SettingsAddresses::AddressSet &addrs)
{
    return LiveSettings::Flag{addrs.phoneNotificationsOffset, "notifications"};
}

constexpr SettingsPersist::Field kField = {
    .splice     = &SettingsSplice::setNotifications,
    .name       = "notifications",
    .probeAPath = "2:/nt-probe-a.tmp",
    .probeBPath = "2:/nt-probe-b.tmp",
    .probeText  = "NotifyToggle primitive self-test",
};

static_assert(SettingsPersist::isWellFormed(kField), "see SettingsPersist::isWellFormed");

} // namespace NotifySetting

#endif // NOTIFY_SETTING_HPP
