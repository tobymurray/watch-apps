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
| A current card with a settled night | `Night 23:47-07:05` / `7h02 at rest` |
| A current card with a night that could still grow | `Night 23:47-07:05` / `7h02 so far` |
| A current card whose night was a watch on a table | `No night` / `watch was off`, in grey |
| A current card with no night, or one 20 h old | `No night yet` / `wear it to bed`, in grey |
| A card whose heartbeat is more than 11 min old | `Slumber stopped` / `last seen 1h05 ago`, in amber |
| No card, no backup and no temp file | `Slumber` / `open Slumber once` |
| Files exist but none opens or decodes | The last good card, or `one moment` if there was none |
| A watch with no time set | `waiting for clock`, and never "stopped" |

`Software/Libs/Header/CardView.hpp` holds those rules, without the SDK, and
`Tests/` checks them on the host.
