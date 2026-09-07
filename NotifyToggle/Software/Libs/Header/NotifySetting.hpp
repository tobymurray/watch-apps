/**
 ******************************************************************************
 * @file    NotifySetting.hpp
 * @brief   Which watch setting this app owns, and the scratch names it commits
 *          under.
 ******************************************************************************
 *
 * The scratch names are this app's alone and must stay exactly what they are:
 * a watch left holding `2:/settings.json.ntprev` by a commit that lost power is
 * recovered by matching that name, and renaming it here would strand the only
 * settings file on the watch.
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
    .tmpPath    = "2:/settings.json.nttmp",
    .prevPath   = "2:/settings.json.ntprev",
    .probeAPath = "2:/nt-probe-a.tmp",
    .probeBPath = "2:/nt-probe-b.tmp",
    .probeText  = "NotifyToggle primitive self-test",
};

static_assert(SettingsPersist::isWellFormed(kField), "see SettingsPersist::isWellFormed");

} // namespace NotifySetting

#endif // NOTIFY_SETTING_HPP
