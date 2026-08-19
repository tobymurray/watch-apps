# 2026-08-19 — the lighting series

The card suite photographed under three lighting conditions spanning 9 EV, to
ask Gate D's question: does the palette-first cartography hold up on glass when
the light changes. It does not — R5 fails — and the sun set shows that **casing
the trace fixes it**.

| | |
| --- | --- |
| Build | `1.1.0` — 20 cards, reference patches, caption on the bottom arc |
| Sets | [`indoor/`](indoor) · [`overcast/`](overcast) — 20 cards, build 1.1.0 |
| | [`sun/`](sun) 20:12–20:13 UTC — 24 cards, build 1.2.0 |
| Camera | Pixel, **manual white balance** (EXIF `WhiteBalance: 1`) held across all three |

## How far apart the two conditions actually are

Taken from EXIF rather than guessed, and cross-checked two ways:

| | Indoor | Overcast | Sun |
| --- | --- | --- | --- |
| ISO | 2974 | 31 | 25–31 |
| Exposure | 1/120 s | ~1/171 s | ~1/558 s |
| APEX brightness | −1.46 | +5.64 | **+7.53** (mean) |

Each step is cross-checked two ways. Indoor→overcast: **7.09 EV** by ISO and
shutter, **7.10 EV** by brightness. Overcast→sun: **1.90 EV** by ISO and
shutter, **1.89 EV** by brightness. So:

- overcast is **~137×** the indoor room
- sun is **~3.7×** overcast, and **~510×** indoor

The three sets span **9.0 EV**. Note the sun set was taken at 16:13 local in
late August — afternoon sun, not noon, so the bright end is not fully explored
and the real ceiling is somewhat above this.

**Exposure discipline improved across the series.** Indoors it alternated a full
stop between adjacent frames; overcast drifted ~0.4 stop; the sun set holds to
**0.22 EV across all 24**. The sun set is the one that can be compared frame to
frame without correction.

**Exposure was not locked.** Indoors it alternated 1/60 and 1/120 — a full stop
between adjacent frames. Outdoors it drifted over about 0.4 stop. White balance
was manual in both, which is the variable that mattered most, but a locked
exposure would make the sets directly comparable without correction.

## R5 fails, and the palette says why

This is the session's result. Rule R5 — the trace must win against every
basemap colour — **does not hold against the two road slots**, and the reason
is arithmetic rather than taste:

| Slot | Code | Channels | Model L* |
| --- | --- | --- | --- |
| `trace` | `0xC3` | r3 g0 b0 | 51.76 |
| `road_minor` | `0xC1` | r1 g0 b0 | 36.58 |
| `road_major` | `0xC0` | r0 g0 b0 | 23.67 |

All three sit on **one axis**: green and blue are zero in every one of them, and
only the red level changes. They are not merely a similar hue, they are the
*same* hue. The trace is separated from the roads by **lightness alone, with no
chroma difference whatsoever** — and lightness is the first thing a reflective
panel loses as ambient light rises and the cover glass starts returning the sky.

Card 17 shows it directly. The trace crossing `road_minor` is marginal indoors,
close to gone under overcast, and gone in sun. Compare `path` (`0xD0`,
r0 g0 b1), a cool ink at the *same* modelled L* as `road_minor`: the trace
stays legible over it in every condition, because there the separation is hue,
not lightness.

Note that card 17 also draws a band of `trace` itself, where the trace is
invisible by construction. That band is not evidence of anything.

## Casing fixes it — confirmed in sun

Cards 21–24 draw the slot bands crossed twice in one frame: the trace as the
spec draws it today, and the same ink with a `paper` casing under it. One frame,
so the comparison cannot be an artefact of two exposures.

**In direct sun the uncased line disappears into the warm bands and the cased
line stays followable across every slot.** The pale outline gives the trace a
boundary that does not depend on the colour underneath, which is precisely what
R5 asks for and what a lightness-only separation cannot deliver.

Card 22 (`cased night`) is the strongest of the four. On the night ground the
uncased trace is nearly indistinguishable from the dark maroon bands, while the
cased line reads cleanly from top to bottom — and it also vindicates drawing the
casing **after** the variant LUT. `paper` is what a restyle remaps hardest; in
`night` it becomes dark, so a casing applied before the LUT would have put a
dark halo on a dark ground and rescued nothing. The pale outline visible in
card 22 is only there because the casing is applied last.

Since cards 18–20 draw the trace before the LUT and 22–24 draw it after, the
pair also settles a question nobody had asked: **the app-drawn trace should be
drawn over the restyled basemap, not restyled with it.**

The change costs the trace one pixel of width on each side, needs no new code
on the render path, and does not spend a palette slot.

## What else the two sets say

- **The halo works, and it is doing real work.** Card 5's glyphs are `road_major`
  ink with a `paper` outline; the outline is what keeps the caption readable
  where it crosses bands, in both conditions. On build 1.0.0 the text was drawn
  plain white and this question was never actually being asked.
- **Card 8's mosaic reads as too dense in both conditions**, which is the
  finding the first session pointed at and the reason the card was changed to
  fill the panel.
- **Overcast lowers contrast across the whole panel**, as expected for a
  reflective display under a bright diffuse sky. Part of what the overcast
  frames show is sky glare on the cover glass rather than the panel's own ink
  range; direct sun will separate those, because it adds a directional specular
  component the diffuse case does not have.

## Reading these photographs

The suite's instrument is the person holding the watch; these are the record.
Two limits to keep in mind:

- **The reference patches were weaker than intended in these two sets.** They
  sat at x<16 and x>224, hard against the bezel's shadow on a *round* panel:
  legible by eye, but automated registration could not land on them, so no
  numeric normalisation was done on these frames. **Fixed in build 1.2.0** —
  24×30 strips at x 16..40 and x 200..224, with every card's subject pulled
  inside x 44..196 to make room. The sun set will be normalisable; these two
  sets stay as they are, and are compared by eye.
- **The second caption line truncates** ("does the halo sa…"), a consequence of
  narrowing that line to a 156 px chord. Cosmetic: the card's identity is on the
  line above and every card's subject is unaffected.
