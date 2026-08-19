#!/usr/bin/env python3
"""Regenerate the reference values in Solar_test.cpp, and re-measure the spread.

The fixtures in `Solar_test.cpp` are the only thing in this repository that says
the sunrise arithmetic is *right* rather than merely self-consistent, so they
have to come from somewhere else. That somewhere is `astral`, a pure-Python
implementation of the same NOAA calculation written by other people:

    python3 -m venv .venv && .venv/bin/pip install astral
    .venv/bin/python astral_reference.py            # the fixture rows
    .venv/bin/python astral_reference.py --sweep    # the polar-day comparison

Two things about the comparison are deliberate.

**Fixed-offset zones.** Each place is asked about in the whole-hour zone nearest
its own longitude rather than in its real civil zone. A fixture should not also
be a test of somebody's DST database; the real zones are exercised by the offset
arithmetic in the test file instead.

**Both events on the same local day.** `astral` computes sunrise and sunset
independently for a calendar date, so west of Greenwich its "sunset on the 21st"
belongs to the evening of the 20th local. Asking in a solar-aligned zone puts
both events inside the local day, which is the convention `Sun::forLocalDay`
uses.

`--sweep` prints, for five polar sites across a whole year, every day the two
implementations disagree about whether the sun rises at all. As of 2026-08-18
that was 13 days out of 1825: three where astral's own interval search fails to
find a sunrise that plainly exists (its message is "Unable to find a sunrise
time", between two ordinary days), and ten single transition days where the
sun's greatest altitude is within a hundredth of a degree of the -0.833 that
defines sunrise. Neither kind belongs in a fixture, so the polar table in
Solar_test.cpp sticks to days comfortably inside their polar period.
"""

import argparse
import datetime

from astral import Observer, sun

PLACES = [
    ("Ottawa",        45.4215,  -75.6972),
    ("London",        51.5074,   -0.1278),
    ("Null Island",    0.0000,    0.0000),
    ("Sydney",       -33.8688,  151.2093),
    ("Reykjavik",     64.1466,  -21.9426),
    ("Quito",         -0.1807,  -78.4678),
    ("Ushuaia",      -54.8019,  -68.3030),
    ("Kashgar",       39.4704,   75.9898),
    ("Tromso",        69.6492,   18.9553),
    ("Longyearbyen",  78.2232,   15.6267),
]

DATES = ["2026-03-20", "2026-06-21", "2026-09-23", "2026-12-21", "2026-01-15", "2026-08-18"]

SWEEP_PLACES = [
    ("Tromso",        69.6492,  18.9553),
    ("Longyearbyen",  78.2232,  15.6267),
    ("Alert",         82.5018, -62.3481),
    ("McMurdo",      -77.8419, 166.6863),
    ("Rovaniemi",     66.5039,  25.7294),
]


def zone(lon):
    """The whole hour nearest this longitude's own solar time."""
    hours = round(lon / 15.0)
    return hours, datetime.timezone(datetime.timedelta(hours=hours))


def verdict(observer, day, tz):
    """(rise, set) as UTC seconds, or (None, why) when there is no such pair."""
    try:
        return (int(sun.sunrise(observer, day, tzinfo=tz).timestamp()),
                int(sun.sunset(observer, day, tzinfo=tz).timestamp()))
    except ValueError as e:
        message = str(e)
        if "above" in message:
            return (None, "AlwaysUp")
        if "below" in message:
            return (None, "AlwaysDown")
        return (None, "Unresolved")


def rows():
    for name, lat, lon in PLACES:
        hours, tz = zone(lon)
        observer = Observer(latitude=lat, longitude=lon, elevation=0.0)
        for iso in DATES:
            day = datetime.date.fromisoformat(iso)
            rise, rest = verdict(observer, day, tz)
            if rise is None:
                print(f'    {{ "{name}", {lat:>9.4f}, {lon:>9.4f}, {hours * 3600:>6d}, '
                      f'{day.year}, {day.month:2d}, {day.day:2d}, Sun::DayKind::{rest} }},')
            else:
                local_r = datetime.datetime.fromtimestamp(rise, tz).strftime("%H:%M")
                local_s = datetime.datetime.fromtimestamp(rest, tz).strftime("%H:%M")
                print(f'    {{ "{name}", {lat:>9.4f}, {lon:>9.4f}, {hours * 3600:>6d}, '
                      f'{day.year}, {day.month:2d}, {day.day:2d}, {rise}, {rest} }},'
                      f'  // {local_r} / {local_s} local')


def sweep(year):
    """A year of polar verdicts, as CSV, for the C++ side to compare against."""
    for name, lat, lon in SWEEP_PLACES:
        hours, tz = zone(lon)
        observer = Observer(latitude=lat, longitude=lon, elevation=0.0)
        day = datetime.date(year, 1, 1)
        while day.year == year:
            rise, rest = verdict(observer, day, tz)
            kind = "Normal" if rise is not None else rest
            print(f"{name},{lat},{lon},{hours * 3600},{day.year},{day.month},{day.day},{kind}")
            day += datetime.timedelta(days=1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sweep", action="store_true",
                        help="a year of polar verdicts as CSV instead of fixture rows")
    parser.add_argument("--year", type=int, default=2026)
    args = parser.parse_args()

    sweep(args.year) if args.sweep else rows()
