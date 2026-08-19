# 2026-08-19 — the lighting series

The card suite photographed under two lighting conditions on one build, to ask
Gate D's question: does the palette-first cartography hold up on glass when the
light changes. A third set, **direct sun, is still outstanding** — it is the
brightest case and the one the spec's ink-range argument is really about.

| | |
| --- | --- |
| Build | `1.1.0` — 20 cards, reference patches, caption on the bottom arc |
| Sets | [`indoor/`](indoor) 14:29–14:31 UTC · [`overcast/`](overcast) 14:37–14:39 UTC |
| Camera | Pixel, **manual white balance** (EXIF `WhiteBalance: 1`) held across both sets |
| Not yet taken | direct sun |

## How far apart the two conditions actually are

Taken from EXIF rather than guessed, and cross-checked two ways:

| | Indoor | Overcast |
| --- | --- | --- |
| ISO | 2974 | 31 |
| Exposure | 1/120 s | ~1/171 s |
| APEX brightness | −1.46 | +5.64 |

ISO and shutter together give **7.09 EV**; the brightness values differ by
**7.10 EV**. Two independent derivations agreeing to 0.01 EV, so:
**overcast is ~137× brighter than the indoor room.** That is the span these two
sets bracket, and direct sun is roughly another 2–3 EV beyond it.

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

Card 17 shows it directly. The trace crossing `road_minor` is marginal indoors
and close to gone under overcast. Compare `path` (`0xD0`, r0 g0 b1), a cool ink
at the *same* modelled L* as `road_minor`: the trace stays legible over it in
both conditions, because there the separation is hue, not lightness.

Note that card 17 also draws a band of `trace` itself, where the trace is
invisible by construction. That band is not evidence of anything.

**The fix the spec already owns.** Casing. `road_major` is drawn cased — a
`paper` halo under the ink — and the line-weights card shows casing surviving
both conditions. A cased trace wins against *any* basemap colour regardless of
hue or lightness, which is exactly what R5 demands, and it needs no new code
and no palette change. It costs the trace one pixel of width on each side.

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
