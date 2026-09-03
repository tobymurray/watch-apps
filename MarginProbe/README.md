# MarginProbe — where the glance area actually ends

Eight glance apps that differ in one number. Each draws an unfilled rectangle
inset that many pixels from every edge of the area the kernel reports, and says
which one it is. Scroll through them on the watch; the first card whose border is
whole on all four sides is the drawable area.

> Unofficial. Not affiliated with, endorsed or sponsored by UNA Watch Ltd.

```
Edge0                            Edge8
┌──────────────────────────────┐ ┌──────────────────────────────┐
│▓                             │ │                              │
│▓        Edge0                │ │      ┌────────────────┐      │
│▓   240x60  inset 0  c5       │ │      │ Edge8          │      │
│▓                             │ │      │ 240x60 inset 8 │      │
└──────────────────────────────┘ └──────┴────────────────┴──────┘
 ^ left edge eaten by the          ^ border clear of it
   carousel's scroll indicator
```

## Why this exists

`SunGlance` reached a watch twice with something cut off, and the fix both times
was a constant somebody guessed. Those constants are still guesses:

| | | |
| --- | --- | --- |
| `kSafeLeftInset` | 18 | "what SleepLab's main lines use and render cleanly" |
| `kSafeRight` | 8 | "a couple of pixels of margin keeps a time from ending flush against the bezel" |
| `kBottomGuard` | 2 | "nothing here can tell whether that number includes a border the carousel draws over" |

Every one of those is a measurement of *another app looking fine*, not of where
the edge is. Eighteen might be twelve. The right margin has never been tested at
all. This app is how those three numbers stop being guesses.

## Round 1: the boundary is circular

Eight box builds went on a watch and were photographed. The result, and the
reason this directory now has a second style:

| Inset | Side bars visible | Circular model predicts |
| --- | --- | --- |
| 0 | ~32% of band height | ~0-50% (the bar straddles the widest point) |
| 2 | ~77% | **78%** |
| 4 | ~87% (detection floor, not clipping) | 100% |

**All four sides are drawn even at inset 0.** The left and right bars are not
missing, they are *clipped to the chord* -- present across the middle of the
band and gone at the corners. That is the signature of a round display, not of a
rectangular clip, and it fits one model closely: a disc of radius ~120 with the
240x60 band centred on it, so the band's full width is exactly the diameter and
its own corners are off the glass.

Which gives the number this was built for:

```
full band height available while |dx| <= sqrt(120^2 - 30^2) = 116.2
  -> minimum uniform inset for an unclipped rectangle = 3.8, call it 4
```

And a second, more useful one: a text line 30 px tall centred in the band has
`sqrt(120^2 - 15^2)` = 119.1 of half-width available, which is essentially the
whole 240. **So the glass costs 4 px at the corners and nothing at all in the
middle rows** -- and `SunGlance`'s `kSafeLeftInset = 18` therefore is not the
glass. It is the scroll indicator, which is visible in every photograph as an
*arc* hugging the bezel, and an arc needs a different allowance per row.

Two other things fell out:

- **`maxControls` is 32**, not the 5 `SunGlance` treats as a luxury. Its
  words-instead-of-icons fallback is dead code on this watch.
- **Poppins Medium 10 has no letter glyphs here.** `240x60  inset 0  c32`
  rendered as `240?60????????0???32`. Digits and `-` survive, so the sub-line is
  now digits only.

## Reading the result

Each card carries its own identifier and the geometry it was built against, so a
photograph is evidence on its own:

```
Edge8
240x60  inset 8  c5
        ^ area   ^ inset   ^ maxControls the kernel granted
```

Two things to look for, and the second is the one that pays:

1. **Which card is the first with an unbroken border.** That inset is the safe
   margin, uniformly.
2. **Which *edge* is broken on the cards that fail.** A rectangle has four sides
   and they are clipped independently, so a card whose left is gone but whose top,
   right and bottom are intact says the left inset needs to be bigger and the
   other three do not. One pass through the series gives a number per side.

Each app also writes `probe.txt` into its own folder, readable over USB:

```
# what the kernel offered, and what was asked of it
name Edge8
inset 8
area 240x60
maxControls 5
rect 8,8 224x44
```

`maxControls` is worth having on its own. `SunGlance` asks for five and falls
back to three, and nothing anywhere records what this kernel actually grants.

## Building

Needs `$UNA_SDK` pointing at an `apps-v1.4.0` checkout. One command builds the
series:

