# 2026-08-24 — how Garmin and Coros solved this

Every number in this repository so far is a measurement of *this* device. This
one is not. It is a survey of what two vendors shipping wrist maps on
comparable hardware actually do, and it is recorded because it corroborates
three findings from independent directions and **challenges the premise of a
fourth**.

**Evidence class.** Everything below is third-party text — reverse-engineered
format documentation, vendor manuals, and user forum reports. Under this
repository's convention that is weaker than CONFIRMED, which means traced to
code, text or an experiment *on this device*. Nothing here should be treated as
a measurement. It is prior art, and its job is to tell us which of our own
questions were worth asking.

## Garmin: pre-generalise into fixed levels, each with its own precision

Garmin's `.img` format is not documented by Garmin; what is known comes from the
mkgmap/OpenStreetMap community's reverse engineering. Two properties matter
here:

- A map carries several **levels**, and each level is built at a **resolution**
  from 1 to 24, **each step down being half as detailed**. Coordinates are at
  most 24-bit, and **fewer bits are used at more zoomed-out levels**.
- Features are assigned to levels **at compile time** — a town is a named dot at
  one level and an outline at the next.

Those are the same two levers
[`2026-08-19-format-ceiling`](../2026-08-19-format-ceiling) arrived at from
measurement: quantise coordinates to what the zoom can actually show, and
generalise when packing rather than when rendering. Garmin has shipped that
arrangement since its handheld GPS era.

mkgmap also carries special-case code to stop small objects such as buildings
degrading into triangles when their coordinates are rounded — which is the
topology-preservation caveat that investigation flagged against its own
per-part Douglas–Peucker.

## Nobody hits 100 ms

The pivot's charter sets a 100 ms redraw budget: "a redraw that misses a frame
is fine, one that misses a second is not."

On a Fenix, **"zoomed in, map redraws are fast, but zoomed out, they slow down
considerably,"** and users report maps that **"take many seconds to redraw"**, or
that fail to render until panned back and forth. This is a long-standing,
widely-reported characteristic rather than a bug with a fix.

**So a shipping product on comparable hardware misses that budget by an order of
magnitude, and ships anyway.** That does not make our 479.9 ms acceptable, and
it is emphatically not a reason to move a bar to meet a measurement. But it does
mean Gate C is currently being judged against a threshold that **no surveyed
competitor meets**, and the threshold's provenance is one sentence of charter
prose rather than anything measured. Whether 100 ms is the right bar is now an
open question in its own right, separate from whether the renderer clears it.

## Both vendors drop buildings, independently

[`2026-08-19-format-ceiling`](../2026-08-19-format-ceiling) measured buildings at
**90.0 ms of a 100 ms budget** — 90% of the frame, for a slot the cartography
spec itself calls "context only". Both vendors already made that call:

- **Garmin** ships a map-detail setting; users describe the higher settings as
  showing **"more POIs, buildings etc that you do not normally need for
  navigation"** and recommend leaving it on normal.
- **Coros** Landscape maps carry **streets, major road networks, natural
  features and waterways** — buildings are not in the product, and even road
  detail is reported as sparse.

Two vendors and one measurement independently reaching "a wrist map is roads,
water and landcover, not buildings" is about as much corroboration as a
cartographic judgement of this kind is going to get.

Coros is the sharper case: its answer to the whole problem is to **ship less
data**, not to render faster.

## The constraint they hit is memory, not geometry

Garmin's map slowness is attributed to memory rather than to rasterising: the
Fenix SoC has **5 MB of operating memory**, so it **"cannot cache map data in
memory and constantly has to pull data from the storage, which is relatively
slow."** Suunto, with roughly 37 MB on a similar processor, is reported as
substantially faster.

[Gate B](../../GATES.md) establishes about **93 KB** for a renderer in this
project. The arenas are not equivalent — Garmin's 5 MB is a whole-SoC figure for
a far richer application, ours is a per-app allocation — but the order of
magnitude is worth sitting with: **this device has roughly fifty times less room
than the one whose users already complain the map is too slow**, and the
diagnosis there is caching, not drawing.

That promotes bench **I07** — 638 µs per seek, the per-(tile, layer) cost of the
layer directory — from an interesting number to the one most likely to decide
the design. A 2×2 viewport with six layers is 24 seeks, ~15 ms, before a pixel
is drawn, and unlike Garmin we have no room to cache our way out.

## What this does and does not change

- **Corroborated:** quantise-to-zoom and pack-time generalisation are the right
  levers; dropping buildings is a normal cartographic decision, not a
  capitulation; storage latency rather than rasterising is where wrist maps
  actually die.
- **Challenged:** the 100 ms budget. Gate C's verdict stands as measured — 479.9
  ms at real density — but the bar it fails against is now known to be stricter
  than shipping practice, and nothing has ever justified it beyond one line of
  prose.
- **Unchanged:** every measurement in this repository. None of this is evidence
  about this device.

## Limits of this survey

- Garmin's format is described from reverse engineering, not vendor
  documentation. It may be wrong or outdated in detail.
- No hard redraw timings were found for any competitor — only qualitative
  "seconds". A measured competitor number would be worth far more than this
  whole section.
- Coros's format is entirely opaque; only the shipped product's content is
  visible.
- Forum posts are user reports, including the 5 MB figure, which is stated by a
  forum participant rather than by Garmin.

## Sources

- mkgmap manual — <https://manpages.ubuntu.com/manpages/trusty/man1/mkgmap.1.html>
- Guide to mkgmap style files — <https://www.cferrero.net/maps/guide_to_mkgmap_style_files.html>
- Mkgmap, precision of points in polygons — <https://community.openstreetmap.org/t/mkgmap-precision-of-points-in-polygons/122620>
- Map Rendering Issues, fēnix 7 forum — <https://forums.garmin.com/outdoor-recreation/outdoor-recreation/f/fenix-7-series/360617/map-rendering-issues>
- Which layers affect draw speed, Enduro 3 forum — <https://forums.garmin.com/outdoor-recreation/outdoor-recreation/f/enduro-3/389273/is-there-a-way-to-speed-up-map-rendering-which-layers-affect-draw-speed>
- fēnix 7 Owner's Manual, Map Settings — <https://www8.garmin.com/manuals/webhelp/GUID-C001C335-A8EC-4A41-AB0E-BAC434259F92/EN-US/GUID-60C3B7A5-51ED-4E4D-A2DC-8578234EF279.html>
- COROS Topo & Landscape maps — <https://the5krunner.com/2021/11/09/coros-starts-to-rollout-topo-maps-landscape-base-maps-galileo-gnss/>
- Downloading Maps to Your COROS Watch — <https://support.coros.com/hc/en-us/articles/4405711354900-Downloading-Maps-to-Your-COROS-Watch>
