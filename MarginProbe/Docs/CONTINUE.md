# Prompt: finish the glance margin measurement, then spend it

You are working in `watch-apps`, in `MarginProbe` — a throwaway measuring
instrument, and the `SunGlance` change it exists to justify. Five rounds of
probing on real hardware have already happened. **Most of the answer is known.
Your job is one confirmation, one open question, and then spending the result.**

**Do not start by re-measuring.** §1 lists what is settled and how; re-deriving
any of it is wasted hardware time. §2 is what is actually open.

## 0. Read first

- [`../README.md`](../README.md) — the instrument. Rounds 1–5, each with what it
  found and why the next round was needed. The tables are measurements, not
  estimates.
- `../Software/Libs/Header/Service.hpp` — the file header carries the reasoning
  for every probe style, including the two that failed and why.
- `../../SunGlance/Software/Libs/Header/Render.hpp` — `kSafeLeftInset`,
  `kSafeRight`, `kBottomGuard`. These three constants are what this whole
  exercise exists to replace. All three are admitted guesses in their own
  comments.
- `../../SunGlance/Software/Libs/Sources/Render.cpp` — `layoutFor()` and
  `shapeFits()`, where those constants are actually spent.

## 1. Settled — do not re-measure

**The glance area is 240×60** and the kernel grants **32 controls**, not the 5
`SunGlance` treats as a luxury. Its words-instead-of-icons fallback for a
control-starved kernel is dead code on this watch.

**The drawable region is the band's inscribed circle.** A pixel lights when the
circle clips it *at all*, not when it swallows its centre:

```
xL(y) = ceil(w/2 - sqrt((w/2)^2 - dy^2) - 1.0),   dy = y + 0.5 - h/2
```

which on 240×60 gives, per row band:

| rows | 0–2 | 3–7 | 8–14 | 15–44 | 45–51 | 52–56 | 57–59 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `xL` | 3 | 2 | 1 | **0** | 1 | 2 | 3 |

Mirrored on the right, measured on both edges three separate times, agreeing
within a pixel every time. **For a 30px text row centred in the band the glass
costs 0 pixels.** `kSafeLeftInset = 18` is therefore not the glass.

The `-1.0` is a correction the fifth round earned: it shipped as `-0.5` (centre
inside the circle) and the panel refuted it — the mid-grey ring drawn one pixel
outside, predicted to vanish, was visible along *part* of the arc. Only part is
the tell: the two rules differ on 22 of the 60 rows, so a uniform off-by-one
would have looked nothing like it. **That refutation was spotted by eye on the
real panel, not in a photograph** — see §5.

**`GLANCE_FONT_POPPINS_MEDIUM_10` has no letter glyphs on this kernel.** Digits
and `-` render; every letter comes back as `?`. Verified twice. Anything that
must read as words needs 18px or larger.

## 2. Open

**a. Proof 1.1.0 is built but not installed.** `Output/Proof_1.1.0.uapp` exists;
the watch still has `1.0.0`, the refuted centre-rule build. Installing and
photographing it is the immediate next action (§3). Its prediction: dark grey
field, unbroken 1px white ring, **no mid grey anywhere**. If mid grey still
shows, the boundary is further out again and the cause is an effective radius
larger than `w/2` rather than a rounding convention — a different fix, which the
same probe distinguishes.

**b. The scroll arc has never been caught overlapping the band.** Round 4 painted
the band mid grey specifically to catch it and found it sitting *above and below*
the band, horizontally within its x-range but vertically clear. So its cost is
unmeasured — not zero, just never observed. The arc's position tracks scroll
position, so it may only overlap when the probe is at the top or bottom of the
glance list, or transiently mid-scroll. Try those.

**c. `kSafeLeftInset = 18` remains unexplained.** The glass accounts for 0–3px
and the arc for an unknown amount. `SunGlance` had an icon at x=11 clipped on
real hardware, which is the only hard evidence the left edge costs anything at
all. Until (b) is resolved, do not assume 18 is pure waste — but do not assume it
is justified either.

## 3. Immediate next action

There is no `cmake` and no ARM toolchain on the Mac; everything is built in
Docker, by **image ID** rather than tag (the tags are local-only, so `docker run`
by name tries to pull and fails).

```sh
# every probe in this directory was built with this image.
# the pip install is required: this image lacks pyelftools, and without it the
# link succeeds and the packer then dies with ModuleNotFoundError: 'elftools'
docker run --rm -v ~/git/watch-apps:/apps -v ~/git/una-sdk:/sdk -e UNA_SDK=/sdk \
  -w /apps/MarginProbe/Software/Apps/MarginProbe-CMake 4d75a70b33b4 bash -lc '
    python3 -m pip install --break-system-packages -q \
        -r /sdk/Utilities/Scripts/app_packer/requirements.txt
    cmake -B build-proof -G "Unix Makefiles" -DPROBE_STYLE=proof -DPROBE_INSET=4 \
        -DBUILD_VERSION=1.1.0 . && cmake --build build-proof -j"$(nproc)"'

# install (watch mounts as a USB-MSC volume; folder name is APP_USER_NAME)
rm -f "/Volumes/UNA WATCH/Apps/Proof/Proof_1.0.0.uapp"
cp MarginProbe/Output/Proof_1.1.0.uapp "/Volumes/UNA WATCH/Apps/Proof/"
sync && diskutil eject "/Volumes/UNA WATCH"     # eject properly; an unclean pull
                                                 # left one install unverified

# then power-cycle, photograph the Proof card, and pull the photo
adb shell 'ls -t /sdcard/DCIM/Camera/ | head -1'
adb pull "/sdcard/DCIM/Camera/<name>.jpg" .
```

