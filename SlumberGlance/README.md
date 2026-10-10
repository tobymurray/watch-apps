# SlumberGlance — Slumber's card in the glance carousel

A `Glance` app, with no autostart and no GUI. It reads the record
[Slumber](../Slumber) publishes to `../SharedData/slumber/card` and shows it as
two lines. It never writes anything. Slumber's README explains why the card is a
separate app.

The carousel starts this service when the card scrolls into view, and stops it
when the card leaves. So the card is read once on arrival, then again every 30
ticks while it stays on screen.

| What it read | What the card says |
| --- | --- |
| A current card | `Slumber up 3h12` / `beat 2m ago  #7` |
| A card whose heartbeat is more than 11 min old | `Slumber stopped` / `last seen 1h05 ago`, in amber |
| No card, no backup and no temp file | `Slumber` / `open Slumber once` |
| Files exist but none opens or decodes | The last good card, or `busy, retrying` if there was none |
| A watch with no time set | `no clock  #7`, and never "stopped" |

`Software/Libs/Header/CardView.hpp` holds those rules, without the SDK, and
`Tests/` checks them on the host.
