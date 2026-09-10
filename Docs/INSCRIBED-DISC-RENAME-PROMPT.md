# Prompt: rename `PanelKit` to `inscribed-disc`, and set the scope for publication

You are working in `watch-apps`, a set of UNA Watch apps. One directory,
`PanelKit/`, holds a `no_std` Rust crate that two app renderers link against.
It is about to be published to crates.io, where a name is claimed permanently.

**The decisions below are made. Do not re-litigate them.** They come out of an
investigation recorded in
[`PanelKit/Docs/2026-09-10-publish-or-not.md`](../PanelKit/Docs/2026-09-10-publish-or-not.md),
with a re-runnable harness in `PanelKit/Docs/measurements/disc-wrapper/`. Read
both before you start, along with `PanelKit/Docs/DESIGN.md` and the repository's
`CLAUDE.md`.

Your job is to execute the rename, make three scope changes, and leave no stale
reference anywhere in the repository.

---

## 0. The standard this repository holds

Read `CLAUDE.md` and obey it, particularly the comment rule: **write no comment
this file cannot keep true.** Never counts of tests or files, never what another
class does, never planned work. The carve-outs are hardware behaviour proven on
the watch, the frozen wire format, and a measurement with its numbers and its
falsifier.

Specific ways to fail this task:

- **Renaming by `sed` alone and declaring victory.** The name appears as a Rust
  crate name, a Rust module path, a C++ namespace, a C++ header guard, a C++
  macro, an `extern "C"` symbol, a CMake module filename, CMake function names,
  CMake variable prefixes, a CI workflow filename, a CI workflow display name, a
  CI path filter, a shell variable inside a CI step, a directory name, and prose
  in five READMEs. A textual replace will miss the ones that are only
  *conceptually* the same name.
- **Leaving the extern symbol dangling.** `DESIGN.md` records that when this
  symbol last moved, "nothing but a link catches that: the crate compiled fine
  with the symbol dangling." `llvm-nm --undefined-only` on the archive is the
  check. Do it.
- **Asserting a footprint or frame time you did not measure.** Every number you
  write must come from a command you ran, with the command recorded.
- **Adding a module, widget or abstraction nobody asked for.** The scope changes
  are exactly the three in §2. Nothing else grows.

---

## 1. The rename

`panelkit` → `inscribed-disc`. The Rust identifier form is `inscribed_disc`; the
crates.io and Cargo form is `inscribed-disc`; the directory form is
`InscribedDisc`.

**Why this name**, so you can judge borderline cases: the crate's central
assumption is that the glass is the *inscribed disc* of a square framebuffer.
`DESIGN.md`'s own falsifier for the geometry is "a display whose glass is not the
inscribed disc". Putting that assumption in the name means a reader whose glass
is shaped otherwise learns it is not for them before installing. The name
deliberately does **not** promise round drawing primitives, and does not claim
kinship with the `embedded-graphics` project.

### The map

| site | from | to |
|---|---|---|
| directory | `PanelKit/` | `InscribedDisc/` |
| Cargo `[package] name` | `panelkit` | `inscribed-disc` |
| Rust paths in dependent crates | `panelkit::…` | `inscribed_disc::…` |
| C++ namespace | `namespace panelkit` | `namespace inscribed_disc` |
| C++ header guard | `PANELKIT_GUISHELL_HPP` | `INSCRIBED_DISC_GUISHELL_HPP` |
| C++ macro | `PANELKIT_DEFINE_HOST_PANIC` | `INSCRIBED_DISC_DEFINE_HOST_PANIC` |
| `extern "C"` symbol | `panelkit_host_panic` | `inscribed_disc_host_panic` |
| CMake module | `panelkit.cmake` | `inscribed-disc.cmake` |
| CMake functions | `panelkit_rust_gui`, `panelkit_gui_depends_on_rust` | `inscribed_disc_rust_gui`, `inscribed_disc_gui_depends_on_rust` |
| CMake variables | `PANELKIT_PATH`, `PANELKIT_INCLUDE_DIRS`, `PANELKIT_RUST_TARGET`, `PANELKIT_RUST_LIB`, `PANELKIT_RUST_TARGET_NAME`, `PANELKIT_CARGO_FEATURES` | the `INSCRIBED_DISC_` equivalents |
| CI workflow file | `.github/workflows/panelkit.yml` | `.github/workflows/inscribed-disc.yml` |
| CI workflow `name:` | `PanelKit` | `InscribedDisc` |
| CI path filters | `PanelKit/**`, `.github/workflows/panelkit.yml` | the renamed paths |
| `app-build.yml` step | the `panelkit=` shell variable, the `grep -q 'PanelKit'` probe, the `steps.app.outputs.panelkit` condition, the `apps/PanelKit` working directory, the step's display name | the renamed equivalents |

