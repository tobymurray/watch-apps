# ITF

Interleaved 2 of 5, added because a wearer has a card that is printed as ITF
and the system that reads it expects ITF. That is the whole of the case, and it
is worth saying plainly because the case that *used* to be made for ITF —
density — is not a case any more.

## Why not for density

[SYMBOLOGIES.md](SYMBOLOGIES.md) ranked the linear candidates by modules per
character and put ITF near the top on the strength of a 2:1 ratio. Two things
have happened since:

- **There is no ISO minimum X-dimension** (`f92e1ce`). The app was never
  outside a standard, so there was never a density defect to fix.
- **Subset C landed** (`dd119a4`), which took numeric ids to roughly half the
  modules inside Code 128 — no new format, no new configuration, no scanner
  that reads the app today losing it.

So ITF is not here because it draws wider bars. At the ratio this app uses it
draws *narrower* ones than subset C does for the same digits. It is here
because a symbology is an interoperability contract, and some tills only
accept their side of it.

## What it accepts

Two to sixteen digits, an **even** number of them.

Odd is **refused, not padded**. Every other ITF implementation, zint included,
pads a leading zero and encodes the result — and a scanner then reads that zero
back as part of the number. A barcode that scans as a number the wearer did not
type is the one harm [Barcode.hpp](../Software/Libs/Header/Barcode.hpp) is
written to prevent, so this refuses and the screen says why. A card in the wild
always carries an even number of digits, because it could not have been printed
as ITF otherwise; an odd one here means a typo.

## Two decisions the panel made

### The ratio is 3:1

ISO/IEC 16390 leaves the wide:narrow ratio to the application. Tutorials use
2:1 because it is densest, and density is the wrong thing to spend on here:

- Resolution is not the binding constraint. At eight digits the narrow element
  is 0.252 mm, twice the 5 mil reference the README uses.
- Edge definition might be. The panel is 8bpp ABGR2222 — **four levels a
  channel** — so a bar boundary that lands mid-pixel steps rather than blends,
  and a decoder tells narrow from wide by comparing widths. The further apart
  those two are, the more rounding the symbol survives.

3:1 spends headroom the screen has on the thing it is short of. It is also what
zint emits, which is what makes a width-for-width diff against it possible.

### Elements are whole pixels, and the symbol is centred

Code 128 is drawn by stretching its run to fill the band, so a module is a
fractional number of pixels and every bar edge is anti-aliased. ITF is not: the
element is rounded **down** to a whole number of pixels and the symbol is
centred in what that leaves.

That costs symbol width and buys two things — no anti-aliasing anywhere, and an
exact 3:1 rather than one that wobbles by half a pixel per element. The width
is not lost: it goes to the quiet zone, which was the tighter constraint.

**The element is sized from the backing, not from the bars band.** This is the
part that was wrong first. The obvious arithmetic is "200 px of bars divided by
the number of elements", and it passes at 2, 8, 12, 14 and 16 digits and fails
at 4, 6 and 10 — because the symbol grows into the white the quiet zone needed.
The budget is the full 220 px of white for `units + 2 × 10` elements, and
`ItfPanel.TheQuietZoneMeetsTenElementsAtEveryLength` is the test that caught it.

| Digits | Units | Element | X-dimension | Symbol | Quiet zone | Bearer |
| --- | --- | --- | --- | --- | --- | --- |
| 2 | 27 | 4 px | 504 µm | 108 px | 56 px | 8 px |
| 4 | 45 | 3 px | 378 µm | 135 px | 42 px | 6 px |
| 6 | 63 | 2 px | 252 µm | 126 px | 47 px | 4 px |
| 8 | 81 | 2 px | 252 µm | 162 px | 29 px | 4 px |
| 10 | 99 | 1 px | 126 µm | 99 px | 60 px | 4 px |
| 12 | 117 | 1 px | 126 µm | 117 px | 51 px | 4 px |
| 14 | 135 | 1 px | 126 µm | 135 px | 42 px | 4 px |
| 16 | 153 | 1 px | 126 µm | 153 px | 33 px | 4 px |

