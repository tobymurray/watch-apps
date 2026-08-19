/**
 ******************************************************************************
 * @file    HomeConfig.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The home position, as written into the app's folder from a desktop.
 ******************************************************************************
 *
 * Turns two strings in `input.json` into a `Fix`, or into a reason there is not
 * one. That is the whole job, and it is a file of its own because it is the
 * seam where data somebody else wrote becomes data this app acts on.
 *
 * ## Why a typed-in position and not a GNSS one
 *
 * A glance is looked at for three seconds. A cold GNSS fix takes tens of them,
 * costs real battery, and the carousel stops this service the moment the user
 * scrolls past -- so a glance that tried to locate itself would spend the power
 * and still show nothing. Meanwhile the accuracy this app needs is almost
 * comically loose: four minutes of sunrise per degree of longitude means a
 * position good to 25 km is good to under a minute, and 25 km is "which city".
 * A value typed once is not a compromise here, it is a better fit for the
 * question than a fix would be.
 *
 * Kira fills it in at install time, so the usual objection -- that a watch with
 * four buttons cannot be told a coordinate -- does not apply: the page that
 * installs the app writes the file in the same visit.
 *
 * ## Where it will come from later
 *
 * `Fix::Source::Cached` is the intended upgrade: the map apps already receive
 * GNSS locations, and a shared last-known fix under `SharedData/` -- one writer,
 * many readers, the arrangement `MapManager` established for pack verification
 * -- would let this glance follow the wearer without ever powering a receiver
 * itself. When that exists the order becomes cached-then-configured, because a
 * real fix from last weekend beats a home a person typed in and then moved
 * away from, and this class keeps its job as the fallback.
 *
 ******************************************************************************
 */

#ifndef HOMECONFIG_HPP
#define HOMECONFIG_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "Fix.hpp"
#include "InputConfig.hpp"

namespace Sun
{

/// coreJSON queries into the document Kira assembles. These are the app's own
/// vocabulary -- Kira invents no part of the format -- so they and the registry
/// manifest have to say the same thing, and nothing but a watch can find out
/// that they do not. The config tests are that check moved off the watch.
constexpr char kLatQuery[] = "values.lat";
constexpr char kLonQuery[] = "values.lon";

class HomeConfig
{
public:
    /**
     * @brief Why there is, or is not, a position.
     *
     * Three states rather than a bool because the two failures need different
     * words on the glance: "you have not set this yet" is a thing to go and do,
     * "the file you wrote is not one I can read" is a thing to go and fix, and
     * a glance that says the wrong one of those sends somebody looking in the
     * wrong place.
     */
    enum class Status : uint8_t {
        Ok,        ///< A position, parsed and on the globe.
        Absent,    ///< No file. Nothing has been configured.
        Rejected,  ///< A file, but not one this app can act on.
    };

    explicit HomeConfig(const SDK::Kernel &kernel, const char *path = InputConfig::kPath);

    /**
     * @brief Re-read if something outside changed the file.
     *
     * @retval true The position may have changed -- recompute.
     */
    bool refresh();

    Status     status() const { return mStatus; }
    const Fix &fix() const { return mFix; }

private:
    void reload();

    InputConfig::Reader mReader;
    Status              mStatus = Status::Absent;
    Fix                 mFix;
    bool                mSeen = false;
};

} // namespace Sun

#endif // HOMECONFIG_HPP
