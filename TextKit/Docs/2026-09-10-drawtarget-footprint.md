# Drawing through a `DrawTarget`, against owning a framebuffer

2026-09-10. What it cost, on `thumbv8m.main-none-eabihf`, to delete this
crate's `Canvas` and blit through `embedded_graphics::DrawTarget` instead.

The question was live because `inscribed-disc` had measured its own primitives
generic against concrete at +7.5%
(`InscribedDisc/Docs/2026-09-08-generic-vs-concrete.md`) and concluded the
abstraction was nearly free. A glyph blit is not a filled rectangle: it touches
a pixel at a time, so that figure had no business being assumed here.

## Method

[`measurements/drawtarget`](measurements/drawtarget), a `staticlib` probe
linking `draw`, `draw_top`, `measure`, `wrap` and `pick` from one call, built
with the crate's release profile -- `opt-level = "z"`, `lto = true`,
`codegen-units = 1`, `panic = "abort"`.

Because LTO inlines this crate into the probe's own codegen unit, there is no
`textkit-*` archive member to measure; the `textprobe-*` member is the sum of
the probe's glue and everything it pulled in, and the glue is the same in every
variant.

```sh
cd TextKit/Docs/measurements/drawtarget
cargo build --release --target thumbv8m.main-none-eabihf
llvm-size --format=sysv target/thumbv8m.main-none-eabihf/release/libtextprobe.a \
  | awk -v crate='^textprobe-' -f ../../../../InscribedDisc/Docs/measurements/textsize.awk
```

`llvm-size` is not on `PATH` on a plain macOS install; it ships inside the
rustup toolchain, at
`$(rustc --print sysroot)/lib/rustlib/*/bin/llvm-size`.

## Result

| variant | `.text` | `.rodata` | vs the owned canvas |
|---|---|---|---|
| `Canvas`, direct byte writes | 2,416 | 7,399 | — |
| `DrawTarget`, shade per pixel | 2,908 | 7,399 | +492 (+20.4%) |
| **`DrawTarget`, shade hoisted** | **2,620** | 7,399 | **+204 (+8.4%)** |

## What it says

**The 20% was not the abstraction. It was calling `Ink::shade` once a pixel.**

Atlas coverage is quantised to four levels, so a run of text has exactly three
distinct non-zero shades no matter how many pixels it covers. Computing them
once per `draw` call and indexing a three-entry ramp took 288 bytes back, and
left the cost of drawing through a `DrawTarget` at +204 bytes, or +8.4% -- in
line with the +7.5% inscribed-disc measured for its fills, and now measured
here rather than assumed.

Falsified by rerunning the command above.

## What was bought with it

The seam is gone. Text used to clip to the disc through this crate's copy of
the rule while the shapes beside it clipped to the rectangle through the app's
framebuffer, over the same bytes. Every renderer now draws everything through
one target.

625 frames across the five renderers are byte-identical before and after
(`*/Software/Apps/CustomGUI/rust/examples/frames.rs`), so none of this moved a
pixel -- including Spin's 43 scene goldens, which passed without regeneration.
