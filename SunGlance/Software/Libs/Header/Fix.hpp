/**
 ******************************************************************************
 * @file    Fix.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Where the watch thinks it is, and how much that claim is worth.
 ******************************************************************************
 *
 * The arithmetic in `Solar.hpp` is the easy half of a sunrise glance. The hard
 * half is this: a watch that has not had a GNSS fix since the last activity --
 * and may never have had one -- still has to put a time on the screen or admit
 * that it cannot.
 *
 * ## Why a typed position is worth a file
 *
 * Because the alternative is a bare pair of doubles, and a bare pair of doubles
 * defaults to (0, 0), which is a real place in the Gulf of Guinea with a real
 * sunrise at about six o'clock all year round. A glance that has no idea where
 * it is and a glance in São Tomé produce the same plausible screen. `Source`
 * makes "nowhere" a state the renderer has to handle rather than a coordinate
 * it can accidentally use.
 *
 * ## The staleness budget, which is generous
 *
 * Sunrise moves about four minutes per degree of longitude, so a position that
 * is 25 km out east-west costs under a minute, and one that is a season out of
 * date costs nothing at all -- the date is an input, not part of the fix. That
 * is why a hand-configured home position is a legitimate answer here and not a
 * placeholder: for the question being asked, a fix from six months ago 20 km
 * away is worth as much as one from this morning.
 *
 * ## The door left open
 *
 * `Source::Cached` exists but nothing produces it yet. When the map apps grow a
 * shared last-known fix -- one writer dropping lat/lon and a timestamp into
 * `SharedData/`, the way `MapManager` already owns pack verification for
 * everybody -- it becomes the preferred source, and `Config` becomes the
 * fallback for a watch that has never had a fix. That ordering, and `utc`,
 * are here now so the renderer can already say which one it is showing and how
 * old it is; a configured home is `utc = -1`, meaning timeless rather than
 * fresh, because nothing about it expires.
 *
 ******************************************************************************
 */

#ifndef FIX_HPP
#define FIX_HPP

#include <cstdint>

#include "Solar.hpp"

namespace Sun
{

struct Fix
{
    enum class Source : uint8_t {
        None,    ///< No position. The glance says so; it does not guess.
        Config,  ///< Typed in once, by a person, and true until they move.
        Cached,  ///< A real GNSS fix somebody else recorded. Not yet produced.
    };

    Source  source = Source::None;
    double  latDeg = 0.0;
    double  lonDeg = 0.0;
    /// When the fix was taken, or -1 for one that does not age.
    int64_t utc    = -1;

    bool     has() const { return source != Source::None; }
    Position position() const { return Position { latDeg, lonDeg }; }
};

/**
 * @brief Parse a decimal degree value written by a human or by Kira.
 *
 * Strict on purpose. The input arrives as text from a form or from Notepad, and
 * the failure modes are quiet ones: `45,4215` is how most of Europe writes a
 * decimal and would parse as 45 under a lenient reader, moving sunrise by half
 * an hour; `45.4215N` would parse as 45.4215 and silently discard a hemisphere
 * the next person will write as `S`. Both are rejected, and rejection is
 * visible on the glance, which is the difference between a value somebody
 * fixes and a value nobody notices.
 *
 * Accepted: an optional sign, at least one digit, an optional fractional part.
 * No exponent, no whitespace, no degree symbol, no compass letter, nothing
 * outside +/- @p limit.
 *
 * @param limit 90 for a latitude, 180 for a longitude.
 * @return false and leaves @p out alone if the text is not one clean number.
 */
bool parseDegrees(const char *text, double limit, double &out);

} // namespace Sun

#endif // FIX_HPP
