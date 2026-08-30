# Sun — sunrise and sunset, as a glance

A `Glance` app: one card in the carousel showing the next two things the sun
will do, for a position you set once when you install it.

> Unofficial. Not affiliated with, endorsed or sponsored by UNA Watch Ltd.

```
   [sun ^] 04:50   [sun v] 19:17        [sun v] 19:17   [sun ^] 04:52
           in 1h12m                             in 8h30m
```

Both times side by side, in the order they happen: the left is always what is
next, the right is always the one after it. **Nothing on the card is in the
past** — at two in the afternoon the right-hand time is tomorrow's sunrise, not
this morning's.

Side by side rather than stacked because the panel is wide and short. Stacking
made height the binding constraint, which is what clipped the digits on the
first hardware run; beside each other, the times get the panel's full height and
the constraint moves to width, where there is room. The layout falls back to
stacking on a panel too narrow to hold two times without them colliding.

The times carry no words because the icons say which event each one is, in a
fraction of the space "sunrise" takes — and what is actually being asked at
seven in the morning is not "when is sunrise" but "how much daylight is there",
which is a question about the pair.

There is no GUI. Tapping the card opens nothing, because everything this app
has to say fits on that card — which is the definition of a glance, and the
reason it is 36 KB and runs only while you are looking at it.

## Status: on a watch, and the panel is finally a measurement

Everything below builds against `apps-v1.4.0` and **84 host tests pass**,
including seventeen that drive the real service through a scripted glance
carousel. Two rounds of hardware feedback so far, and both were about the panel
rather than the sun: the first clipped the bottom of a stacked row, the second
found the left icon sitting under the carousel's scroll indicator.

**The glance area is 240×60.** Not the 241×88 this was written against — a third
shorter, which means the stacked arrangement could never have worked here at any
font size, and the drawable width is narrower still because the scroll indicator
is painted over the left edge. That number came off the watch in `glance.txt`,
and every layout constant now derives from it. So:

