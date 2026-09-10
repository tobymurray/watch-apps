# Could forty lines replace `Surface`?

2026-09-10. The harness behind
[`Docs/2026-09-10-publish-or-not.md`](../../2026-09-10-publish-or-not.md).

`src/disc.rs` is a disc-clipping `DrawTarget` wrapper written over *any* target,
in two forms: `DiscClipped`, which supplies a per-row `fill_solid`, and
`DiscNaive`, which filters `draw_iter` and leaves `fill_solid` at the default.
`src/lib.rs` carries `Plain`, a rectangular byte-per-pixel target standing in
for "some target the adopter already has".

## Re-running it

```
cd InscribedDisc/Docs/measurements/disc-wrapper

# The wrapper paints the same pixels as Surface, and reaches Rgb565 and
# BinaryColor, which Surface does not compile against.
cargo test --release

# Frame time, nine interleaved trials reporting the median. A single pass
# moved Surface::round from 22.4 to 32.5 us between runs, which is why.
cargo run --release --example bench

# The crate's published constants, re-derived, at four panel sizes.
cargo run --release --example counts

# Linked .text on the watch's target, four clip strategies.
cd footprint
for f in plain surface wrapper promoted naive; do
  cargo build --release --target thumbv8m.main-none-eabihf --features $f
  llvm-size --format=sysv target/thumbv8m.main-none-eabihf/release/wrapcost
done

# Whether spelling the bound as PixelColor<Raw = RawU8> costs anything.
# One body, two bounds; only how the trait is satisfied differs.
cd ../bound
for f in bytecolor rawu8; do
  cargo build --release --target thumbv8m.main-none-eabihf --features $f
  llvm-size --format=sysv target/thumbv8m.main-none-eabihf/release/boundcost
done
```

`llvm-size` ships with the toolchain but is not on `PATH`; it is under
`$(rustc --print sysroot)/lib/rustlib/<host>/bin`.

## What it found

| linked `.text`, `thumbv8m.main-none-eabihf` | bytes |
|---|---|
| `Plain`, rect clip only | 444 |
| `inscribed_disc::Surface::round` | 772 |
| `DiscClipped` over `Plain`, the standalone copy in `src/disc.rs` | 688 |
| **`inscribed_disc::clip::DiscClipped`**, after promotion | **704** |
| `DiscNaive` over `Plain` | 374 |

`DiscNaive` is below the rect-clip floor because it never calls the parent's
`fill_solid`, so that row fill is garbage-collected. It is not the cheaper
option; it is the one that gave up the fast path.

Promotion cost 16 bytes. The standalone copy carries its own half-chord; the
promoted one calls `geometry::lit_span`, which also answers the single-pixel
case and returns a clamped interval. Those 16 bytes buy
`the_wrapper_paints_exactly_what_the_surface_paints`: one predicate rather than
two that happen to agree.

| host frame time, median of nine | µs | vs `Surface::round` |
|---|---|---|
| `Plain`, rect clip | 12–14 | 0.6× |
| `inscribed_disc::Surface::round` | 20–22 | — |
| **`inscribed_disc::clip::DiscClipped`** | 24–26 | **1.18–1.19×** |
| `DiscClipped`, the standalone copy | 23–25 | 1.15× |
| `DiscNaive` | 158–180 | **7.4–8.6×** |

Not a device measurement: aarch64 host. Ratios, not microseconds.

## What would falsify it

- **A different scene.** `bench`'s frame is three full-width bands, a ring and
  600 scattered pixels. A frame that is mostly single pixels moves the naive
  wrapper's ratio toward 1, because there is no `fill_solid` to give up; a frame
  that is mostly large fills moves it the other way.
- **The device.** A Cortex-M33 with no data cache should make the naive
  wrapper's 7.4–8.5× worse, not better. Nothing here has run on glass.
- **A parent that overrides `fill_solid`.** `Plain` does.
  `embedded-graphics-framebuf` 0.5.0 does not, so `DiscClipped` over it is
  per-pixel whatever the wrapper does — checked by reading its `DrawTarget`
  impl, which supplies `draw_iter` and `clear` only.
- **`opt-level`.** This is `z`, matching the apps.
