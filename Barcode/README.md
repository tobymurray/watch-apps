# Barcode — a Code 128 barcode for a value you supply

Displays a parkrun-style **Code 128** barcode, plus the id underneath it in
readable text. The id is not compiled in: you type it into the UNA phone app
when you install this one, and the watch reads it at launch.

That second half is the point. The watch has four buttons and no keyboard, so a
value only you know — an athlete id here, but the shape is the same for a
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

## Setting your id

### From the phone

The app declares one configuration field, so the UNA app asks for it while it
installs:

> **Your id** — The number under your barcode, copied exactly. Case matters.
> Leave it blank until you have it in front of you — a guessed id scans as
> somebody else.

It is a required field, so the install will not finish until it is filled in.
Editing it later is the same screen, and the new value applies the next time you
open the app.

### Or over USB

The values file is a plain file in the app's own folder, so it can still be
written by hand:

1. Connect the watch by USB and wait for the drive to mount.
2. Open `Apps/Barcode/` on it — the same folder the `.uapp` lives in.
3. Create `input.json`:

   ```json
   {
     "schema": 1,
     "values": {
       "id": "A1234567"
     }
   }
   ```

   [`input.example.json`](input.example.json) is that file, ready to copy.
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

**There is no "unset".** Every field in `app-manifest.json` must declare a
`default`, and it must satisfy the field's own constraints, so an app cannot say
"there is no safe value for this". The default here is a single space, which the
app treats as the absence of an id — a sentinel that is checked by an explicit
comparison, because a space is a perfectly legal Code 128 character. It also
means `required: true` is weaker than it looks: the SDK counts accepting the
pre-filled value as satisfying a required field, so the sentinel can reach the
file, and the app has to recognise it rather than trust that a value is a value.

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
│   ├── Header/AppConfigFields.hpp    # the app's copy of that field, CI-checked
│   ├── Header/Barcode.hpp        # id + why-there-is-no-id, shared Service <-> GUI
│   ├── Header/Code128.hpp        # header-only Subset B encoder, no SDK dependency
│   ├── Header/Commands.hpp       # the two-message contract between the halves
│   ├── Sources/AppConfigFields.cpp
│   └── Sources/Service.cpp       # reads the config, publishes the result
└── Apps/
    ├── Barcode-CMake/            # build glue
    └── TouchGFX-GUI/             # the GUI: bars, or a prompt saying what to do
```

The Service owns the value and the GUI only renders what it is sent, so the
configuration is read in exactly one place — which is also what `SDK::AppConfig`
requires, since it is one instance per app on one thread.

`app-manifest.json` never reaches the watch, so the binary carries its own copy
of what it declared. The two must agree, and CI is what makes sure they do:

```sh
python3 $UNA_SDK/Utilities/Scripts/app_packer/validate_app_config.py \
    --check Barcode/app-manifest.json \
    --check-bounds Barcode/Software/Libs/Sources/AppConfigFields.cpp
```

## Buttons

| Button | Position | Does |
| --- | --- | --- |
| `SW4` / R2 | bottom right | **back — leaves the app** |

Nothing else is bound. There was a GUI-to-Service "set the id" message; it went
when the file arrived, because a GUI path that can overwrite a provisioned value
only gives the app a way to lose it. `SDK::AppConfig` would now happily let the
watch write the id back to the file — and it still should not.

## Building

```sh
export UNA_SDK=/path/to/una-sdk
cd Barcode/Software/Apps/Barcode-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=1.0.0 .. && cmake --build build
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