```sh
export UNA_SDK=/path/to/una-sdk
./Tools/build_all.sh                     # Output/Edge{0,2,4,6,8,12,16,20}_1.0.0.uapp
INSETS="1 2 3" VERSION=1.1.0 ./Tools/build_all.sh   # or any other sweep

# the staircase, which is one app and takes no inset
cd Software/Apps/MarginProbe-CMake
cmake -B build-stair -G "Unix Makefiles" -DPROBE_STYLE=stair -DPROBE_INSET=0 \
      -DBUILD_VERSION=1.0.0 . && cmake --build build-stair
```

Deploy by copying each `.uapp` into `Apps/Edge<N>/` on the USB-MSC volume, then
power-cycling. The watch scans `Apps/` and registers what it finds — the names in
`app_list.json` are read out of the `.uapp`, not from the folder — so nothing else
on the volume needs editing.

## Round 2: the staircase (`-DPROBE_STYLE=stair`)

One rectangle samples one column per side, so the box series could establish
*that* the boundary is circular but not trace its shape. The staircase stands a
full-height bar in each of nine columns from both edges -- x = 0, 2, 4, 6, 8, 10,
12, 16, 20 -- plus a grey datum across the middle. Every bar is clipped to the
chord at its own column, so they come out as a literal staircase and the whole
boundary curve is in one photograph.

The point is that it is read by **counting bars**, not by measuring one. Round 1
foundered on exactly that: a single bar's visible height had to be measured in
photo pixels against a curved glass at an off-axis angle, and the right-hand bar
read systematically shorter than the left at every single inset -- lighting, not
geometry. Counting is immune to both.

Two pixels apart and not one, because one bar plus one gap is the finest pitch a
photograph of this panel resolves: round 1 measured a 1px line as a five-pixel
run, so a 1px pitch would merge into a smear.

## Round 3: the ruler (`-DPROBE_STYLE=ruler`)

The staircase drew correctly and both edges came back as clean staircases -- and
reading it still failed, because reading it meant measuring photo pixels. Two
independent calibrations of the *same photograph* disagreed by a factor of two:
the bar pitch gave 7.5 photo pixels per display pixel, the bar height gave 3.7.
Nothing about a hand-held shot of a curved glass settles which is right.

So the ruler stops measuring and starts naming. Six adjacent 1px columns from
each edge, one colour per display pixel:

| column | 0 | 1 | 2 | 3 | 4 | 5 |
| --- | --- | --- | --- | --- | --- | --- |
| colour | red | green | blue | yellow | cyan | white |

The outermost stripe that survives names its own column. *The outermost stripe is
blue* means column 2 is the first that draws and the drawable inset is 2 -- no
scale, no arithmetic, no argument about what the camera was doing. Six columns
because rounds 1 and 2 bracketed the answer between 0 and 4.

Six hues that stay apart at two bits a channel and through a lens. Nothing that
relies on a shade -- grey against white, brown against yellow -- because a
reading error is the one failure this style exists to rule out.

### Drawable is not readable

Both edges carry the same six, and that is the other half of the point. The
right edge is clean glass. The left edge is glass **plus the carousel's scroll
arc, which is painted over the glance rather than beside it** -- visible in every
photograph so far as a pale curve hugging the bezel, and clearly overlapping the
left-hand bars in round 2.

So the two edges answer two different questions:

| | measures |
| --- | --- |
| right edge | what the hardware **can draw** -- the glass boundary alone |
| left edge | what a wearer **can read** -- glass, minus whatever the arc covers |

`SunGlance`'s `kSafeLeftInset = 18` currently answers both with one number, and
the difference between the two edges in one photograph is exactly the arc's
footprint.

## Round 3 result: the drawable boundary, solved

The ruler's *colours* failed. The panel is a backlit LCD at two bits a channel
and the backlight is blue, so every hue is dragged toward it: red came back as a
magenta tinge at the extreme edge, green landed on cyan and could not be told
from white, and the bright colours bloomed over the dark single-channel ones. Six
hues chosen to be distinct in RGB222, all collapsing into one channel on glass.

The ruler's *shape* worked, and that turned out to be the answer. The block's
outer edge is stepped -- one step per display pixel, ~15 photo pixels each --
and tracing the outermost lit column row by row gives the boundary directly:

| rows from band centre | drawable inset | model `120 - sqrt(120^2 - dy^2)` |
| --- | --- | --- |
| 0 (the datum) | **0** | 0 |
| +/-15 | 0.7 - 1.3 | 0.9 |
| +/-22 | 2.6 - 2.7 | 2.1 |
| +/-30 (band edge) | ~4.1 | 3.8 |

Symmetric about the datum and matching the circle to within a pixel at every
sample. The reading is scale-independent: the step size is calibrated from the
block itself, and the curve bottoming out at exactly 0 at the datum is the
self-check that earlier rounds lacked.

```
inset(y) = 120 - sqrt(120^2 - (y - 30)^2)      for the 240x60 band
```

