# 2026-08-19 — the first hardware session

The session that took MapLab off the model and onto the glass. Five runs, the
twelve cards photographed, and the watchdog ladder climbed to its top step.
Verdicts live in [`../../GATES.md`](../../GATES.md); this directory is the
evidence they cite.

| | |
| --- | --- |
| Build | `1.0.0`, SDK `apps-v1.4.0` (`KERNEL_INTERFACE_VERSION 3`) |
| Device | UNA Watch, firmware 1.4 line |
| Runs | 56, 151 (full suite) · 942 (partial) · 246, 946 (watchdog) |
| Log | [`maplab_log.csv`](maplab_log.csv) — 66 `B` rows, 56 valid, **0 `INCOMPLETE`** |
| Cards | [`cards/`](cards) — indoors, backlight off, well-lit interior room. Camera originals, 3000×4000 q95, panel-cropped without resampling |

## What the log settles

Read it with `python3 ../../../Tools/maplab_report.py maplab_log.csv --all`.

**Gate C fails at city density and passes below it.** Rural 24.0 ms, suburban
70.2 ms, city centre 160.5 ms against a 100 ms budget. Runs 56 and 151 are
independent passes agreeing to within 0.3%; run 942 is a partial third pass
that corroborates R01–R03 to within 0.4%.

**The wire format is not the lever.** R05 puts decode+transform at 3.4% of the
city render. A perfect zero-cost format cannot close a 1.6× gap; only a faster
rasteriser or ~38% fewer points can.

**The watchdog is not the constraint.** Every step of the ladder survived,
including 16 s (run 946). Uptime climbs monotonically across all five `R` rows
and never resets, so the device was never rebooted at any point.

**The host is a usable proxy.** Host:device is ~240× constant to within 0.5%
across a 6× density range.

## What the cards do not settle

The card suite asks to be read **indoors and in sunlight**. Only the indoor
half was taken, and it is the daylight half that the palette's whole argument
about ink range depends on. Gate D stays UNVERIFIED.

The images here are the **camera originals**, 3000×4000 at quality 95, pulled
off the phone over `adb` and cropped to the panel at native resolution — no
resampling, roughly 8 photo pixels per panel pixel. An earlier version of this
directory held copies that had been through Signal at 1024 px and quality 75;
those lost about 9/10 of the pixels and are not what is stored here.

Two limits remain, both the phone's rather than the transfer's:

- **JPEG 4:2:0 chroma subsampling** — colour resolution halved in both axes.
- **Auto white balance** (EXIF `WhiteBalance: 0`), so every frame carries a
  different colour transform.

The consequence is specific: readings *within* one frame are sound, comparisons
*between* frames are not, and these cannot be compared against a future
daylight set at all. Cards 3, 4 and 6–8 are geometry and are unaffected.

**For the daylight session, lock white balance and exposure and turn off any
HDR or scene enhancement**, and shoot the indoor set again in the same
configuration — otherwise the two lighting conditions differ by camera settings
as well as by light, and nothing can be attributed to either.

Per-card readings are tabulated in [`../../GATES.md`](../../GATES.md#gate-d--the-indoor-half-2026-08-19).
The three findings worth carrying:

1. **R5 is weakest in the day variants.** The trace red and the road maroon
   share a hue family, separated mostly by lightness — the axis a reflective
   panel loses first in bright light. Check this first in sunlight.
2. **Card 8 says generalisation is under-aggressive at coarse zoom.** That is
   the same lever Gate C needs pulled, so one change may serve both gates.
3. **Card 4 sets a floor on dash design.** The finest cycle reads as a solid
   line at 2 px.

## The suite has changed since this session

These twelve are the suite as it stood on 2026-08-19. It was extended to twenty
afterwards, **because of** what these photographs showed, and three defects
visible in them were fixed at the same time — the caption sat across the middle
of the panel, the half-scale card used a quarter of it, and the text cards drew
no halo despite asking about one. See
[`../../GATES.md`](../../GATES.md#what-changed-in-the-suite-because-of-this-session).

So these frames are not directly comparable with a later set, and are kept as
what they are: the indoor half of the first session, on build `1.0.0`.

## How the cards were identified

Filenames follow the canonical order in `Software/Libs/Header/Cards.hpp`
(`enum class Card`), which is also the order the app steps through them.

The twelve originals are a single unbroken exposure sequence, 13:22:35 to
13:24:06 UTC, one shot per card in order. That sequence is confirmed at seven
of its twelve positions:

- **Positions 1, 2, 5** — caption read directly off the frame and matching
  `cardQuestion()` verbatim ("one quantum = one step?", "which bands vanish?",
  "does the halo save it").
- **Positions 3, 4, 8, 9** — unmistakable by subject: the line-weight ladder,
  the dash ladder, a half-scale render occupying the middle of the panel, an
  inverted night ground.

Seven confirmations spread across positions 1–9, with no position out of
order, fix the mapping for the remaining five (6, 7, 10, 11, 12) by induction.
Their `N/12` captions are drawn over the map itself and stay illegible even at
full resolution, so induction rather than reading is what places them — but
each also matches its enum description by subject, which is an independent
check that agrees.
