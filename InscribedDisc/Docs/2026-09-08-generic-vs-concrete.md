# Generic over `PixelColor`, against a concrete colour type

2026-09-08. What it costs, on `thumbv8m.main-none-eabihf`, to write this kit's
primitives against `embedded_graphics::DrawTarget` rather than against one
concrete `Abgr2222` framebuffer.

Nothing in this repository knew this, and both the tiering decision and the
publishing decision turn on it: if genericity is expensive, the kit is concrete
with a generic veneer and cannot honestly be published as platform-generic.

## Method

Four primitives — the ones tier 0 and tier 1 actually contain — written three
ways, with only the abstraction differing between them:

| variant | what it is |
|---|---|
| `concrete` | one colour type, direct byte writes, row `fill` fast path |
| `generic` | `DrawTarget`-generic, instantiated at one colour type |
| `generic_fast` | the same, with `fill_solid` overridden to fill by rows |
| `generic_two` | the same generic code, instantiated at two colour types |

The primitives: a clipped filled rectangle, a disc-clipped fill, a centred
page-indicator row, and a swept arc with a shared trig helper, so the arc's own
maths is identical in every variant and cannot differ between them.

Built with the crate's release profile — `opt-level = "z"`, `lto = true`,
`codegen-units = 1`, `panic = "abort"` — as `crate-type = ["staticlib"]`, and
measured with `llvm-size --format=sysv`, summing `.text*` sections **of the
crate's own archive member only**. The archive carries all of `core` and
`compiler_builtins`; an archive-wide sum is not a footprint.

Harness: [`measurements/genericity`](measurements/genericity), checked in beside
this file so the numbers can be re-derived rather than trusted.

```
cd InscribedDisc/Docs/measurements/genericity
for f in concrete generic generic_fast generic_two; do
  rm -f target/thumbv8m.main-none-eabihf/release/libgenericity.a
  cargo build --release --target thumbv8m.main-none-eabihf --features $f
  llvm-size --format=sysv target/thumbv8m.main-none-eabihf/release/libgenericity.a \
    | awk -v crate='^genericity-' -f ../textsize.awk
done
```

## Result

| variant | `.text` | `.rodata` | vs concrete |
|---|---|---|---|
| `concrete` | 884 | 59 | — |
| `generic` | 966 | 27 | +82 (+9.3%) |
| **`generic_fast`** | **950** | 43 | **+66 (+7.5%)** |
| `generic_two` | 1,814 | 27 | +930 (+105%) |

## What it says

**The abstraction is nearly free at one instantiation: 66 bytes, 7.5% of the
primitive set.** An app instantiates exactly one colour type, so that is what an
app pays. The kit is therefore generic, and the concrete `Abgr2222` path does
*not* need to be a feature-gated fork.

**A second colour type in the same binary costs 848 bytes.** That is
monomorphisation behaving exactly as expected, near-linear, and no watch app
pays it — it is what an off-watch adopter's `Rgb565` build costs in its own
binary, which is where it belongs.

**Overriding `fill_solid` is 16 bytes *cheaper* than not overriding it.** The
default walks `fill_contiguous` a pixel at a time; a specialised body replaces
that machinery rather than adding to it, and restores the row-fill fast path at
the same time. `Surface` supplies it. This is the one result that was not
predictable in advance and it is why the generic path is not slower at runtime
than the concrete one it replaced.

## The instrument defect, which is the important part

**The first run of this experiment gave +710 (+80%) for `generic`.** That number
argues for the opposite architecture — a concrete kit, with genericity as an
expensive veneer to be avoided.

It was wrong. `pub mod concrete` had no `#[cfg]`, so it was compiled into the
`generic` build too, and the "generic" figure was generic *plus* concrete. The
total looked entirely plausible; nothing about 1,594 bytes for four primitives
says "this is two implementations". What exposed it was reading the per-symbol
breakdown, where `concrete::arc` and `concrete::fill_rect` appear in a build
that should not contain them.

Gating each module behind its own feature is the fix, and the harness as checked
in has that gating. The lesson is
`FINDINGS.md`'s: an instrument that can be misread by someone who does not
already know the answer is not finished. **Re-measure by symbol, not by total.**

## What would falsify this

- A different `opt-level`. This is `z`; `s` or `3` inline differently and the
  gap may widen.
- A colour type wider than one byte, where `to_byte`/`from_byte` stop being
  free and the `ByteColor` bound stops being the right one.
- More than about four distinct `DrawTarget` implementations in one binary,
  where each monomorphisation is another copy of every generic primitive. Two
  cost 848; the growth is linear and there is no reason to expect it to stop.
- A future `embedded-graphics` whose `fill_solid` default is no longer
  per-pixel, which would remove the 16-byte win and most of the runtime one.