| | |
| --- | --- |
| **Checked against an independent implementation** | Sunrise and sunset for 52 place-days against [`astral`](https://pypi.org/project/astral/) 3.2, plus a 1825-day polar sweep. Worst disagreement: 26 s below 55° latitude, 72 s to 66°, 118 s beyond. [`Tests/README.md`](Tests/README.md) has the method and the generator. |
| **Checked by running the real code** | The service itself, from a config file on the storage to the strings handed to the kernel, through the message queue a carousel would send. |
| **Checked on the watch** | That it draws, that the position is read, that the times are right, and — now — how big the glance area actually is and where its usable part starts. |
| **Not checked at all** | Whether font 25 is the largest that fits, or whether the 18-pixel left inset is exactly right rather than merely enough. Both are now tuned against `glance.txt` rather than against another app's numbers, which is the difference between the first two rounds and any that follow. |

## What it shows

One question, answered: **what happens next, and when.** Not a table of today's
two times — that is a screen, and this is a card you see for three seconds.

| When | Left | Right | Caption |
| --- | --- | --- | --- |
| Before sunrise | ⬆ `04:50` | ⬇ `19:17` | `in 1h12m` |
| After sunrise | ⬇ `19:17` | ⬆ `04:52` *(tomorrow's)* | `in 8h30m` |
| After sunset | ⬆ `04:52` | ⬇ `19:14` | `tomorrow, in 9h34m` |
| The last sunset before the midnight sun | ⬇ `19:17` | *(empty)* | `in 3h05m` |
| Above the Arctic circle in summer | `no sunset` | | `the sun stays up` |
| …and in winter | `no sunrise` | | `the sun stays down` |
| No position configured | `--` | | `no position set` |
| A config it cannot read | `--` | | `input.json rejected` |
| The watch's clock is unset | `--` | | `clock not set` |
| Position and time zone disagree | the times, unchanged | | `times are for home` |

The bottom half of that table is the point of the app as much as the top half.
Each state has its own words because each needs something different done about
it, and a caption that says the wrong one of those sends somebody looking in the
wrong place.

The empty right-hand side is not an oversight: on the day of the last sunset
before the midnight sun there is no next sunrise, and a slot is better empty
than filled with a time that is not coming.

## The icons

Two of them, 24×21, drawn by [`Tools/draw_icons.py`](Tools/draw_icons.py) and
converted by the SDK's own `png2abgr2222.py`:

```sh
python3 -m venv .venv && .venv/bin/pip install pillow
.venv/bin/python Tools/draw_icons.py
.venv/bin/python "$UNA_SDK/Utilities/Scripts/png2abgr2222/png2abgr2222.py" \
    --inputs Resources/icon_sunrise.png Resources/icon_sunset.png \
    -o Software/Libs/Header/Icons.h
```

The shapes are in a script rather than in a drawing program because a PNG in a
diff is a wall nobody can see over — "make the arrow bigger" is a comment on a
file that cannot be reviewed. The script prints an ASCII preview of what it drew.

A glance image is one byte per pixel, **two bits per channel**, so there are
four levels of everything and no antialiasing to lean on. The first version had
rays on the sun; at this size they came out as lopsided blobs that merged into
the disc, and the icon read as a smudge. What is left is a disc resting on a
horizon with an arrow under it — the arrow in white because it carries the
meaning, the horizon in grey because it is only context.

The kernel decides how many controls a glance may have, and this screen wants
five: two icons, two times and a caption. Every SDK glance example asks for
three. If the kernel offers fewer than five the icons are dropped and the rows
say `rise 04:50` and `set 19:17` instead; below three the glance is declined
outright, because two times with no way to tell which is which is worse than no
glance at all.

## The layout, and why none of it is constant

The first version put the two times at fixed heights — 32 pixels each for a
font-30 line — and on the watch the bottom of the second one's digits was cut
off.

The numbers had been copied from [SleepLab](../SleepLab), which draws **one**
line at font 30 and gives it **36** pixels. That ratio, about 1.2, is the part
worth copying; the absolute numbers are not, because one line and a caption fit
in a panel that two lines and a caption do not.

So there are no positions left in the service. `Sun::layoutFor()` takes the area
the kernel reports and works out every box in it, considering four shapes in
this order:

1. side by side, with icons
2. stacked, with icons
3. side by side, words instead of icons
4. stacked, words instead of icons

Icons before font size, because they are what lets the times carry no words at
all; between two shapes that both keep them the larger font wins, and side by
side breaks the tie. If none of the four fit, the glance is declined rather than
drawn cut off.

On the real 240×60 panel that comes out as **side by side, font 25, icons kept**
— icons at x21 and x130, times 70×30 beside them, the caption across the bottom
at y37. A 180-pixel-wide panel drops the icons to keep the font; a 200×60 one
keeps them at font 18; a 120-wide one stacks. Stacking cannot happen on *this*
watch: two lines and a caption need 63 pixels before any margin, and there are
60.

### The part of the panel that is not drawable

The kernel reports 240 wide; the carousel then paints a scroll indicator down
the left edge, **over** the glance. The first build to reach a watch centred its
content in the reported width, put the left icon at x=11, and had its edge
clipped. Everything except the caption is now laid out inside an 18-pixel left
inset — the same x SleepLab's main lines use — with 8 on the right. The caption
still spans the full width because it is centred text: what the indicator passes
over is its margin, not a glyph.

Every text box is exactly its line height and centred in its band, rather than
being handed the whole band. `GlanceText_t` has no vertical alignment, so a box
taller than its line leaves the kernel to decide where in it the glyphs sit —
and that decision is the one nobody here can see.

### `glance.txt`

Beside `input.json`, written when it changes, so the next round of tuning is
done against a measurement rather than another app's numbers:

```
# what the kernel offered, and what was drawn from it
area 240x60
side-by-side font25 icons yes fits yes
first 50,3 70x30  second 159,3 70x30
sub 0,37 240x21
```

It is written *before* the app decides whether the panel is drawable, because a
panel nothing fits in is exactly the case somebody needs the measurement for,
and a glance that declines silently tells them nothing.

## Layout of the code

```
Software/Libs/
├── Header/Sources
│   ├── Solar.*        NOAA's solar position calculation. No SDK, no clock, no I/O.
│   ├── Schedule.*     The next two events, and the time-zone sanity check.
│   ├── Render.*       What the screen says and where it goes, as pure functions.
│   ├── Icons.h        Generated. The two icons, as ABGR2222 bytes.
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

### What makes the phone ask

A phone only collects a value for an app that declares it wants one, and the
declaration lives in [`app-manifest.json`](app-manifest.json) — `configFile`
naming the file to write, `configFields` naming what goes in it. The ids are the
keys under `values`, which is why they are `lat` and `lon` and why `HomeConfig`
reads `values.lat` and `values.lon`:

```json
{
  "configFile": "input.json",
  "configFields": [
    {
      "id": "lat",
      "type": "string",
      "label": "Latitude",
      "description": "Decimal degrees, north positive. 45.4215 is Ottawa. …",
      "default": "",
      "maxLength": 12,
      "pattern": "(?:-?\\d{1,2}(?:\\.\\d{1,6})?)?",
      "required": true,
      "validationMessage": "Decimal degrees between -90 and 90, e.g. 45.4215. …"
    },
    { "id": "lon", "…": "the same, to 180 and 13 characters" }
  ]
}
```

**`string` and not `float`**, though the SDK offers both. A float field would get
a numeric keypad and range validation on the phone, which sounds strictly better
— and would probably work, because `InputConfig::getString` returns coreJSON's
raw slice without checking the token type, so an unquoted `45.4215` would reach
`parseDegrees` intact. But every fixture in `HomeConfig_test.cpp` is a quoted
value, so that is an untested path, and the keypad is not worth taking it.

**The patterns are the phone's manners, not the app's rules.** They accept what
`parseDegrees` accepts and refuse the two mistakes above — the decimal comma and
the hemisphere suffix — so those get caught on the form instead of on the glance.
They do not enforce range: `91` satisfies the pattern and is refused by the app,
because a regex that spells out ±90 correctly is longer than the check it
duplicates and can disagree with it. Both admit the empty string, because a
declared `default` has to satisfy its own constraints.

**`required` on both**, because the app shows no time at all without them, which
is what that flag is for. There is no third field naming the place: every
declared field has to be filled before the phone will assemble a document, so an
optional label would in practice be mandatory.

### Why it is typed in and not sensed

The obvious objection is that the watch has a GNSS receiver and this is a
coordinate. Three reasons it is not used, in increasing order of how long they
will stay true.

A glance cannot take its own fix. The service runs only while the card is on
screen and returns from `run()` when it scrolls away; a cold fix is tens of
seconds and real battery. The card would be gone before the receiver had
anything.

Nothing records one for it, either. GNSS reaches an app through the sensor
layer, and the four apps in this repository that read it — `RunMap`, `HikeMap`,
`BikeMap`, `GpsLab` — are all `Activity` apps that run when you start a ride or a
run and record into their own folders. There is no shared last-known fix under
`SharedData/`, and no call anywhere in the SDK that hands back a position without
powering the receiver. `Fix::Source::Cached` exists in the enum for the day one
appears, and is documented as not yet produced because it is not.

And on the day one does appear, this field stays. The ordering in
[`Fix.hpp`](Software/Libs/Header/Fix.hpp) is cached-then-configured, with the
configured home as the fallback for a watch that has never had a fix — which is
every watch on the day it is unboxed, and permanently for somebody who wanted a
sunrise glance and does not run.

Which is fine, because for this question a typed position is not a compromise.
Four minutes of sunrise per degree of longitude means a position good to 25 km is
good to under a minute, and 25 km is "which city". A home typed once is right
until you move to another one, which is why it is stored with `utc = -1` —
timeless rather than fresh. What a cached fix would actually buy is the
traveller, whose glance shows the sunrise they left behind until they edit the
field.

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

Deploy by copying the `.uapp` into `Apps/Sun/` on the USB-MSC volume.

There is no *app* icon — the pair of 30×30 and 60×60 PNGs that the launcher and
Kira's card use — so `APP_USE_ICONS` is `Off` and Kira draws a lettered tile.
The two icons above are the glance's own artwork and are compiled into the
service; the two are unrelated.

## Tests

```sh
export UNA_SDK=/path/to/una-sdk
cd Tests
cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)
```

Three executables and 84 tests: the arithmetic, the layout and the wording with
no SDK at all, the config reader over the SDK's in-memory filesystem, and the real
service driven by a scripted glance carousel. [`Tests/README.md`](Tests/README.md)
says what each is evidence about — and what the fixtures are *not* evidence
about.

## Licence

MIT — see [../LICENSE](../LICENSE).
