# Barcode — a Code 128 barcode for a value you supply

Displays a parkrun-style **Code 128** barcode, plus the id underneath it in
readable text. The id is not compiled in: you write it into a small JSON file in
the app's own folder over USB, and the app reads it at launch.

That second half is the point. The watch has four buttons and no keyboard, so a
value only you know — an athlete id here, but the shape is the same for a
transit pass, a membership number or an account token — has to come from
outside, and the SDK has no supported way to get one in. This app is a working
answer to [UNAWatch/una-sdk#225](https://github.com/UNAWatch/una-sdk/issues/225),
built out of what exists today rather than out of what a companion protocol
might one day offer.

## Setting your id

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

Until you do, the app says so on screen and draws no barcode. There is
deliberately no built-in id to fall back on: a barcode is an identity claim, and
a plausible placeholder that scans is worse than a blank screen — at a parkrun
finish it credits somebody else's run.

If the file is there but unusable, the screen names which of these it is —
not valid JSON, wrong `schema`, no `values.id`, or a value the encoder will not
take. On a device with no keyboard the failure has to be legible from the watch
itself, or it is not debuggable at all.

### What the id may contain

1 to 16 printable ASCII characters. Anything else is refused rather than
trimmed, because a shortened id is a *wrong* id, and a barcode that scans as
someone else's number is the one genuinely harmful thing this app could do.

### Changing it later

Overwrite `input.json` and relaunch the app. It re-reads the file whenever the
GUI resumes, but only when the file's size or timestamp has actually changed, so
the common case costs one `stat`. Note that USB mass storage detaches the volume
from the watch while your computer holds it, so in practice the reboot after
ejecting is what picks the new value up.

## Why a separate file, and not `settings.json`

Every UNA activity app keeps its own `settings.json` in this same folder, so
that would be the obvious place to put an id. It is the wrong one:

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

`input.json` is not invented. Its shape copies **`SDK::Variant::Config`**
(`Libs/Header/SDK/Variant/VariantConfig.hpp`), which is the SDK's existing
bounded-JSON-config reader — the one that gives a code-less variant alias its
name and FIT identity. From it this app takes:

| Rule | Why it is worth copying |
| --- | --- |
| A `schema` major that must match **exactly** | An unknown major falls back entirely instead of guessing at rearranged keys |
| An app-owned subtree the reader treats as opaque (`features` there, `values` here) | The vocabulary belongs to the app; a shared reader never needs to know these key names |
| A size ceiling checked **before** anything is allocated | 8 KB there, 4 KB here |
| Every failure falls back to a default | A config somebody else wrote must never stop the app starting |

The only real difference is where the bytes come from: `SDK::Variant::Config`
reads an alias `.uapp` built by [`make_variant.py`](https://github.com/UNAWatch/una-sdk/blob/main/Utilities/Scripts/app_merging/make_variant.py),
this reads a plain file a person can type into Notepad. Which is the whole
argument for issue #225 in one sentence — the platform already has the reader,
the schema discipline and the vocabulary; what it lacks is a config source the
user can write.

The size ceiling is not decoration. All five SDK settings serializers do
`new char[file->size()]` with no upper bound, so any oversized file dropped into
an app folder takes a bite out of a 256 KB app budget. `InputConfig` refuses
before it allocates.

## Layout

```
Software/
├── Libs/
│   ├── Header/Barcode.hpp        # id + why-there-is-no-id, shared Service <-> GUI
│   ├── Header/Code128.hpp        # header-only Subset B encoder, no SDK dependency
│   ├── Header/Commands.hpp       # the two-message contract between the halves
│   ├── Header/InputConfig.hpp    # the bounded reader
│   ├── Sources/InputConfig.cpp
│   └── Sources/Service.cpp       # reads the file, publishes the result
└── Apps/
    ├── Barcode-CMake/            # build glue
    └── TouchGFX-GUI/             # the GUI: bars, or a prompt saying what to write
```

The Service owns the value and the GUI only renders what it is sent, so the file
is read in exactly one place.

## Buttons

| Button | Position | Does |
| --- | --- | --- |
| `SW4` / R2 | bottom right | **back — leaves the app** |

Nothing else is bound. There was a GUI-to-Service "set the id" message; it went
when the file arrived, because a GUI path that can overwrite a provisioned value
only gives the app a way to lose it.

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

A proof of concept. The file format is one contributor's proposal, not a UNA
convention, and issue #225 is unanswered — so treat `input.json` as something
that may have to be renamed if the platform ships its own answer. The app-side
half is the part meant to outlive that: read a bounded, versioned, validated
blob from a known path, and re-check it cheaply. Whatever eventually writes the
file — a desktop page, a phone, a kernel handler behind the BLE custom-command
service — the app does not have to change.