The watch registers any `.uapp` dropped in `Apps/<Name>/` — it scans on boot and
reads the app's name out of the binary, so `app_list.json` needs no editing.

`4d75a70b33b4` is `ghcr.io/tobymurray/kira-toolchain`, which was what
`app-build.yml` pinned when these probes were built. **CI has since moved** to
`ghcr.io/tobymurray/watch-apps-toolchain@sha256:cca44e2c…`, which is not the same
image and may not be local. That does not affect `MarginProbe` — it is
deliberately not in CI (§6) — but do not describe `4d75a70b33b4` as matching CI,
and pull the current pin if you want a build that does.

## 4. The stranded `SunGlance` branch, and why it needs rework

Branch **`sunglance-release`** carries four commits based on `0c01566`, which is
now well behind `main`:

```
feat(sunglance): declare the position as a phone-filled config contract
docs(sunglance): the position is declared in the manifest, not a TOML sketch
ci(sunglance): build it on every push, like Barcode
chore(sunglance): version 0.4.0
```

It gives `SunGlance` an `app-manifest.json` (so the phone collects `lat`/`lon`
into `input.json`, which `HomeConfig` already reads unchanged) and a caller
workflow. **It was correct when written and is now wrong in three specific
ways**, because `main`'s CI changed under it:

1. `.github/workflows/sunglance.yml` declares `permissions: contents: read`. It
   must be `contents: write` — `app-build.yml` now commits, tags and pushes the
   version bump itself, and a caller's grant caps what the called workflow can
   use.
2. The `chore(sunglance): version 0.4.0` commit is now redundant and should be
   dropped. CI derives the bump from commit types since the app's last
   `sunglance-*` tag; the `feat:` commit alone would take 0.3.1 → 0.4.0 on its
   own.
3. Consequently `appVersion` in the manifest should read **`0.3.1`** (what was
   last built), not `0.4.0`. CI bumps *from* it.

Rebase it onto `main` and fix those three before doing anything else with it. The
manifest itself, and the README rewrite, need no changes — verified against the
SDK's `validate_app_config.py`, which passes.

## 5. Method — what failed, and why. Do not repeat any of it.

Four rounds were spent learning how *not* to measure this. In order:

- **Measuring photo pixels does not work.** Two independent calibrations of the
  same photograph disagreed by a factor of two. The glass is curved, the shot is
  hand-held, and there is no scale in frame you can trust.
- **A fixed camera distance would not have fixed it.** Each card is
  self-calibrating if it carries a feature of known size; what actually cost
  accuracy was off-axis angle, refraction at the very edge being where the
  measurement lives, and uneven lighting making one side read consistently
  shorter than the other.
- **Colour is not a usable encoder.** The panel is a backlit LCD at two bits a
  channel and the backlight is blue. Red survives only as a magenta tinge, green
  lands on cyan and cannot be told from white, and bright hues bloom over dark
  single-channel ones. Six hues chosen to be distinct in RGB222 collapsed into
  one channel on the glass.
- **Luminance is.** Three neutrals shift *together* under that bias, so the bias
  cancels. Every reliable reading in this exercise came from levels, not hues.
- **At macro magnification the LCD's own pixel grid aliases.** Edge detection
  starts reading subpixel structure instead of levels. This is why the fifth
  round's refutation came from a person looking at the panel and not from any
  amount of scanning the photograph.

What *did* work, every time: making the panel state a discrete fact — count the
bars, name the colour, is the ring broken — instead of asking a photograph for a
measurement.

## 6. The payoff

Once (a) and (b) are closed, `SunGlance`'s `Render.hpp` should stop spending a
flat 18 columns on the left:

- Replace `kSafeLeftInset` with a function of `y`, per §1's staircase, plus a
  separate and separately-justified arc allowance if (b) finds one.
- `kSafeRight = 8` and `kBottomGuard = 2` are both guesses too, and §1 covers
  them: the right edge mirrors the left, and the bottom costs the same as the
  top.
- Then re-run `layoutFor()`'s numbers. Recovering columns raises the font the
  side-by-side arrangement can afford — `timeWidthFor()` is 2.8 em per time and
  the pair plus a gap has to fit `usableW`, so every column returned is real.
  Font 30 needs `iconW <= 13`; today's 24px icon forces font 25.
- `MarginProbe` is deletable the moment those constants are numbers. It is a
  measuring instrument with a shelf life, has no `app-manifest.json`, and is
  deliberately not wired into CI.

## 7. Non-negotiables

- **Do not re-measure §1.** It is hardware time already spent. Argue with it
  explicitly if you disagree, and say why.
- **Never install without ejecting.** One install in this exercise went
  unverified because the volume was pulled without ejecting.
- **Do not touch anything on the watch volume except `Apps/<Probe>/`.** No
  `app_list.json`, no `settings.json`, no other app's folder.
- **State predictions before photographing.** Every probe after the second was
  built to be refutable, and the fifth one *was* refuted. That is the format
  working, not failing.