### One API rename that follows from the crate name

`geometry::inscribed_square` reads as `inscribed_disc::geometry::inscribed_square`
after the rename, which stutters. Rename it **`largest_square`** and keep its
doc comment's measured content: it is searched against the predicate rather than
computed as `floor(d / √2)`, because on a 240-pixel panel the closed form gives
169 whose corner is at 169² + 169² = 57,122 against the disc's 239² = 57,121, and
the answer is 168.

Leave every other public name alone. `is_lit`, `lit_start`, `lit_span`,
`lit_chord`, `box_is_lit`, `Surface`, `ByteColor`, `Clip::Disc`, `Clip::Rect`,
`Abgr2222` and `shade` all keep their names.

### Every file that mentions the old name

Tracked:

```
.github/workflows/app-build.yml
.github/workflows/panelkit.yml
README.md
PanelKit/Cargo.toml
PanelKit/Cargo.lock
PanelKit/README.md
PanelKit/panelkit.cmake
PanelKit/Header/GuiShell.hpp
PanelKit/src/lib.rs
PanelKit/src/surface.rs
PanelKit/src/panic.rs
PanelKit/src/preview.rs
PanelKit/src/widgets.rs
PanelKit/examples/gamut.rs
PanelKit/Docs/DESIGN.md
PanelKit/Docs/2026-09-08-generic-vs-concrete.md
PanelKit/Docs/2026-09-09-panic-handler-cost.md
PanelKit/Docs/measurements/genericity/src/lib.rs
PanelKit/Docs/measurements/panic-handler/{Cargo.toml,Cargo.lock,src/main.rs}
PanelKit/Docs/measurements/tier-footprint/{Cargo.toml,Cargo.lock,src/lib.rs}
Spin/Software/Apps/CustomGUI/Gui.cpp
Spin/Software/Apps/CustomGUI/spin_gui.h
Spin/Software/Apps/CustomGUI/rust/{Cargo.toml,Cargo.lock}
Spin/Software/Apps/CustomGUI/rust/src/lib.rs
Spin/Software/Apps/CustomGUI/rust/src/bin/preview.rs
Spin/Software/Apps/CustomGUI/rust/examples/frametime.rs
Spin/Software/Apps/Spin-CMake/CMakeLists.txt
```

Untracked, and still yours to fix:

```
SettingsEditor/Software/Apps/CustomGUI/rust/{Cargo.toml,Cargo.lock,src/lib.rs}
PanelKit/Docs/2026-09-10-publish-or-not.md
PanelKit/Docs/measurements/disc-wrapper/**
Docs/PANELKIT-PROMPT.md
Docs/PANELKIT-ECOSYSTEM-PROMPT.md
```

Use `git mv` for tracked paths so history follows. Verify with a case-insensitive
sweep at the end: `git grep -i panelkit` must return nothing, and so must a
`grep -ri panelkit` over the working tree excluding `.git`, `target/` and `tmp/`.

---

## 2. The three scope changes

These are the only functional changes. Each has evidence behind it; keep the
evidence attached where you record the result.

### 2.1 Add `DiscClipped` — the highest-value change

`Surface` is bounded on `ByteColor`, so it serves one byte per pixel only. That
bound is why the crate's addressable population is three Sharp panels rather
than every round display. `Rgb565` — which is what every GC9A01 round module is,
including everything `gc9a01-rs` drives — does not compile against it.

A disc-clipping wrapper over *any* `DrawTarget` has none of that limit. One is
already written, tested and measured at
`PanelKit/Docs/measurements/disc-wrapper/src/disc.rs`. **Promote it into the
crate** as a module (`clip`), exported at the root.

What is already established about it, and must stay true:

- It paints **byte-identical** pixels to `Surface::round` on a scene that
  straddles the rim on every row. The test is
  `the_wrapper_paints_exactly_what_the_surface_paints`; bring it into the crate's
  own tests.
- It disc-clips `Rgb565` and `BinaryColor` over `embedded-graphics-framebuf` with
  **0 pixels behind the bezel**. Bring both tests in. Add
  `embedded-graphics-framebuf` as a **dev-dependency only** — it must not become
  a real dependency.
- Linked `.text` on `thumbv8m.main-none-eabihf` at `opt-level = "z"`: 444 for a
  plain rect-clipped target, 688 for the wrapper over it, 772 for
  `Surface::round`.
- Frame time, median of nine interleaved trials on an aarch64 host: 1.15–1.20×
  `Surface::round` with the per-row `fill_solid`, 7.4–8.5× without it. Keep the
  per-row path and the span-ends early-out; both are why.

