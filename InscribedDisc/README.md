# inscribed-disc

On a **round** display the framebuffer is square and the glass is not, so
nothing stops you painting pixels the wearer will never see. This says where the
glass is, and gives you [`embedded-graphics`] draw targets that will not let you
paint off it. `no_std`, no allocator, no font.

```toml
[dependencies]
inscribed-disc = "0.1"
```

`default-features = false` leaves `geometry` alone — the predicate, the chords
and the largest centred square, with nothing in your dependency tree.

## Is this for you?

Two hard constraints, and they are the whole gate.

- **Your display is round, and its glass is the inscribed disc of the
  framebuffer.** On a rectangular panel this gives you nothing, and on a round
  bezel over a square screen whose corners show — Bangle.js 1 — a disc clip
  would be wrong.
- **You push whole frames.** There is no damage tracking or partial redraw, on
  purpose. A display that only accepts dirty rectangles gains nothing here.

After that it is a question of which part you take.

- `geometry` and `clip` have **no colour constraint at all**. `DiscClipped`
  wraps any `DrawTarget`, so `Rgb565` — which is every GC9A01 module — and
  `BinaryColor` are clipped as readily as anything else.
- `Surface` is a framebuffer of its own and is bounded on `ByteColor`, so it
  serves **one byte a pixel**: any colour whose `PixelColor::Raw` is `RawU8`
  satisfies it with no impl written anywhere, `Gray8` included, and `Rgb565`
  does not compile against it. Lifting that means writing the stride arithmetic
  against `PixelColor::Raw` rather than `u8`, which has not been done —
  `DiscClipped` over a framebuffer you already have is the answer at other
  widths.
- `color` assumes a channel has **a few levels**: `shade()` and the palette are
  for two bits a channel. At 8 bits a channel, use `embedded-graphics` directly.

The panel this was written for is 240×240 at two bits a channel — 64 colours,
and the only greys are 0, 85, 170 and 255.

## What it does

```rust
use embedded_graphics::{pixelcolor::Gray8, prelude::*};
use inscribed_disc::Surface;

let mut fb = [0u8; 240 * 240];
let mut s = Surface::<Gray8>::round(&mut fb, 240, 240).unwrap();
s.clear(Gray8::BLACK);
s.fill_rect(60, 100, 120, 40, Gray8::WHITE);
```

Any colour whose `PixelColor::Raw` is `RawU8` works there with no impl written
anywhere. For two bits a channel, the `abgr2222` feature brings `Abgr2222` and
`shade`.

`Surface` is a `DrawTarget`, so anything in the `embedded-graphics` ecosystem
draws onto it — and everything that does is clipped to the inscribed disc, not
to the square buffer. That distinction is the reason this crate exists: a box
inset from the framebuffer's edge is not inset from the glass, and no simulator
shows the difference.

For a panel `Surface` will not take — anything wider than a byte a pixel —
`DiscClipped` wraps a `DrawTarget` you already have and applies the same clip:

```rust
use embedded_graphics::{
    pixelcolor::Rgb565, prelude::*, primitives::{PrimitiveStyle, Rectangle},
};
use embedded_graphics_framebuf::FrameBuf;
use inscribed_disc::DiscClipped;

let mut data = [Rgb565::BLACK; 240 * 240];
let mut fb = FrameBuf::new(&mut data, 240, 240);
let _ = Rectangle::new(Point::new(4, 4), Size::new(232, 232))
    .into_styled(PrimitiveStyle::with_fill(Rgb565::WHITE))
    .draw(&mut DiscClipped::new(&mut fb));
```

That 4-pixel inset is the mistake the crate is about: on a rectangular target it
paints 12,792 pixels the glass never shows, 22.2% of the framebuffer.

| module | | feature |
|---|---|---|
| `geometry` | the lit-disc predicate, row chords, the largest centred square | always |
| `clip` | `DiscClipped`, a disc clip over any `DrawTarget`, at any colour depth | `embedded-graphics`, on by default |
| `surface` | the framebuffer, the clip, a row-fill `fill_solid` | `embedded-graphics`, on by default |
| `color` | `Abgr2222`, its palette, `decode` to 8-bit RGB, and `shade` for partial coverage | `abgr2222` |
| `preview` | frames to PNG in the panel's own colours, host-only | `preview` |

Text is not included and neither is a font: bring your own rasteriser and hand
it the buffer through `Surface::bytes_mut`, applying `geometry::is_lit` yourself.

## Constants that came from measuring

The crate carries a few numbers that were counted rather than reasoned, and each
is re-derived by a test on every build: the widest row of a 240-pixel panel is
238 and every chord is even, and the largest centred square is 168, where the
closed form says 169.

The frame time is the one that is not a test, so it is quoted with what it
compares. Drawing 43 real scenes, `Surface::round` costs **57.9 µs a frame**
with the span-ends fast path, against **128 µs** for the row scan it replaced
and **43 µs** for rect clipping with no disc at all. So the fast path recovers
128 to 57.9; it does not reach 43, because 43 is not a disc clip.

[`Docs/DESIGN.md`](Docs/DESIGN.md) has them with what would falsify each.

## Status

**0.1.** One application uses it, and has recorded a session on the watch it was
written for. Modules that had no caller were removed rather than published; the
design record says which, and they can come back when something needs them.

## Licence

MIT. See [`LICENSE`](LICENSE).

[`embedded-graphics`]: https://crates.io/crates/embedded-graphics
