# Barcode — Code 128 barcodes for values you supply

Displays a parkrun-style **Code 128** barcode, plus the ID underneath it in
readable text. The IDs are not compiled in: you type them into the UNA phone app
when you install this one, and the watch reads them at launch.

It holds **up to six**, so a parent can carry the whole family's parkrun IDs and
step between them at the finish funnel with `L2`. That is what every comparable
app on Garmin and Apple does, and it is the one thing a single-code app cannot
be asked to do at the moment it matters.

That second half is the point. The watch has four buttons and no keyboard, so a
value only you know — an athlete ID here, but the shape is the same for a
transit pass or a membership number — has to come from outside.

This app was originally built because the SDK had no supported way to get one
in; it read a file it defined itself, and it was a working answer to
[UNAWatch/una-sdk#225](https://github.com/UNAWatch/una-sdk/issues/225). **The
SDK now ships that answer** — `configFields` in `app-manifest.json`, collected
by the companion app and read on the watch through `SDK::AppConfig` — so the
app's own reader is gone and this is built on the platform feature instead.

The file it reads did not change. The envelope this app invented and the one
`SDK::AppConfig` defines are the same document, down to the `schema` key, so an
`input.json` already sitting on a watch keeps working and the USB route below
still does what it always did. That is not luck: the original copied
`SDK::Variant::Config`, and so did the SDK.

What did change is what the app can *say* when something is wrong, and that is
a real loss — see [Concessions](#concessions).

## Setting your codes

### From the phone

The app declares six numbered slots, so the UNA app asks for them while it
installs — a flat list of twelve rows, an ID and a name per code:

> **Code 1 ID** — The number printed under your barcode, copied exactly. Case
> matters. This is the code the watch shows first.
> **Code 1 name** — What the watch calls this code, so you know which is which
> when you cycle through them. Optional.
> **Code 2 ID** — A second barcode to carry — a family member's parkrun ID, a
> membership number. Leave empty if you do not need it.
> … and so on to Code 6.

Only **Code 1** is required; the rest are left empty unless you want them. The
names are what the watch shows above the bars, so `Sam` beats `Code 3` when you
are looking for the right one in a hurry. Editing later is the same screen, and
new values apply the next time you open the app.

Slots do not have to be contiguous — filling 1, 3 and 6 gives you three codes,
and the watch cycles between exactly those three.

### Or over USB

The values file is a plain file in the app's own folder, so it can still be
written by hand:

1. Connect the watch by USB and wait for the drive to mount.
2. Open `Apps/<APP_ID>/` on it — the same folder the `.uapp` lives in. The
   watch names that folder after the app's ID, not its name.
3. Create `input.json`:

   ```json
   {
     "schema": 1,
     "values": {
       "id1": "A1234567",
       "name1": "Me",
       "id2": "A7654321",
       "name2": "Sam"
     }
   }
   ```

   [`input.example.json`](input.example.json) is that file, ready to copy.
   Leave out any slot you do not want.
4. Eject the drive safely, unplug, and power-cycle the watch.

Until an id arrives by one route or the other, the app says so on screen and
draws no barcode. There is deliberately no usable id to fall back on: a barcode
is an identity claim, and a plausible placeholder that scans is worse than a
blank screen — at a parkrun finish it credits somebody else's run.

### What the id may contain

1 to 16 printable ASCII characters. The phone enforces that as you type; a
hand-written file is checked on the watch. Either way an id that does not fit is
**refused, not trimmed** — a shortened id is a *wrong* id, and a barcode that
scans as someone else's number is the one genuinely harmful thing this app could
do. Keeping that true through the migration took a deliberate trick; see
[Concessions](#concessions).

### Changing it later

Edit it on the phone, or overwrite `input.json` and relaunch. The app re-reads
whenever the GUI resumes. Note that USB mass storage detaches the volume from
the watch while your computer holds it, so on that route the reboot after
ejecting is what picks the new value up.

## Why a separate file, and not `settings.json`

Every UNA activity app keeps its own `settings.json` in this same folder, so
that would be the obvious place to put an id. It is the wrong one — and the SDK
agrees: `configFile` is a file of its own by design, and the companion app is
forbidden from writing anywhere else in the app's directory. The reasons were
worth writing down before that was settled, and they still hold:

- **The app rewrites it whole.** `SettingsSerializer::save()` serialises the
  in-memory struct with `open(truncate)`, so any key the app does not know about
  is destroyed the first time the user touches a settings screen. A value
  written from outside would silently vanish.
- **It is the app's own file.** Keeping externally-written data somewhere else
  makes "this came from outside, validate it" a property of the filename rather
  than something to remember.
- **The name is plausibly spoken for.** UNA's phone app was observed reading a
  `settings.json` off the watch during a sync. Squatting in it risks a
  collision; a separate file risks at worst an orphan.

## Where the format comes from

`input.json` was never invented here. Its shape copied **`SDK::Variant::Config`**
— the SDK's existing bounded-JSON-config reader, the one that gives a code-less
variant alias its name and FIT identity — because that was the platform's own
answer to "read a config somebody else wrote, and never let it stop the app
starting":

| Rule | Why it was worth copying |
| --- | --- |
| A `schema` major that must match **exactly** | An unknown major falls back entirely instead of guessing at rearranged keys |
| An app-owned subtree the reader treats as opaque (`features` there, `values` here) | The vocabulary belongs to the app; a shared reader never needs to know these key names |
| A size ceiling checked **before** anything is allocated | 8 KB there, 4 KB here |
| Every failure falls back to a default | A config somebody else wrote must never stop the app starting |

`SDK::AppConfig` reaches for the same four rules, which is why the migration was
a deletion rather than a translation: the file on disk is unchanged, and the
8 KB ceiling is now the SDK's rather than this app's 4 KB one.

## Concessions

Three things were lost moving onto the platform feature. They are listed here
rather than buried because two of them are visible to whoever is holding the
watch.

**The screen can no longer name the fault.** The old reader reported six
distinct states — absent, too large, unreadable, not JSON, wrong schema, ok —
because on a device with no keyboard the failure has to be legible from the
watch itself. `SDK::AppConfig` reports every one of those as `isLoaded() ==
false` and writes the detail to a log nobody wearing the watch can read. The
prompt is now one message covering the lot, pointing at the phone and the file
instead of naming what is wrong.

**There is no "unset" — but the empty string does the job.** Every field in
`app-manifest.json` must declare a `default` that satisfies its own constraints,
so an app cannot say "there is no safe value for this". Earlier versions of this
app answered that with a sentinel: a single space, recognised by an explicit
comparison because a space is a legal Code 128 character. That is gone. Since
these fields declare **no `minLength`**, the empty string is itself a legal
value, so `""` is the default and "empty" means "not set" with no magic constant
anywhere.

That is worth knowing generally: a field that declares `minLength: 1` forces you
to invent a placeholder, and one that does not lets you say nothing. It does not
fix `required: true` being weaker than it looks — the SDK counts accepting the
pre-filled value as satisfying a required field, so an empty Code 1 can still
reach the file — but the app reads that as "no code" and says so, which is the
right outcome anyway.

**Truncation had to be bought back.** `SDK::AppConfig` truncates an over-long
string to the declared `maxLength` on a UTF-8 boundary and tells the caller
nothing about having done it. A field declared at 16 would hand back the first
16 characters of a 30-character value as though that were the id — a *wrong* id
that still scans, which is the one outcome this app exists to prevent. So the
field is declared at **17**, one byte longer than an id can be: anything too
long arrives at 17 bytes, the length check refuses it, and the wearer is told.
The phone never offers a 17-character value because the field's pattern caps
entry at 16, so the extra byte is reachable only from a hand-edited file —
exactly the untrusted path that needed defending.

Two things were gained, for balance: the id can now be typed on a phone keyboard
with validation as you go, and a backslash in the id works, because
`SDK::AppConfig` decodes JSON escapes before the app sees the value. The old
reader had to refuse one, since coreJSON handed it the escape sequence
undecoded.

## Layout

```
app-manifest.json                 # package metadata + the one configuration field
Software/
├── Libs/
│   ├── Header/AppConfigFields.hpp # the app's copy of the fields, CI-checked
│   ├── Header/Barcode.hpp        # kMaxCodes, the codes, and why there is no fallback
│   ├── Header/Code128.hpp        # header-only Subset B encoder, no SDK dependency
│   ├── Header/Commands.hpp       # the two-message contract between the halves
│   ├── Sources/AppConfigFields.cpp
│   └── Sources/Service.cpp       # reads the config, publishes the result
└── Apps/
    ├── Barcode-CMake/            # build glue
    └── TouchGFX-GUI/             # the GUI: bars, or a prompt saying what to do
```

The Service owns the codes and the GUI only renders what it is sent, so the
configuration is read in exactly one place — which is also what `SDK::AppConfig`
requires, since it is one instance per app on one thread. Cycling is the one
thing the GUI does alone: it already has every code, so `L1`/`L2` never round-trip.

### Changing how many codes it holds

`Barcode::kMaxCodes` is the single number. The service loop, the message, the
screen and the cycling are all written against it, so **no logic changes** — but
the *declaration* is per-field by construction, so raising it means adding an
`idN`/`nameN` pair in two places: `configFields` in `app-manifest.json`, and
`kFields` in `AppConfigFields.cpp`. (The SDK's `--check-bounds` reads that table
as text and rejects named constants, so it cannot be generated from a loop.)

Two `static_assert`s make a mismatch a build error rather than a silent bug:

- `AppConfigFields.cpp` fails if the table is not `kMaxCodes * 2` entries
- `Commands.hpp` fails if a full state no longer fits a 256-byte message block

**Seven is the ceiling**, and it is that second assert that sets it — not the
SDK's 32-field limit, which would allow sixteen. A `Code` is 30 bytes and a
`State` is 182 at six; eight would overrun the pool block.

`app-manifest.json` never reaches the watch, so the binary carries its own copy
of what it declared. The two must agree, and CI is what makes sure they do:

```sh
python3 $UNA_SDK/Utilities/Scripts/app_packer/validate_app_config.py \
    --check Barcode/app-manifest.json \
    --check-bounds Barcode/Software/Libs/Sources/AppConfigFields.cpp
```

`minKernelVersion` is not hand-written either. It is a floor derived from the
SDK ABI, so let the resolver set and check it:

```sh
python3 $UNA_SDK/Utilities/Scripts/app_packer/min_kernel_version.py \
    --stamp Barcode/app-manifest.json    # raise it to the floor
python3 $UNA_SDK/Utilities/Scripts/app_packer/min_kernel_version.py \
    --check Barcode/app-manifest.json    # verify it is >= the floor
```

### Icons

There are **two**, and they are not the same picture:

| File | Where it goes | Constraints |
| --- | --- | --- |
| `Resources/icon_60x60.png`, `icon_30x30.png` | baked into the `.uapp`, shown in the watch menu | Converted to **ABGR2222** — one byte per pixel, two bits per channel. Four shades, four alpha levels, by truncation. |
| `Resources/icon_store.png` → packaged as `icon.png` | the phone's app store, re-hosted as an ordinary PNG | None. 512×512 full colour. |

Both are drawn by scripts rather than kept as hand-made PNGs, because the watch
pair is pixel-critical: a bar narrower than one whole pixel downsamples to grey,
and grey truncates to a muddy mid-tone instead of black. The bars are therefore
drawn at 1× on integer boundaries and never resampled, while the rounded corner
— the one place antialiasing is wanted — is drawn at 16× and downsampled.

```sh
python3 Barcode/Resources/make_icon.py       Barcode/Resources   # watch, 60 + 30
python3 Barcode/Resources/make_store_icon.py Barcode/Resources   # store, 512
```

`make_icon.py` reports the left/right bar margins and any shade that survived
quantisation muddy, so a change that unbalances or greys the icon says so.

`id` in the manifest is the same value as `APP_ID` in
[`Barcode-CMake/CMakeLists.txt`](Software/Apps/Barcode-CMake/CMakeLists.txt),
and the two have to stay equal: it is how the store tracks the app and how the
phone matches a new `.uapp` to the installed one when the file name changes.
The build prints it, so it can be checked rather than assumed —
`INFO:root:ID : 409506B8B69EC13E` in the `app_merging.py` output.

## Fitting the id on a round screen

The id under the bars is the human-readable half of the barcode: what someone
reads out or types in when a scanner will not cooperate. It is also the widest
thing on the face, and the face is a circle, which the TouchGFX Designer's
geometry knows nothing about — `textArea1` was 203px wide at x=19, which is 203px
of a *square*. The id sits low on the panel, so a long one had its first and last
character shaved off by the bezel.

**The simulator cannot show this.** It draws the full 240×240 square with no
bezel, so the clipping is invisible there and reached the watch twice before it
was caught. Mask the simulator's screenshots with a 120px-radius disc first:

```sh
# The lit area, as BarcodeLayout::pixelIsLit models it: a pixel counts as lit
# only if its centre is within 119.5 dot pitches of the centre. Half a pitch
# inside the datasheet's active area, deliberately -- see Tests/README.md.
python3 - <<'PY' > disc.pgm
import sys
rows = [bytes(255 if (2*x-239)**2 + (2*y-239)**2 <= 239**2 else 0 for x in range(240))
        for y in range(240)]
sys.stdout.buffer.write(b"P5\n240 240\n255\n" + b"".join(rows))
PY
convert disc.pgm -negate outside.png
convert shot.png outside.png -compose Multiply -composite -format "%[fx:int(mean*w*h)]" info:
# ^ ink the bezel eats. 0 is the goal.
```

Do not draw the disc with ImageMagick's `-fx`: a malformed expression yields a
uniform image, every measurement then reads 0, and it looks like a pass.

Three tiers, and the id is measured with `Font::getStringWidth` rather than
counted, because the font is proportional — `WWWWWWWWWWWW` is twelve characters
and 240px, while sixteen `1`s are 208:

| Width at SemiBold 20 | What is drawn |
| --- | --- |
| ≤ 186 | SemiBold 20, one line |
| > 186, and ≤ 187 at Regular 18 | Regular 18, one line |
| wider still | Regular 18, split in half over two lines |

Regular 18 is already in flash for the caption and the prompts, so stepping down
to it costs no font — which matters, because `594bedb` trimmed this app to the two
faces it actually draws. A third, smaller face would not help anyway: `W` is about
one em wide, so sixteen of them need an 11pt face to fit on one line, and that is
not readable.

Splitting rather than truncating, because half an id is no use to someone typing
it in, and the bars always carry the whole thing. Both halves always fit: the
widest half possible is eight `W`s at 144px, against the ~170px the lower of the
two rows allows. It is a second `TextArea`, not a newline — a newline inside a
wildcard is where TouchGFX *stops*, not a line break, which is the same reason
the prompt is four widgets.

The numbers all come from measurement, since neither font metrics nor the
circle's arithmetic predict the result well — a glyph's widest point is not on its
bottom row, so the analytic bound is too pessimistic:

| id | chars | SemiBold 20 | Regular 18 | ink | outside the circle |
| --- | --- | --- | --- | --- | --- |
| `0123456789ABC` | 13 | 172 | 147 | 170 | 0 |
| `0123456789ABCD` | 14 | 186 | 160 | 185 | 0 |
| `0123456789ABCDE` | 15 | 197 | 169 | 195 | 20 |
| `0123456789ABCDEF` | 16 | 208 | 178 | 203 | 64 |
| `GYMWORLD12345678` | 16 | 222 | 193 | — | 2 |
| `WWWWWWWWWWWW` | 12 | 240 | 216 | 203 | 29 |

186 is the widest string *measured* to sit wholly inside the circle, not an
interpolation between it and the 197 that fails. `GYMWORLD12345678` is the reason
the third tier exists: an entirely plausible id that overflows even 18pt by 6px.

### The panel, and the limit none of this can move

All of the above is about fitting text on the glass. Whether the *barcode* can be
read is a separate question with a harder answer, and it is settled by the
hardware rather than by the layout.

The panel is a Sharp **LS012B7DD06** — the `UNAview_LS012` board in
[UNAWatch/una-hardware](https://github.com/UNAWatch/una-hardware). Table 3-1 of
its technical literature (spec LD-29652B):

| | |
| --- | --- |
| Screen size | 1.19 inch |
| Active area | ⌀30.24 mm |
| Dot configuration | 240 (H) × 240 (V) |
| Dot pitch | **0.126 × 0.126 mm** |
| Display mode | Normally Black, reflective with slight transmission |
| Colours | 64 — "1 pixel has RGB each 2bit" |

That last row is where the four grey levels come from, and it is a fact about the
glass rather than about `LCD8bpp_ABGR2222`.

The dot pitch is what turns a module width in pixels into one a scanner cares
about. A Code 128 symbol for an id of *n* characters is `11n + 35` modules — the
35 is the standard's own figure for non-data overhead — drawn across 200 px, so
the X-dimension is `200 × 126 / (11n + 35)` microns:

| chars | modules | X-dimension | |
| --- | --- | --- | --- |
| 7 | 112 | 225 µm | 8.9 mil |
| 8 | 123 | 204 µm | 8.0 mil — a parkrun id is 7 or 8 characters |
| 12 | 167 | 150 µm | 5.9 mil |
| 14 | 189 | 133 µm | 5.2 mil — longest that clears 5 mil |
| 15 | 200 | 126 µm | 5.0 mil |
| 16 | 211 | 119 µm | 4.7 mil |

**There is no ISO minimum to compare these against.** ISO/IEC 15417's scope
specifies "…dimensions, decoding algorithms and *the parameters to be defined by
applications*", and X-dimension is one of those application parameters. An
earlier version of this README asserted a 0.19 mm ISO floor and concluded that
any id over eight characters was "outside the standard". That was wrong — the
figure is not in the symbology standard, and the conclusion drawn from it was
wrong too.

Against scanner capability instead, which is the question that matters:

- Code 128 is a mid-density symbology, and scanner-selection guidance puts it at
  5–10 mil capability. Everything up to **14 characters** clears 5 mil; 15 and 16
  fall just under, at 4.96 and 4.7 mil.
- Zebra's [LS2208 spec sheet](https://www.zebra.com/us/en/products/spec-sheets/scanners/general-purpose-scanners/ls2208.html)
  — a cheap, very widely deployed laser scanner — quotes a 3.0 mil narrow
  element. **Every length this app accepts is above that**, sixteen characters
  included.

So resolution is not the binding constraint anyone thought it was. What is
likely to decide a scan on this panel is contrast, specular glare off the front
polariser, and the anti-aliased bar edges — none of which these numbers measure,
and all of which are settled by pointing a scanner at the watch rather than by
arithmetic.

**Code 128 Subset C** remains the lever if a long numeric id ever does prove
marginal: the standard prices a numeric character at 5,5 modules against 11 for
a Subset B character, so a 16-digit id is 123 modules rather than 211 — the same
density as an eight-character id. The encoder is Subset B only, and the 107-row
table in `Code128.hpp` already carries `START_C` and `CODE_C`, so it is encoder
logic and not new data. Not implemented, and not urgent.

Being reflective rather than emissive cuts both ways for scanning: contrast in
daylight is paper-like, which is ideal, and in a dim room without the front light
there is nothing to read at all.

## Buttons

| Button | Position | Does |
| --- | --- | --- |
| `SW2` / L2 | bottom left | **next code** |
| `SW1` / L1 | top left | previous code |
| `SW4` / R2 | bottom right | **back — leaves the app** |

`L1` and `L2` wrap. Cycling is handled entirely in the GUI, which already holds
every code, so pressing `L2` never waits on the service.

**There are deliberately no on-screen button hints.** The bezel indicators are
23×35 ABGR2222 bitmaps and they blit corrupt on device — a smear of horizontal
dashes where the arc should be. That was found and worked around in `6b7de05`
("correct Barcode rendering on device") by leaving all four `NONE`, and
reintroduced in 0.3.0 by lighting `L1`/`L2` for cycling, which simply moved the
artifact from the bottom-right corner to the left edge. Fixed again in 0.3.2.

The pager marks carry the affordance instead: a row of small marks below the id,
one per code with the current one lit, says there are four codes without drawing
anything the blit path can corrupt. They are `touchgfx::Box` fills — a path that
renders correctly on device, unlike the indicator bitmaps. The unlit ones are
`170`, not something dimmer, because `LCD8bpp_ABGR2222` has two bits a channel:
the only greys are 85, 170 and 255, and 85 is the first thing to wash out in
daylight. A pager whose unlit marks vanish has lost the one thing it says.

The marks replaced a fraction appended to the caption in 0.3.4. `Gym 1/2` read as
a title with a number in it rather than as the first of two codes, so the caption
is now only ever the code's name, and position moved out of the label entirely.
Six marks at 8px on a 15px pitch span 83px of the ~130 the round panel still
allows at that height — and since `Commands.hpp` caps `kMaxCodes` at seven (98px),
the row cannot outgrow the screen. An unnamed code now has no caption at all: the
marks and the id beneath the bars say everything the fraction used to.

Getting real hints back means *drawing* the arcs rather than blitting them —
[`SleepLab`](../SleepLab) and [`Squash`](../Squash) both replaced this container
with one built from `touchgfx::Circle` and `PainterABGR2222` for exactly that
reason, and gained a five-colour palette in the process. Barcode still uses the
SDK template's bitmap version.

Nothing else is bound. There was a GUI-to-Service "set the id" message; it went
when the file arrived, because a GUI path that can overwrite a provisioned value
only gives the app a way to lose it. `SDK::AppConfig` would now happily let the
watch write an ID back to the file — and it still should not.

## Building

```sh
export UNA_SDK=/path/to/una-sdk
cd Barcode/Software/Apps/Barcode-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.3.6 .. && cmake --build build
```

Or the desktop simulator, which is where the provisioning flow is easiest to
exercise:

```sh
cd Barcode/Software/Apps/TouchGFX-GUI
UNA_SDK=/path/to/una-sdk make -f simulator/gcc/Makefile -j4
./build/bin/simulator.out
```

Dropping `input.json` into the simulator's filesystem root is the same thing as
writing it over USB. Take the root from the `Path to files created by app` line
the app logs at startup and do not guess at it: the mock filesystem's root is a
fixed number of `..` above the GUI directory, chosen for an app sitting inside
the SDK tree, so out of tree it lands somewhere unrelated to this app.

## Tests

Host tests live in [`Tests`](Tests), covering the Code 128 encoder and its spec
table, the geometry the barcode is drawn with, and the real `Service` driven by
a scripted message queue:

```sh
export UNA_SDK=/path/to/una-sdk
cd Barcode/Tests
cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)
```

Two things worth knowing before trusting a green run, both set out in
[the tests' own README](Tests/README.md). The barcode gets a quiet zone of ten
*pixels* where the symbology asks for ten *modules*, which is short at every id
length anyone would use; and ids of 15 or 16 characters fall just under the 5 mil
density that scanner-selection guidance suggests for Code 128, which is a
property of the dot pitch and not of the drawing. Both are recorded as
characterisation tests rather than fixed.

Nothing here has seen a panel. The geometry tests can say a rectangle's corners
are lit and what a module works out to in microns; they cannot say a scanner
reads it.

## Status

Built on the SDK's shipped configuration feature rather than on a private
convention. `input.json` is now declared as `configFile` in `app-manifest.json`,
the id is a declared `configFields` entry, and `SDK::AppConfig` does the reading
— so the format is the platform's, the phone can write it, and CI checks that
what the binary believes matches what the package declares.

The prediction in the old version of this section turned out to be right in the
one way that mattered: the app-side half was the part that outlived the change.
The reader was deleted; nothing about the barcode, the encoder, the two-message
contract or the screen had to move.

Two of the [concessions](#concessions) are worth raising upstream rather than
living with — the reader cannot report *why* a configuration was unusable, and a
field cannot declare that it has no safe default. Both are additive fixes to the
SDK, and both are why this app still carries a sentinel and a 17-byte field.