From ten digits the element is a single pixel — 126 µm, the 5 mil reference
exactly. There is no trick left at that point: the quiet zone is already
binding and the backing cannot grow without leaving the lit circle.

## Bearer bars, which are not decoration

ITF is continuous and carries **no check character**. A scan that clips the top
or bottom of the symbol can pick up a subset of the bars and decode it as a
valid *shorter* number. That is not a theoretical failure — it is the reason
GS1 requires bearer bars on ITF-14 — and it is precisely the "plausible value
that scans" outcome this app exists to refuse.

So a bar runs flush above and below the symbol, at least two elements thick and
never under 4 px. A clipped scan crosses solid ink and fails instead of
returning half a number. ITF's bars are shorter than Code 128's for this
reason, and `ItfPanel.TheBearerBarsAndTheBarsFitTheBand` holds the arithmetic.

## Configuration

`ITF`, in the `fmtN` field, matched without regard to case — the third word in
a vocabulary that already had `Code128` and `QRCode`. Nothing about the config
shape changed: no new field, no new field count, and an `input.json` written
before this still means what it meant.

The gap this leaves is the one the SDK's field model makes unavoidable:
**patterns are per-field**, so `id1`'s pattern cannot depend on `fmt1`. The
phone will happily save `fmt1=ITF` alongside `id1=A1234567` and the watch is
where that is caught. `Problem::BadValue` already covers it, and its prompt
says what an id may contain — but it cannot say "ITF wants an even number of
digits" specifically, because one prompt serves every format. That is a known
cost of the shape, recorded rather than solved.

## Evidence

**Structure.** Ten rows, five elements each, exactly two wide — the "2 of 5"
itself, which is what a transcription slip breaks. Plus all ten rows distinct.

**Round trip.** A decoder built the other way round: elements back to
narrow/wide, de-interleaved into digits. It shares the table, so it is evidence
about the interleaving and the bookkeeping rather than about the table.

**zint.** 24 vectors, width for width against an independent implementation
with its own table and its own interleaving. Even lengths only, by the stated
rule above, because zint pads and this refuses; the refusal is tested directly
instead. The corpus puts every digit in both a bar position and a space
position, covers every length the app accepts, and includes an ITF-14 shape.

**The framebuffer, captured and decoded.** Driven headless under `Xvfb` and
read back with zbar:

| | ITF `12345678` | ITF `00012345678905` | Code 128 `A1234567` |
| --- | --- | --- | --- |
| decoded from the full 240×240 screen | **`I2/5:12345678`** | **`I2/5:00012345678905`** | no |
| decoded from the bars band alone | yes | yes | **`CODE-128:A1234567`** |
| grey levels in the band | **none — 0 and 255 only** | **none** | 0, 85, 170, 255 |
| ink outside the lit circle | **0** | **0** | **0** |

The grey row is the whole-pixel decision, measured: the ITF screens contain no
anti-aliased pixel anywhere in the band, and the Code 128 screen contains both
intermediate levels the panel can make.

The first row is worth reading carefully and **not** as "Code 128 is broken".
Both symbols decode. ITF decodes from the whole screen as captured, black
surround and id text included; Code 128 needs the band cropped out first. The
likeliest reason is the quiet zone — ITF's is at least ten elements by
construction, Code 128's is a fixed 10 px which is about five modules at
parkrun length, and the suite has characterised that shortfall since the tests
landed. It is a difference in margin, not a defect, and it is one more argument
for the layout rules above.

**What none of this says: nobody has pointed a scanner at this glass.** The QR
work got as far as a phone camera on the panel; ITF has not had that, and a
laser reading a reflective LCD through a front polariser is exactly the thing
arithmetic and framebuffers cannot settle.
