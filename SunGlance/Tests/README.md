# Host tests

68 tests, three executables, and the split between them is about what each one
is *evidence* about rather than about what it covers.

```sh
export UNA_SDK=/path/to/una-sdk
cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)
```

| Executable | Needs | What a pass means |
| --- | --- | --- |
| `sunglance-pure-tests` | GoogleTest | The astronomy, the two-event policy, the coordinate parser and the wording are right. No SDK, no kernel, no filesystem. |
| `sunglance-config-tests` | + kernel doubles, coreJSON | A config file on the watch's storage becomes the position the app acts on, or a stated reason it does not. |
| `sunglance-glance-tests` | + the real `Service` | The parts are joined up, and the strings actually reach the kernel. |

## Where the sunrise numbers come from

`Solar_test.cpp` checks this code against
[`astral`](https://pypi.org/project/astral/) 3.2 — an independent Python
implementation of the same NOAA calculation, written by other people.
[`fixtures/astral_reference.py`](fixtures/astral_reference.py) regenerates the
table and explains the two things that make the comparison meaningful (a
solar-aligned zone per place, and both events inside the same local day).

**The observed disagreement, over 107 place-days:**

| Latitude | Worst disagreement | Tolerance asserted |
| --- | --- | --- |
| below 55° | 26 s | 45 s |
| 55–66° | 72 s | 90 s |
| beyond 66° | 118 s | 180 s |

The widening is geometry, not sloppiness. Near the poles the sun approaches the
horizon at a shallow angle, so a hundredth of a degree of disagreement about
where it is becomes minutes of disagreement about when it gets there. Asserting
a tighter bound up there would be asserting something that is not true.

**Polar days.** A year-long sweep of five polar sites — 1825 place-days,
`--sweep` in the generator — put the two implementations on the same verdict
every day but thirteen. Ten were single transition days, where the sun's
greatest altitude is within a hundredth of a degree of the −0.833° that defines
sunrise and neither answer is a fact about the sky. The other three are astral's
own interval search failing to find a sunrise that plainly exists, sandwiched
between two ordinary days. The polar fixtures therefore stay well inside their
polar periods, and the transition days are documented in the test file rather
than asserted.

## What the fixtures are not evidence about

**Not that anything renders.** No test here has seen a panel. The layout bands
come from SleepLab's, which came off a real watch; the character budgets in
`Render_test.cpp` are an estimate of what fits at these font sizes and are
explicitly marked as unverified. A caption one character too wide for the panel,
an icon that collides with the time next to it, or a five-control glance a
kernel will not grant all pass every test in this directory.

**Not that the watch's clock or time zone is right.** Everything is computed
from what the platform says the time is. If the RTC is wrong the glance is
wrong, and the only defence in the app is the `clock not set` state and the
longitude-versus-zone check.

**Not that `astral` is correct.** It is a second opinion, not an almanac. Both
implementations descend from the same NOAA method, so they share its
assumptions — sea level, standard refraction, no terrain. A mountain to your
east delays sunrise by more than every disagreement in the table above.

## The glance harness

[`GlanceHarness.hpp`](GlanceHarness.hpp) is the file worth reading before
changing `Service.cpp`. It scripts the message queue a glance carousel would
send — start, ticks, stop — answers the service's request for the glance area,
and captures every `RequestGlanceUpdate` with the text and colour of each
control. `Service` runs unmodified and does not know the harness exists.

It is here because of what happened to SleepLab, whose glance sent nothing at
all for weeks while every part of it built and passed its own tests. The bug
lived in the joins: which call invalidates a control, which call marks the form
clean, and which of those the tick actually reaches. The two tests that would
have caught it are `SendsBothUpcomingEventsOnce` and
`TheMinuteTickingOverSendsExactlyOneMoreUpdate` — one says something is sent,
the other says it is not sent sixty times a second.

It also captures the images and the visibility flags, not only the text, which
is what makes `DuringTheDayTheIconsSwapRound` possible: an icon on the wrong row
is invisible in the code and obvious on the wrist. The icons are compared by
**content**, because the generated header declares them `static const` and each
translation unit gets its own copy — the pointers legitimately differ, and
Service.cpp is the only file in the app that includes it.

One consequence of `Control::setVisible()` is worth knowing before editing the
service: **it does not invalidate**. It writes the flag and returns, so a
control that is hidden and nothing else would keep being drawn until something
unrelated dirtied the form. `Service::showControl()` invalidates explicitly, and
the hidden-row assertions here are what would catch it going missing.

The app's one concession to being testable is `Sun::setWallClockSource()`, and
it buys the whole exercise: the interesting moments on this screen are the
minute before sunrise and the minute after sunset, which is not when anybody
runs a test suite. `TZ=UTC` is set on that suite by CMake rather than assumed,
because every assertion in it is a local clock reading.
