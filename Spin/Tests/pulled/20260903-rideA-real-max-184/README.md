# Ride A, pulled 2026-09-04

The recording behind
[`Docs/RECOVERY-FIELD-RESULTS.md`](../../../Docs/RECOVERY-FIELD-RESULTS.md#2026-09-03--ride-a-real-maximum-184),
kept here because until it was pulled it existed only on the watch — and the
heart-rate trend experiment's whole comparison is against it.

| File | What it is |
|---|---|
| `activity_20260903T205637.fit` | the ride, one `record` a second while the clock ran |
| `activity_20260903T205637.json` | the summary the watch's activity list reads |
| `recovery.log` | every session on the watch up to 2026-09-04 00:20, not just this one |
| `spin_sessions.json` | the shared log as it stood, `kept: 7` |
| `watch_settings.json` | the watch's own settings **as pulled**, which is not what this ride ran against |

**`watch_settings.json` says `heartRateZones: [30,60,70,80,90,100]`.** That is
the synthetic maximum the later desk sessions were run at, left in place
afterwards. Ride A ran at **184**, which its own `start` line in `recovery.log`
records. The settings file is the watch's state on 2026-09-04, not this ride's.

`recovery.log` covers every session, real and synthetic. `hr_max_setting` in
`spin_sessions.json` is the only thing separating them; read
[the maximum-heart-rate table](../../../Docs/RECOVERY-FIELD-RESULTS.md#which-maximum-each-recorded-session-ran-against)
before treating any number here as physiology.

```sh
Tools/hr_trend.py Spin/Tests/pulled/20260903-rideA-real-max-184/activity_20260903T205637.fit \
    --max-hr 184 --zones 5
```
