# Sun — sunrise and sunset, as a glance

A `Glance` app: one card in the carousel that says what the sun does next and
when, for a position you set once when you install it.

> Unofficial. Not affiliated with, endorsed or sponsored by UNA Watch Ltd.

```
        sunrise                    sunset                   polar night
         04:50                     19:17                     no sunrise
  in 1h12m, sets 19:17      in 8h30m, rose 04:50         the sun stays down
```

There is no GUI. Tapping the card opens nothing, because everything this app
has to say fits in three lines — which is the definition of a glance, and the
reason it is 34 KB and runs only while you are looking at it.

## Status: builds, tested at a desk, never run on a watch

Everything below builds against `apps-v1.4.0` and **64 host tests pass**,
including twelve that drive the real service through a scripted glance
carousel. Nothing here has been on hardware or through the simulator. So:

| | |
| --- | --- |
| **Checked against an independent implementation** | Sunrise and sunset for 52 place-days against [`astral`](https://pypi.org/project/astral/) 3.2, plus a 1825-day polar sweep. Worst disagreement: 26 s below 55° latitude, 72 s to 66°, 118 s beyond. [`Tests/README.md`](Tests/README.md) has the method and the generator. |
| **Checked by running the real code** | The service itself, from a config file on the storage to the strings handed to the kernel, through the message queue a carousel would send. |
| **Not checked at all** | That any of it renders. The layout bands, the assumption that three text controls fit, and the character budgets in `Render_test.cpp` are all estimates from SleepLab's numbers and have never met a panel. |

## What it shows

One question, answered: **what happens next, and when.** Not a table of today's
two times — that is a screen, and this is a card you see for three seconds.

| When | Headline | Caption |
| --- | --- | --- |
| Before sunrise | `sunrise 04:50` | `in 1h12m, sets 19:17` |
| After sunrise | `sunset 19:17` | `in 8h30m, rose 04:50` |
| After sunset | `sunrise 04:52` | `tomorrow, in 9h34m` |
| Above the Arctic circle in summer | `midnight sun / no sunset` | `the sun stays up` |
| …and in winter | `polar night / no sunrise` | `the sun stays down` |
| No position configured | `--` | `no position set` |
| A config it cannot read | `--` | `input.json rejected` |
| The watch's clock is unset | `--` | `clock not set` |
| Position and time zone disagree | the times, unchanged | `times are for home` |

The last four are the point of the app as much as the first three. Each has its
own words because each needs something different done about it, and a caption
that says the wrong one of those sends somebody looking in the wrong place.

## Where the position comes from

**You type it in once, and Kira writes it during the install.** Two fields on
the app's card — a latitude and a longitude in decimal degrees — land in
`Apps/Sun/input.json` over USB before the watch is unplugged.

That is not a compromise, and it is worth saying why, because "a watch app that
cannot find itself" sounds like a defect:

- **The accuracy needed here is loose.** Sunrise moves about four minutes per
  degree of longitude, so a position good to 25 km is good to under a minute.
  25 km is "which city", not "which street".
- **A fix would cost more than it is worth.** A cold GNSS fix takes tens of
  seconds and real battery; the carousel stops this service the moment you
  scroll past. A glance that tried to locate itself would spend the power and
  still show nothing.
- **It does not go stale in any way that matters.** The date is an input, not
  part of the position. A coordinate typed in February is exactly as correct in
  August.

### The upgrade this is built for

`Fix::Source::Cached` exists in the code and nothing produces it yet. The map
apps already receive GNSS locations; a shared last-known fix under
`SharedData/` — one writer, many readers, the arrangement
[`MapManager`](../MapManager) established for pack verification — would let this
glance follow you without ever powering a receiver itself. When that lands the
order becomes **cached, then configured**: a real fix from last weekend beats a
home you typed in and then moved away from, and this file keeps its job as the
fallback for a watch that has never had one. `Fix` already carries the
timestamp that makes an age displayable, and a configured home is timeless
(`utc = -1`) rather than pretending to be fresh.

## What it refuses to do

**It does not guess where it is.** Without a position it shows `--`. The
alternative is a default, and the default in a struct of doubles is (0, 0) —
the Gulf of Guinea, where the sun rises at about six all year round and the
screen looks perfectly healthy. A wrong answer that looks right is the only
kind of bug this app can really have.

**It does not invent a sunrise on a day that has none.** Above the Arctic and
below the Antarctic circle the equation genuinely has no solution for part of
the year. That is a state in the type system (`DayKind`), not a sentinel time,
so it cannot be rendered as `00:00` by accident.

**It says when its own position and the watch's time zone disagree.** This is
the failure a typed-in home position is prone to and nothing else can catch:
fly a few zones east, the watch syncs to local time, and the glance draws
Ottawa's sunrise against a Lisbon clock — every part behaving, the whole thing
hours wrong. So the longitude is compared against the offset, wrapped the short
way round the globe, and anything beyond four hours replaces the caption with
`times are for home`. Four hours because geography really does stretch that far
— western China runs about three hours ahead of its sun, Spain about two — and
none of those people should be told their watch is confused.

**It does not round the arithmetic into a claim it has not earned.** The
accuracy statement above is a measured disagreement with another
implementation, per latitude band, not the "±1 minute" that gets copied from
paper to paper. Near the poles the sun approaches the horizon at a shallow
angle, so hundredths of a degree become minutes, and on the day a polar night
ends no implementation's verdict is a fact about the sky.

## Why `Glance`, and what that costs

[SleepLab](../SleepLab) is a `Utility` because it has to be awake all night;
its glance is a side effect of a service that already exists. This is the
opposite. Sunrise is arithmetic over a date and a coordinate, both just as
available three milliseconds after launch as they would have been if the app
had run since boot — so there is nothing to keep warm. A `Glance`-type app's
service is started by the carousel, ticked while the card is on screen, and
returns from `run()` on `EVENT_GLANCE_STOP`, which is exactly the shape of the
work.

The cost, written down because it is not obvious: **this app cannot have a home
widget.** A widget is pushed by a service that is alive when nobody is looking,
which is the one thing this app type is not. A morning "sun sets at" widget
would mean changing the app type first.

## Layout of the code

```
Software/Libs/
├── Header/Sources
│   ├── Solar.*        NOAA's solar position calculation. No SDK, no clock, no I/O.
│   ├── Schedule.*     Which event comes next, and the time-zone sanity check.
│   ├── Render.*       The three lines, as a pure function of what is known.
│   ├── Fix.*          A position, its provenance, and a strict degree parser.
│   ├── HomeConfig.*   input.json -> a Fix, or a reason there is not one.
│   ├── InputConfig.*  A copy of Barcode's bounded JSON reader. See its header.
│   ├── WallClock.*    The one place the wall clock is read.
│   └── Service.*      The only file that touches the SDK.
```

Four of those eight compile against nothing but the standard library, which is
what lets the wording and the astronomy be argued about at a desk rather than
at dawn.

## Setting the position

From Kira, fill in the two fields on the app's card and press **Save to watch**.
Or write the file yourself onto the USB volume, at `Apps/Sun/input.json`:

```json
{
  "schema": 1,
  "values": {
    "lat": "45.4215",
    "lon": "-75.6972"
  }
}
```

Decimal degrees, north and east positive, as strings — that is the shape Kira
assembles, so it is the shape the app parses.
[`input.example.json`](input.example.json) is that file, ready to copy.

The parser is deliberately strict, and rejects rather than interprets:
`45,4215` (a decimal comma, which a lenient reader would truncate to `45` and
move sunrise half an hour), `45.4215N` (which would silently discard a
hemisphere), degrees-minutes-seconds, and anything off the globe. A rejected
file says so on the glance; a misread one would not.

The one mistake nothing can catch is swapping the two fields, when both values
happen to be valid latitudes. That lands you in the Southern Ocean, and only
the time-zone check will notice.

### The registry manifest

Kira only writes a settings file for an app that declares one. This is what
this app's `registry/sun.toml` says:

```toml
app_id     = "CCAC55621745C147"
source     = "https://github.com/tobymurray/watch-apps"
subdir     = "SunGlance"
folder     = "Sun"
licence    = "MIT"
maintainer = "tobymurray"

[config]
file   = "input.json"
schema = 1

[[config.fields]]
path      = "values.lat"
title     = "Latitude"
help      = "Decimal degrees, north positive. e.g. 45.4215 for Ottawa."
maxLength = 12
required  = true

[[config.fields]]
path      = "values.lon"
title     = "Longitude"
help      = "Decimal degrees, east positive. e.g. -75.6972 for Ottawa."
maxLength = 13
required  = true

# Plus a [[versions]] block naming the commit this was built from, which cannot
# be written until there is one:
#
# [[versions]]
# version = "0.1.0"
# rev     = "<40-character commit sha>"
# sdk_rev = "apps-v1.4.0"
# notes   = "First build."
```

`required` on both because the app shows no time at all without them, which is
what that flag is for. There is no third field naming the place: Kira needs a
value for every field it declares before it will assemble a document, so an
optional label would in practice be mandatory.

## Building

Needs `$UNA_SDK` pointing at an **`apps-v1.4.0`** checkout — the same line
[SleepLab](../SleepLab/README.md#building) targets, and for the same reason: an
app carries the kernel interface version it was built against, and a v3 app on
a v2 kernel exits instantly to an `App PID` error screen with nothing catching
it at build time.

```sh
export UNA_SDK=/path/to/una-sdk          # apps-v1.4.0
cd Software/Apps/SunGlance-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 . && cmake --build build
```

Or with Kira:

```sh
kira build-app --app SunGlance --sdk /path/to/una-sdk --version 0.1.0 --out Sun.uapp
```

`AppID` is `CCAC55621745C147` =
`sha256("https://github.com/tobymurray/watch-apps#sunglance")[0:8]`, the repo
convention.

Deploy by copying the `.uapp` into `Apps/Sun/` on the USB-MSC volume. There is
no icon yet, so Kira draws a lettered tile.

## Tests

```sh
export UNA_SDK=/path/to/una-sdk
cd Tests
cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)
```

Three executables and 64 tests: the arithmetic and the wording with no SDK at
all, the config reader over the SDK's in-memory filesystem, and the real
service driven by a scripted glance carousel. [`Tests/README.md`](Tests/README.md)
says what each is evidence about — and what the fixtures are *not* evidence
about.

## Licence

MIT — see [../LICENSE](../LICENSE).