`DiscClipped` and `Surface` must agree about the clip, and the way they agree is
by both going through `geometry`. Do not give `DiscClipped` its own copy of
`is_lit` or of the half-chord — the harness copy has one because it was written
to be independent of the crate, and that is exactly what must not survive
promotion. A test that the two paths paint the same pixels is the guard.

### 2.2 Make `embedded-graphics` an optional dependency

`src/geometry.rs` has **no `use` statements at all**. Verified: it compiles as a
standalone crate with an empty dependency tree, its eleven tests pass, and it
builds for `thumbv8m.main-none-eabihf`.

So `geometry` should be the zero-dependency core, and `embedded-graphics` a
default-on optional dependency gating everything that needs it. A consumer who
wants only "is this pixel on the glass" — a custom renderer, an LVGL binding, a C
FFI consumer — should be able to have it with nothing in their tree.

Resulting feature graph:

| feature | default | gates |
|---|---|---|
| — | always | `geometry` |
| `embedded-graphics` | **on** | `clip` (`DiscClipped`), `surface` (`Surface`, `ByteColor`) |
| `abgr2222` | off | `color` (`Abgr2222`, `shade`, the palette, `GREY_LEVELS`, `CHANNEL_MAX`) |
| `preview` | off | `preview`; implies `std` and `abgr2222`; brings `png` |
| `panic-handler` | off | `panic`, location only |
| `panic-message` | off | implies `panic-handler`; adds the message |
| `std` | off | host assertions |

`DESIGN.md` §10 currently argues there is deliberately no `abgr2222` feature
because "the colour type is always present, so a feature for either would gate
nothing and say it did." That reasoning was about a crate whose only audience had
that panel. It is now wrong, because gating it removes real code for the majority
of the new audience. Update that passage rather than leaving it to contradict the
manifest.

Prove the gating works: `--no-default-features` must build for both
`thumbv8m.main-none-eabihf` and `riscv32imac-unknown-none-elf`, and
`cargo tree --no-default-features -e normal` must show no dependencies.

### 2.3 Delete `widgets`

`Marks` and `Pill` have **no caller in committed application code.** Their only
users are the uncommitted `SettingsEditor` renderer and
`Docs/measurements/tier-footprint`, which `DESIGN.md` itself says is a fixture
that exists to be measured and not a use. That is one caller, below the
two-caller bar this repository sets for widgets in `DESIGN.md` §8 item 9.

Remove the module, the `widgets` feature, its tests, its mention in `lib.rs`'s
crate-level example, its row in `PanelKit/README.md`'s module table, and its use
in the `tier-footprint` fixture. Record it in `DESIGN.md`'s **What is parked**
table beside `nav`, `text`, `scene`, `draw` and `dither`, with the commit it is
recoverable from, in the same register as those five: what it was, and why it
went.

If `SettingsEditor` is committed later and needs a page-indicator row, it comes
back and brings its second caller with it. Say that in the parked entry.

---

## 3. Stale references to remove completely

Each of these is currently false or about to be. Delete or correct — and where a
claim is the kind this repository cannot keep true, **delete it rather than
update the number**, because updating only resets the clock.

1. **`PanelKit/README.md`**: "the disc clip's fast path is worth 128 µs a frame
   against 43." Misattributed. 43 µs is rect clipping with **no disc at all**;
   the fast path lands at 57.9 µs against the 128 µs scan it replaced. The same
   figures appear in `src/geometry.rs`'s `lit_start` doc comment, which states
   them correctly — make the README agree with it. Source:
   `tmp/adversarial-review/panelkit/frametime.txt`.
2. **`DESIGN.md` §10**: `Cargo.toml` "declares `MIT OR Apache-2.0`" against an
   MIT `LICENSE`. Settled — the manifest says `MIT` and the file is MIT. Delete
   the entry from **What is not built**.
3. **`DESIGN.md` §10**: `embedded-graphics-framebuf` "does not fit". Half wrong,
   and the wrong half decides the crate's audience: `FrameBuf<C: PixelColor, B:
   FrameBufferBackend<Color = C>>` is generic over `PixelColor`, which is
   *more* general than `Surface`. What it lacks is the disc clip, and it supplies
   only `draw_iter` and `clear` — no `fill_solid` or `fill_contiguous` — so it is
   per-pixel regardless. Rewrite to say that, and note that `DiscClipped` over it
   is now the composition the crate offers for panels `Surface` refuses.
4. **`DESIGN.md` §10 and §11 on `ByteColor`**: the passages describing the
   `Rgb565` error and the byte-per-pixel limit predate commit `342fba0`, which
   made `ByteColor` a blanket alias over
   `PixelColor<Raw = RawU8> + From<RawU8> + Into<RawU8>`. `Gray8` now works with
   no impl written anywhere, and the orphan-rule dead end an adopter used to hit
   is gone. The byte-per-pixel limit itself still stands. Update to match, and
   keep the measured note that the change cost 0 bytes — linked tier-0 `.text` is
   656 either side.
5. **`Docs/PANELKIT-PROMPT.md` and `Docs/PANELKIT-ECOSYSTEM-PROMPT.md**: both
   untracked, both superseded — the first by the crate existing, the second by
   `Docs/2026-09-10-publish-or-not.md`. Delete them.