**What this means for `SunGlance`.** Its rows are ~30 px tall and centred, so
`|dy| <= 15`, where the glass costs **one pixel**. `kSafeLeftInset = 18` is
therefore about seventeen pixels of something that is not the glass -- and on a
240-wide panel that is seventeen columns spent on an unexamined guess.

## Round 4: the arc probe (`-DPROBE_STYLE=arc`)

Which leaves the other half of the question, and the half that actually costs
width: how much does the scroll arc cover? Round 3 could not say, because on the
left edge the arc and the ruler are both bright and overlapping -- under a blue
backlight they land at the same luminance and merge into one blob.

So this style stops using hue entirely and paints the band mid grey, letting the
two things being measured announce themselves as the levels either side of it:

| level | means |
| --- | --- |
| **black** | the glass cut it -- nothing reaches the panel here |
| **grey** | drawn, and nothing on top of it |
| **white** | drawn, and the scroll arc is painted over it |

Three levels a blue backlight cannot collapse, and no stripes to resolve, so
bloom stops mattering. Everything drawn on top of the fill -- datum, ticks, label
-- is **black**, because black is the one value the arc can neither wash out nor
be mistaken for.

The ticks are the ruler: black 1px columns at 5, 10, 15, 20, 25 and 30 from each
edge. The arc hides the ones it covers, so **the innermost hidden tick names the
arc's reach** -- the number `kSafeLeftInset` has been standing in for since it
was guessed.

## Round 5: the proof (`-DPROBE_STYLE=proof`)

Four rounds produced a claim. This one states it in a form the panel can refute.

```
a pixel is drawable iff it lies inside the circle of radius w/2 centred
on the band, so the leftmost drawable column in row y is

    xL(y) = ceil(w/2 - sqrt((w/2)^2 - dy^2) - 0.5),   dy = y + 0.5 - h/2
```

Three filled staircases, painted one over the next, so each covers the last
everywhere except the single pixel where they disagree:

| | | |
| --- | --- | --- |
| **mid grey** | one pixel *outside* `xL` | drawn first, **expected to vanish entirely** |
| **white** | exactly *at* `xL` | **expected as an unbroken 1px ring** |
| **dark grey** | one pixel *inside* `xL` | the field the ring sits on |

If the model holds: a dark grey field, a white ring all the way around it, and no
mid grey anywhere on the panel. Three ways it can fail, each informative:

| what you see | what it means |
| --- | --- |
| mid grey outside the ring | the glass reaches further than claimed |
| the white ring broken | the glass stops short there |
| white thicker than a pixel | the model has the wrong curvature |

Greys and not hues, deliberately. Round 3 established that this backlight drags
every colour toward blue and that adjacent bright and dark hues bloom into each
other. Three neutrals at 85, 170 and 255 shift *together* under that bias, so the
bias cancels and only luminance carries the signal.

The outside ring cannot be tested everywhere, and that is geometry rather than an
oversight: across the middle rows `xL` is already 0, so one pixel further out is
off the band and there is nothing to draw. Those rows clamp to the boundary and
are painted over by it. The test has force exactly at the corners -- which is
where the arc bites and where the whole question has lived since round 1.

25 rectangles at 240x60, plus the label and sub-line: 27 of the 32 controls the
kernel grants. The loop stops at `maxControls - 2` regardless, because a form
built past the grant would be a silent truncation, and this app of all of them
must not quietly draw something other than what it claims.

## One source tree, nine apps

`PROBE_INSET` is a compile definition, and everything that distinguishes one
build from another is derived from it: the inset, the name on screen, the folder
on the watch, and the AppID. There is no second place to forget.

The AppIDs are computed by CMake, not pasted:

```cmake
string(SHA256 PROBE_ID_FULL "https://github.com/tobymurray/watch-apps#marginprobe${PROBE_INSET}")
string(SUBSTRING "${PROBE_ID_FULL}" 0 16 PROBE_ID)
```

which is the repo convention, and reproduces `SunGlance`'s `CCAC55621745C147`
from its own URL. Eight hand-copied ids would be eight chances to install two
apps over each other, and the watch keys its folders on that id, so a collision
looks exactly like the build not having been copied.

Each inset gets its own `build-<N>/` directory. They are eight different apps
that happen to share a source tree, and a shared `build/` has them overwrite each
other's objects and emit eight copies of whichever ran last.

## Not wired into CI

`.github/workflows/app-build.yml` discovers one app per directory and requires a
version from an `app-manifest.json`. This directory is eight apps and has no
manifest, because there is nothing to configure and nothing to publish. It is a
measuring instrument with a shelf life — when the three constants above are
numbers, this can be deleted.

## Licence

MIT — see [../LICENSE](../LICENSE).
