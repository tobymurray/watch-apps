# inscribed-disc

Drawing primitives for **round, low-bit-depth** displays: an
[`embedded-graphics`] surface that clips to the glass rather than to the
framebuffer, and colour arithmetic for a channel with a handful of levels
instead of 256. `no_std`, no allocator, no font.

```toml
[dependencies]
inscribed-disc = { version = "0.1", default-features = false }
```

## Is this for you?

Take it only if all four hold. Each is a hard constraint, not a preference.

- **Your display is round.** On a rectangular panel this gives you nothing: the
  disc clip and the chord table are the whole value.
- **Your framebuffer is one byte per pixel.** `Surface` is bounded on
  `ByteColor`, which any colour whose `PixelColor::Raw` is `RawU8` satisfies
  with no impl written anywhere — `Gray8` included. `Rgb565` **does not
  compile** against it. Lifting that means writing the stride arithmetic against
  `PixelColor::Raw` rather than `u8`, which has not been done.
- **Your colour depth is low.** `shade()` and the palette assume a channel has a
  few levels. At 8 bits a channel, use `embedded-graphics` directly.
- **You push whole frames.** There is no damage tracking or partial redraw, on
  purpose. A display that only accepts dirty rectangles gains nothing here.

The panel this was written for is 240×240 at two bits a channel — 64 colours,
and the only greys are 0, 85, 170 and 255.

## What it does

```rust
use inscribed_disc::{color::Abgr2222, surface::Surface};

let mut fb = [0u8; 240 * 240];
let mut s = Surface::<Abgr2222>::round(&mut fb, 240, 240).unwrap();
s.clear(Abgr2222::BLACK);
s.fill_rect(60, 100, 120, 40, Abgr2222::WHITE);
```

`Surface` is a `DrawTarget`, so anything in the `embedded-graphics` ecosystem
draws onto it — and everything that does is clipped to the inscribed disc, not
to the square buffer. That distinction is the reason this crate exists: a box
inset from the framebuffer's edge is not inset from the glass, and no simulator
shows the difference.

| module | |
|---|---|
| `surface` | the framebuffer, the clip, a row-fill `fill_solid` |
| `color` | `Abgr2222`, its palette, and `shade` for partial coverage |
| `geometry` | the lit-disc predicate, row chords, the inscribed square |
| `widgets` | a page indicator and a toggle pill *(feature `widgets`)* |
| `preview` | frames to PNG in the panel's own colours *(feature `preview`, host-only)* |
| `panic` | a `#[panic_handler]` reporting `file:line` *(feature `panic-handler`)* |

Text is not included and neither is a font: bring your own rasteriser and hand
it the buffer through `Surface::bytes_mut`, applying `geometry::is_lit` yourself.

## Constants that came from measuring

The crate carries a few numbers that were counted rather than reasoned, and each
is re-derived by a test on every build — the widest row of a 240-pixel panel is
238 and every chord is even; the largest centred square is 168, where the closed
form says 169; the disc clip's fast path is worth 128 µs a frame against 43.
[`Docs/DESIGN.md`](Docs/DESIGN.md) has them with what would falsify each.

## Status

**0.1.** One application uses it, and has recorded a session on the watch it was
written for. Modules that had no caller were removed rather than published; the
design record says which, and they can come back when something needs them.

## Licence

MIT. See [`LICENSE`](LICENSE).

[`embedded-graphics`]: https://crates.io/crates/embedded-graphics
