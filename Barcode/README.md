# Barcode — Code 128 and QR barcodes for values you supply

Displays a parkrun-style **Code 128** barcode, plus the ID underneath it in
readable text. The IDs are not compiled in: you type them into the UNA phone app
when you install this one, and the watch reads them at launch.

Each code can be drawn as a **QR code** instead, or as **ITF** for a card already
printed that way — a per-code `Code128` / `QRCode` / `ITF` choice on the phone
form. That defaults to `Code128` and should stay there unless you know better: a
laser scanner — the kind at a parkrun finish funnel — cannot read a QR code at
all, while every scanner reads Code 128. An *imaging* scanner reads both, and
most modern handhelds are imagers. ITF is narrower still in scope: it is for a
card that is already ITF, and nothing else. See
[Which format](#which-format).

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
installs — a flat list of eighteen rows, an ID, a name and a format per code,
plus one more that applies to all of them:

> **Code 1 ID** — The value encoded in the barcode: a parkrun athlete ID, a
> membership number, a transit pass. Copy it exactly — case matters. This is the
> code the watch shows first.
> **Code 1 name** — Optional label so you can tell your codes apart when cycling
> through them. It is not part of the barcode, and a QRCode does not show it.
> **Code 1 format** — `Code128`, `QRCode` or `ITF`. See
> [Which format](#which-format).
> … and so on to Code 6.
> **Boost backlight** — On by default. Turns the backlight on for a short while
> whenever the watch has a code to show, so a scanner has light to read it by —
> see [Boosting the backlight](#boosting-the-backlight). One switch for all six
> codes, not one per code.

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
       "name2": "Sam",
       "id3": "GYMWORLD12345678",
       "name3": "Gym",
       "fmt3": "QRCode"
     }
   }
   ```

   `fmtN` is `Code128`, `QRCode` or `ITF`, in any case. Leave it out and the
   code is a Code 128 barcode, which is what every file written before this
   existed says.

   [`input.example.json`](input.example.json) is that file, ready to copy.
   Leave out any slot you do not want.
4. Eject the drive safely, unplug, and power-cycle the watch.

Until an id arrives by one route or the other, the app says so on screen and
draws no barcode. There is deliberately no usable id to fall back on: a barcode
is an identity claim, and a plausible placeholder that scans is worse than a
blank screen — at a parkrun finish it credits somebody else's run.

### Which format

Each code has a **format** row on the phone form, next to its ID and name. It is
pre-filled with `Code128`, which draws the ordinary barcode exactly as the app
always has. Change it to `QRCode` and that one code is drawn as a QR code
instead.

Case does not matter — `qrcode`, `QRCode` and `QRCODE` are the same word, and so
are `code128`/`Code128` and `itf`/`ITF`. There is no enum field type in the SDK,
so this is a plain text box and you are typing; being fussy about capitals would be a
spelling test rather than a safety check. `qr` on its own is *not* accepted:
two words for one format is a wart, and the watch tells you what to use.

**A file with no format row at all still means Code 128**, so an `input.json`
written before this existed keeps working and keeps meaning what it meant.
There are tests for that and for a row that has been emptied by hand.

Which to pick is a question about whatever is going to scan it, and the split is
not phone against not-phone — it is **laser against imager**:

| | laser scanner | imaging scanner | takes |
| --- | --- | --- | --- |
| Code 128 | reads it | reads it | 1–16 printable characters |
| QR | **cannot** | reads it | 1–16 printable characters |
| ITF | reads it | reads it | an **even** number of digits, 2–16 |

A laser sweeps a line, so there is no mechanism by which it reads a grid. An
imaging scanner takes a picture and decodes it, so it reads both — and that is
not exclusive to phones. Most modern handheld scanners are 2D imagers, as are
self-checkout and ticket-gate readers, and they handle QR perfectly well.

So Code 128 is the safe default because it works either way, and `QRCode` is for
a code you know is read by an imager. If you do not know, leave it on
`Code128`; you lose nothing but module size.

parkrun is the case this app was built for and it has one of each. The funnel
device is an Opticon OPN-2001, whose vendor material describes a laser engine —
that one cannot read QR. The Virtual Volunteer app scans with a phone camera,
and Apple Wallet parkrun passes are QR and are scanned from watches every week.
So if you know your event scans with the app, `QRCode` is a real option.

**`ITF` is not a choice about scanners — it is a choice about a card.** Code 128
and ITF are read by the same hardware, and for the same digits Code 128 draws
the *wider* bars of the two, so there is nothing to gain by switching a code
that already works. Pick ITF when the card in your pocket is printed as ITF and
whatever reads it expects ITF. It takes digits only, and an even number of them:
an odd count is **refused rather than padded**, because the usual leading zero
would be read back as part of your number. [Docs/ITF.md](Docs/ITF.md) has the
reasoning and what it costs.

What QR buys, where it works, is **module size**. A 16-character alphanumeric id
is Code 128's worst case here: 211 modules across 200 px, a 119 µm X-dimension.
The same id as a QR symbol is a **504 µm** module, four times as coarse, because
a matrix symbology spends area in two directions and this panel has area the
bars do not use. See [The panel](#the-panel-and-the-limit-none-of-this-can-move).

Two things a QR code costs, both deliberate:

- **It shows no name.** The symbol has to end above the id row and be a whole
  number of pixels per module, which puts it over the band the caption uses.
  Every size that would clear the caption is smaller, and module size is the
  number that decides whether a scanner can resolve it. The id text and the pager
  marks still say which code you are looking at.
- **The id limit does not change.** Still 1 to 16 characters. QR could hold a
  URL; six of those would not fit in the message the service sends the screen,
  and a URL has no readable rendering under the symbol on a 30 mm panel.
  [`Docs/QR.md`](Docs/QR.md) costs that out in full.

An unrecognised format — only reachable by hand-editing `input.json`, since the
phone's pattern accepts nothing else — refuses that code and says so on screen,
rather than quietly drawing it as Code 128.

### What the id may contain

1 to 16 printable ASCII characters. The phone enforces that as you type; a
hand-written file is checked on the watch. Either way an id that does not fit is
**refused, not trimmed** — a shortened id is a *wrong* id, and a barcode that
scans as someone else's number is the one genuinely harmful thing this app could
do. Keeping that true through the migration took a deliberate trick; see
[Concessions](#concessions).

**A leading or trailing space is refused too**, even though a space is itself
one of those 1-16 printable ASCII characters. It is invisible on the phone
form and easy to leave behind when an id is copied out of a spreadsheet cell
or a PDF, and every symbology here draws it as a real character rather than
ignoring it — so a trailing space is not a smaller mistake than a wrong digit,
it is a barcode that scans as a *different* id with nothing on screen to show
why. A space in the middle of an id is unaffected.

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

## How wide the bars come out

A Code 128 symbol is 11 modules whatever it holds, and the bars get a fixed
200 px on a panel with a 0.126 mm dot pitch. So the number of symbols is the
number that decides whether this app's barcode can be read at all, and until
0.3.6 it was one symbol per character with nothing to be done about it.

**Subset C** changes that: it packs a *pair of digits* into one symbol. The
encoder now switches into it wherever that shortens the barcode, and back out
when it stops paying. Nothing about this is visible from outside — the accepted
ids are exactly what they were, and a scanner decodes the same string either
way, because subset switching is part of Code 128 rather than an extension to
it. Only the bars get wider.

| Id | Before | Now | X-dimension | |
| --- | --- | --- | --- | --- |
| `A1` | 57 | 57 | 0.442 mm | nothing to pair |
| `A1234567` (parkrun) | 123 | **101** | 0.205 → **0.249 mm** | |
| `12345678` | 123 | **79** | 0.205 → **0.318 mm** | |
| `123456789012` | 167 | **101** | 0.151 → **0.249 mm** | |
| `1234567890123456` | 211 | **123** | 0.119 → **0.204 mm** | 16 digits now cost what 8 characters did |
| `WWWWWWWWWWWW` | 167 | 167 | 0.150 mm | no digits, no change |

There is [no ISO minimum](#is-it-scannable) to hold these to, so the line worth
naming is scanner capability: 5 mil (0.127 mm) is the low end of what
scanner-selection guidance gives for Code 128. Alphabetic ids of 15 and 16
characters fall just under it. **Every numeric length now clears it**, the worst
being fifteen digits at 0.188 mm.

A parkrun id — a letter and seven digits — is the shape this helps least, since
the `A` and the odd leading digit both stay in subset B and only the remaining
six pair up. It still goes 0.205 → 0.249 mm, about 8.0 to 9.8 mil.

Which subset to use is not configurable and should not be: it is arithmetic
about a particular id's digits, and there is no version of that question a
wearer or a config file has a useful opinion about.

### One surprise worth knowing

**Adding a digit to an odd-length numeric id makes the barcode wider bars, not
narrower.** Nine digits come to 101 modules and ten to 90; fifteen come to 134
and sixteen to 123.

An odd count strands a digit that cannot be paired, and carrying it costs a
symbol of its own plus a switch back into subset B — two symbols to say what a
pair says in one. No encoding avoids that; it is what pairing means.

In practice: a numeric id with a leading zero to spare renders wider bars with
it than without. And fifteen digits is the worst case in the entire numeric
range at 0.188 mm — still half again the 5 mil line, but the one numeric length
where sixteen digits would do better than fifteen.

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
│   ├── Header/Encoded.hpp        # the widths every symbology produces
│   ├── Header/Symbology.hpp      # format -> encoder, the one file naming both
│   ├── Header/Code128.hpp        # header-only subset B/C encoder, no SDK dependency
│   ├── Header/Itf.hpp            # header-only Interleaved 2 of 5, 3:1, even digits only
│   ├── Header/Commands.hpp       # the two-message contract between the halves
│   ├── Sources/AppConfigFields.cpp
│   └── Sources/Service.cpp       # reads the config, publishes the result
└── Apps/
    ├── Barcode-CMake/            # build glue
    └── CustomGUI/                # the GUI: bars, or a prompt saying what to do
        ├── Gui.cpp               # kernel message loop, the Service<->GUI contract,
        │                         # the "remember last code" file, per-format dispatch
        ├── barcode_gui.h         # the C ABI into the Rust core, checked at both
        │                         # compile time and by a runtime fingerprint
        └── rust/                 # no_std render core: embedded-graphics + u8g2-fonts
```

The Service owns the codes and the GUI only renders what it is sent, so the
configuration is read in exactly one place — which is also what `SDK::AppConfig`
requires, since it is one instance per app on one thread. Cycling is the one
thing the GUI does alone: it already has every code, so `L1`/`L2` never round-trip.

### The GUI is Rust, through CustomGUI, not TouchGFX

Ported from TouchGFX in place, following the architecture
[`RustGuiPoc`](../RustGuiPoc) and [`QrGuiPoc`](../QrGuiPoc) proved out first.
The SDK's **CustomGUI** entry point hands the GUI process a raw framebuffer
and a kernel message loop instead of a widget/scene-graph framework;
`Gui.cpp` owns that loop, and a `no_std` Rust crate under `CustomGUI/rust`
owns only pixels, reached over the C ABI in `barcode_gui.h`. Every encoder
this app draws — `Code128.hpp`, `Itf.hpp`, `Qr.hpp`, `Matrix.hpp` — is
**unmodified**: `Gui.cpp` calls them exactly as the old TouchGFX `Model`/
`BarcodeWidget` did, and only the widget tree that turned their output into
pixels was rewritten.

Measured against the TouchGFX GUI it replaced, from real `arm-none-eabi-size`
output on the same 600 KiB GUI RAM window (code runs from RAM on this SDK, so
framework size and the framebuffer share one budget):

| | Rust/CustomGUI | TouchGFX (removed) |
| --- | --- | --- |
| `.text` | 59,032 | 84,712 |
| `.bss` | 83,952 | 65,560 |
| RAM total | 143,184 — 23.3% | 152,648 — 24.8% |
| packaged `.uapp` | 87,964 bytes | 120,444 bytes |

These are smaller than an earlier version of this table (18.5%/73,472 bytes)
because that version's text had a real problem: u8g2-fonts' bitmap glyphs
have no anti-aliasing data to draw with, since the format is a fixed-resolution
1bpp bitmap, not a vector outline — so text came out visibly more jagged than
the Poppins TouchGFX drew, caught by putting real device captures
(`Resources/capture_*.png`) side by side with the new renderer's output
rather than judging it on the simulator alone. The fix (see `render_smoothed()`
in `lib.rs`) renders each string with a bitmap font one size class larger,
then shrinks it by area-averaging into the panel's four gray levels — a
genuine if approximate stand-in for anti-aliasing, costing a 15,360-byte
scratch buffer (`SS_MAX_W * SS_MAX_H`, sized from the actual widest string
measured across every screen, not guessed) and two extra font tables for the
larger source sizes. `.bss` is no longer "close either way" the way it was
before that fix — the scratch buffer is real, counted overhead this build pays
that TouchGFX's own pre-rendered, anti-aliased glyph bitmaps never needed to.
The RAM total is still smaller than TouchGFX's, by a much narrower margin than
first measured; the packaged `.uapp` stays meaningfully smaller, 27% down
rather than the original 41%. A true fix — rasterizing the original Poppins
TTF files (recoverable from git history) with real outline anti-aliasing via
a crate like `ab_glyph` — was investigated and shelved: it needs a heap
allocator, which on this Cortex-M target needs a hand-written
`critical-section` implementation (interrupt disable/restore) for an SDK
whose interrupt architecture isn't documented here and can't be verified
without real hardware. The supersample-and-shrink approach was chosen
specifically to avoid that risk.

Text is the one place `embedded-graphics`'s own `MonoTextStyle` could not
follow TouchGFX's proportional-width id layout — it only ships fixed-width
bitmap fonts. [`u8g2-fonts`](https://github.com/Finomnis/u8g2-fonts) closed
that gap: real proportional bitmap glyphs, `no_std`, with a
`get_rendered_dimensions()` measurement API that is the direct analogue of
TouchGFX's `Font::getStringWidth()` the old id-layout algorithm was built on.
The *algorithm* — prefer a bold face if it fits under an ink limit, else a
smaller regular face, else split by character count rather than by width — is
unchanged; only the two pixel thresholds are, because they were tuned to
TouchGFX's specific proprietary font metrics and had to be re-derived against
`u8g2-fonts`' bitmap faces. **The round-bezel ink-overflow measurement in
["Fitting the id on a round screen"](#fitting-the-id-on-a-round-screen) below
has not been redone against the new fonts** — it was verified against
TouchGFX's fonts only, and is an open item before trusting the new build the
way that section trusts the old one.

Prompt text changed shape along the way: the old `promptFor()` hardcoded up
to four pre-wrapped lines per `Problem`, by hand, except `BadFormat`'s, which
was already generated from `Symbology::kFormatNames` and word-wrapped against
measured pixel width — a past bug fix, keeping the on-screen format list from
drifting out of sync with what the app actually draws. `Gui.cpp` now builds
one flat, unwrapped message per `Problem` (`BadFormat`'s still built from
`kFormatNames`, so it keeps tracking that list automatically) and the Rust
core word-wraps every one of them the same way, at render time, against the
font it actually has. One wrapper for every screen instead of one bespoke
path and eight hand-split arrays.

Code128 and ITF disagree on purpose about how element widths become pixels —
`Symbology.hpp`'s `renderStyle()` documents why — and the port kept that
disagreement rather than flattening it. ITF still rounds to one whole-pixel
unit for the entire symbol, same as before. Code128 does not: its modules are
already close to one pixel at the lengths this app draws, so flooring each
one independently would nearly halve the narrow ones. It instead rounds each
cumulative bar/space *boundary* to the nearest pixel from an accumulated
sub-pixel position, which keeps every module close to its true fractional
width without needing true alpha-blended anti-aliasing (which TouchGFX's
`CanvasWidget` had and `embedded-graphics`'s fill primitives do not) on a
panel with only four levels a channel to blend into anyway.

### Changing how many codes it holds

`Barcode::kMaxCodes` is the single number. The service loop, the message, the
screen and the cycling are all written against it, so **no logic changes** — but
the *declaration* is per-field by construction, so raising it means adding an
`idN`/`nameN`/`fmtN` triple in two places: `configFields` in `app-manifest.json`,
and `kFields` in `AppConfigFields.cpp`. (The SDK's `--check-bounds` reads that
table as text and rejects named constants, so it cannot be generated from a
loop.)

Two `static_assert`s make a mismatch a build error rather than a silent bug:

- `AppConfigFields.cpp` fails if the table is not `kMaxCodes * kFieldsPerCode + 1`
  entries — three per code, plus the one `boostBacklight` setting every code
  shares
- `Commands.hpp` fails if a full state no longer fits a 256-byte message block

**Seven is the ceiling**, and it is that second assert that sets it — not the
SDK's 32-field limit, which (with `boostBacklight` taking one of the 32) would
allow ten. A `Code` is 30 bytes and a `State` is 182 at six; eight would
overrun the pool block.

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

**This section, including the measurement table below, describes the
TouchGFX build this app shipped before the Rust/CustomGUI port** (see
["The GUI is Rust, through CustomGUI, not TouchGFX"](#the-gui-is-rust-through-customgui-not-touchgfx)
above). The *reasoning* — a proportional font's width has to be measured
rather than counted, the simulator cannot show bezel clipping, three tiers
of large/small/split — still applies, and the new build follows the same
shape with `u8g2-fonts`' own measurement API standing in for TouchGFX's
`Font::getStringWidth()`. The specific pixel numbers in the table are
TouchGFX's font metrics, not `u8g2-fonts`'; they have not been re-measured
against the new build with the disc-mask methodology below, which is an open
item before trusting the Rust build's bezel fit the way this section trusts
the old one.

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

**A QR code sidesteps the whole table.** Its module is four *whole pixels*,
504 µm, whatever the id says — because a matrix symbology spends area in two
directions rather than width in one, and the version is fixed at 2, which holds
26 characters against the 16 an id may be. That is four times the X-dimension of
the worst row above. The cost is that the symbol has to be square: the largest
square of lit pixels on this panel is 168 px, and only 144 px is available once
the id row below it is left alone, which is what fixes the version at 2 and the
module at 4 px. A 5 px module misses fitting by a single pixel row.
[`Docs/QR.md`](Docs/QR.md) has that arithmetic and what it rules out.

A camera does read it off this glass: Google Lens decodes the symbol clearly
from the watch. That is a real scan rather than arithmetic, and it is the claim
this section could not make until 0.4.0 was on a wrist — see
[the tests' README](Tests/README.md) for what it does and does not cover.

The table above is **alphabetic** ids, which is the pessimistic case. **Code 128
Subset C** is implemented, and it prices a numeric character at 5,5 modules
against 11 for a Subset B one — so a 16-digit id is 123 modules rather than 211,
the same density as an eight-character one, and every numeric length clears
5 mil. See [How wide the bars come out](#how-wide-the-bars-come-out); the
counter-intuitive part is that an *odd* number of digits does worse than one
more digit.

Being reflective rather than emissive cuts both ways for scanning: contrast in
daylight is paper-like, which is ideal, and in a dim room without the front light
there is nothing to read at all.

## Boosting the backlight

That last sentence is the reason **Boost backlight** exists, and it is on by
default. Whenever the watch has a code to show — at launch, and every time you
reopen the app — the app asks the kernel for the backlight at full brightness
for 30 seconds, through the same `SDK::Message::RequestBacklightSet` request
[`Squash`](../Squash) uses for its own on-screen cues. That covers raising your
wrist, being read, and cycling to a second code if you carry more than one; it
does not relight itself on a bare button press, so cycling past the 30 second
mark with the app still open goes back to however the watch would otherwise
have lit the screen.

It is one setting for all six codes, not one per code — there is only one
panel, and whether it needs help being read does not depend on which code is
on it. Turn it off if you would rather the watch behaved exactly as it does
without this app: `boostBacklight` is a plain `bool` field, so unlike the id
and format fields there is nothing to mistype.

## Buttons

| Button | Position | Does |
| --- | --- | --- |
| `SW2` / L2 | bottom left | **next code** |
| `SW1` / L1 | top left | previous code |
| `SW4` / R2 | bottom right | **back — leaves the app** |

`L1` and `L2` wrap. Cycling is handled entirely in the GUI, which already holds
every code, so pressing `L2` never waits on the service.

The GUI also remembers which code was on screen, in a small file of its own
(`last_code.txt`, alongside `input.json` in the app's folder — the phone never
reads or writes it) — so reopening the app goes straight back to it instead of
back to Code 1. A family member who is always Code 3 does not re-cycle past 1
and 2 every time. Written on every cycle and read back once, at the first
snapshot of real codes the GUI sees after launch; a resume that happens while
the app is already open leaves mid-session cycling alone rather than
re-reading it.

**There are deliberately no on-screen button hints.** The bezel indicators are
23×35 ABGR2222 bitmaps and they blit corrupt on device — a smear of horizontal
dashes where the arc should be. That was found and worked around in `6b7de05`
("correct Barcode rendering on device") by leaving all four `NONE`, and
reintroduced in 0.3.0 by lighting `L1`/`L2` for cycling, which simply moved the
artifact from the bottom-right corner to the left edge. Fixed again in 0.3.2.

The pager marks carry the affordance instead: a row of small marks below the id,
one per code with the current one lit, says there are four codes without drawing
anything a blit path can corrupt. They were `touchgfx::Box` fills before the
Rust port and are plain filled rectangles now — in both cases, solid colour
straight into the framebuffer, the path that renders correctly on device
unlike the indicator bitmaps. The unlit ones are `170`, not something dimmer,
because `LCD8bpp_ABGR2222` has two bits a channel: the only greys are 85, 170
and 255, and 85 is the first thing to wash out in daylight. A pager whose
unlit marks vanish has lost the one thing it says.

The marks replaced a fraction appended to the caption in 0.3.4. `Gym 1/2` read as
a title with a number in it rather than as the first of two codes, so the caption
is now only ever the code's name, and position moved out of the label entirely.
Six marks at 8px on a 15px pitch span 83px of the ~130 the round panel still
allows at that height — and since `Commands.hpp` caps `kMaxCodes` at seven (98px),
the row cannot outgrow the screen. An unnamed code now has no caption at all: the
marks and the id beneath the bars say everything the fraction used to.

Getting real hints back means *drawing* the arcs rather than blitting them —
[`SleepLab`](../SleepLab) and [`Squash`](../Squash), both still on TouchGFX,
replaced their bitmap container with one built from `touchgfx::Circle` and
`PainterABGR2222` for exactly that reason. The Rust/CustomGUI renderer this
app now draws through has no bitmap-blit path to corrupt in the first place —
`embedded-graphics`' own circle/arc primitives fill straight into the
framebuffer the same way the pager marks do — so the constraint that kept
Barcode on the SDK template's bitmap version no longer applies here. Nothing
draws button hints yet; this just means the door TouchGFX kept shut for this
app is open now, not that it has been walked through.

Nothing else is bound. There was a GUI-to-Service "set the id" message; it went
when the file arrived, because a GUI path that can overwrite a provisioned value
only gives the app a way to lose it. `SDK::AppConfig` would now happily let the
watch write an ID back to the file — and it still should not.

## Building

Needs a Rust toolchain with the `thumbv8m.main-none-eabihf` target alongside
the usual ARM/CMake/Python setup — same requirement as
[`RustGuiPoc`](../RustGuiPoc)/[`QrGuiPoc`](../QrGuiPoc), and already present in
this repo's own `toolchain/Dockerfile`.

```sh
rustup target add thumbv8m.main-none-eabihf
export UNA_SDK=/path/to/una-sdk
cd Barcode/Software/Apps/Barcode-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.5.0 . && cmake --build build
```

`cmake --build` always invokes `cargo build --release` for
`CustomGUI/rust` first and links the resulting `libbarcode_gui.a` into the
GUI ELF (`Gui::run()` checks a runtime ABI fingerprint against it at startup,
so a stale archive fails loudly rather than misreading the C struct it was
handed). The `.uapp` lands in `Output/` as before.

There is no TouchGFX simulator any more. The nearest equivalent is the Rust
core's own desktop simulator, which calls the identical `render()` the
firmware calls against hand-built `Frame`s rather than driving the app end to
end through a mocked filesystem and kernel:

```sh
cd Barcode/Software/Apps/CustomGUI/rust
cargo run --bin sim --features sim              # interactive, TAB cycles scenes
cargo run --bin sim --features sim -- --dump     # every scene to /tmp/barcode_gui_preview/*.png
```

Exercising the actual provisioning flow — `input.json` arriving, `SDK::AppConfig`
reading it, the Service publishing a real `Barcode::State` — needs either real
hardware or a Service-side host harness; nothing here mocks the kernel message
bus the way the old TouchGFX simulator did.

## Tests

Host tests live in [`Tests`](Tests), covering the Code 128 encoder and its spec
table, the QR encoder and the zint oracle underneath it, the geometry the
barcodes are drawn with, and the real `Service` driven by a scripted message
queue:

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

The QR side goes two steps further. A capture of the simulator's framebuffer
decodes to the right id with zbar, an independent decoder, at the real 4 px
module — so the pixels the renderer puts down are a correct, readable symbol end
to end. And the symbol has been read off the watch itself: **Google Lens decodes
it clearly** from the panel, which is the part no capture and no arithmetic could
settle. One reader in one set of conditions, not a survey of scanning angles and
lighting, but the question of whether this panel can carry a QR code is closed.

The bars have had no equivalent test. Nothing here says a laser scanner reads a
Code 128 symbol off this glass.

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