6. **Root `README.md`**: the `PanelKit` row describes "two widgets" and says the
   Rust half is "shaped so [it] could be published". Rewrite for what the crate
   now is and where it now lives.
7. **`DESIGN.md` §11**: the `SettingsEditor` section correctly says the app is
   uncommitted and quotes none of its figures. Keep that discipline — do not
   start citing it now.
8. **Any count of tests, files or lines** you find in prose. Two were already
   removed for being wrong; do not reintroduce the pattern. `cargo test
   --all-features` passing is the claim; the number is not.

---

## 4. Publication readiness

Set these once, correctly:

- `description` — say round, say what it clips, and do not promise primitives.
- `keywords` — the discovery channel is search over description and keywords, not
  the name: crates.io returns `embedded-graphics` for a `gc9a01` query without
  that string in its name. Include the terms a reader with a round panel would
  type.
- `repository` — currently points at `/tree/main/PanelKit`. Update the path.
- `include` — must still package only Rust sources, the README, the design record
  and the licence. `cargo package --list` must show nothing from `Docs/measurements/`,
  `Header/` or the CMake glue.
- `rust-version` — currently `1.81`, where `embedded-graphics` needs 1.71.1.
  Establish what actually requires 1.81 and lower it if nothing does.

Then verify, and put the commands and their output in the commit body or the
design record, not in a comment:

```
cargo test --all-features
cargo clippy --all-features --all-targets -- -D warnings     # CI fails on any warning
RUSTDOCFLAGS="-D warnings" cargo doc --all-features --no-deps
cargo build --release --no-default-features --target thumbv8m.main-none-eabihf
cargo build --release --no-default-features --target riscv32imac-unknown-none-elf
cargo tree --no-default-features -e normal                    # must be empty
cargo package --list
cargo publish --dry-run
```

And the two that catch what compilation does not:

```
# The renamed extern symbol actually resolves.
llvm-nm --undefined-only <the app archive> | grep host_panic

# Spin's frames are unchanged. It is installed on a watch; the bar is not
# "still compiles" but "draws the identical pixels".
cd Spin/Software/Apps/CustomGUI/rust && cargo test        # the 43-scene golden
```

`llvm-size` and `llvm-nm` ship with the toolchain but are not on `PATH`: they are
under `$(rustc --print sysroot)/lib/rustlib/<host>/bin`.

**Do not run `cargo publish`.** Stop at `--dry-run` and hand back.

---

## 5. What not to do

- **Do not split the crate.** One crate. `color`, `preview` and `panic` stay in
  it behind default-off features and are published with it. They are not harmful
  to publish: default-off features are not compiled, and `Abgr2222` is the
  correct type for the three round Sharp `DD` panels, which no crate on crates.io
  provides. A split has one in-repo consumer today, which is the condition that
  parked five modules already; it is a normal 0.x operation to do later if a
  second consumer appears.
- **Do not un-park anything.** `nav`, `text`, `scene`, `draw` and `dither` stay
  in git. `widgets` joins them.
- **Do not lift the byte-per-pixel limit on `Surface`.** `DiscClipped` is the
  answer for other widths. Writing the stride arithmetic against
  `PixelColor::Raw` wants an adopter asking for it and the genericity measurement
  re-run at that width.
- **Do not bump `version` by hand.** CI cuts the release from the commit type.
- **Do not touch `Spin`'s arc sampler.** It stays on `micromath`; swapping the
  trig would move pixels, and the 43 golden frames are what is being protected.

---

## 6. Commits

Conventional commits, one logical change each, staged deliberately —
`CLAUDE.md` records that a `git add <App>` once swept an unrelated change into a
commit whose message described only the intended fix. A reasonable sequence:

1. `refactor!` — the rename, mechanical, no behaviour change.
2. `feat` — `DiscClipped`, with the footprint and frame-time numbers in the body.
3. `refactor!` — `embedded-graphics` optional, with the empty-dep-tree proof.
4. `refactor!` — remove `widgets`, with the caller evidence.
5. `docs` — the stale references in §3.
6. `chore` — the manifest metadata in §4.

Then report: what you renamed, what you measured and with which command, what
you deleted and why, and anything in §1–§4 you could not do and what blocked it.
If a claim in this prompt turned out to be wrong, say so and say how you know —
several of these numbers have already survived one adversarial review, which does
not make them true.
