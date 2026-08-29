# Host tests

Two executables, and the split between them is about what each one is *evidence*
about rather than about what it covers.

```sh
export UNA_SDK=/path/to/una-sdk
cd Barcode/Tests
cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)
```

| Executable | Needs | What a pass means |
| --- | --- | --- |
| `barcode-pure-tests` | GoogleTest | The Code 128 encoder and its 107-row spec table are self-consistent and match the published constants, and the geometry the barcode is drawn with is honest about a round, four-level panel. No SDK, no kernel, no filesystem. |
| `barcode-service-tests` | + kernel doubles, coreJSON, `SDK::AppConfig` | A file somebody else wrote becomes the id the GUI draws, or a stated reason it does not — and the state actually leaves the service. |

## The table is the load-bearing part

Before this suite, the whole of "this barcode is the right barcode" was a
comment in `Code128.hpp` saying the 107-row pattern table had been
*cross-checked against the published reference table on 2026-07-25* — once, by
eye, by one person. A transposed row there is invisible: the app starts, bars
fill the screen, they scan, and they decode to somebody else's athlete number.
That is the exact harm [the README](../README.md) says this app must never do.

The table is held to the symbology's own structure:

| Claim | What it catches |
| --- | --- |
| 107 rows, six elements each, every element 1–4 | Truncation, a stray digit |
| Every row sums to 11 modules | Most single-element slips |
| **Bars sum even, spaces sum odd, every row** | Code 128's self-checking property. Catches most slips that still sum to 11 — the ones the row-sum test lets through |
| All 107 rows distinct | A duplicate is an ambiguous decode |
| Start A/B/C and Stop match their published patterns | The table being shifted or misaligned anywhere |

Plus a golden vector for `"A1"` written out width by width, and the checksum
arithmetic for a parkrun-shaped `"A1234567"` worked by hand in the test.

Structure alone is the table checking itself, though, and so is the round trip
below — it shares the table it is testing. A self-consistently wrong table
survives both. So there is also an oracle.

**An independent implementation.** [`oracle/generate.cpp`](oracle/generate.cpp)
records what [zint](https://zint.org.uk) — separate implementation, separate
table, separate subset selection — produces for a corpus of ids, and
`oracle/zint_vectors.hpp` commits the module patterns as data so the tests need
no zint at runtime. `ZintOracle` asserts that **every vector matches, module for
module**: not the same length, the same bars. The corpus is chosen for the things
that decide a Code 128 encoding — the ends of the printable range, digit runs of
every parity at the start, middle and end, and the ids this app exists for.

That is what makes it evidence about the subset switching in particular. The
switching was added after these vectors were recorded, and `A1234567` — a letter
then an odd run of seven digits, where an encoder has to decide whether to pair
from the first digit or the second — comes out identical to zint's 101 modules.
So do `1234AB5678`, `AB123456CD` and `999999999999`.

Regenerate when the corpus changes:

```sh
apt-get install -y libzint-dev
cd oracle && g++ -std=c++17 -o generate generate.cpp -lzint && ./generate > zint_vectors.hpp
```

The version is recorded in the generated file, because a change in zint's subset
policy would otherwise look like a change in ours.

**A round trip.** `Code128RoundTrip` decodes the encoder's output back to text
the way a scanner has to — following the start character and the switch symbols,
recomputing the check from the stream — and asserts it equals the id. It shares
the table, so it cannot prove the table right; what it catches is the encoder's
bookkeeping, which is where subset switching goes wrong. Every id in its corpus,
every vector in the oracle's, and every one of the 95 printable characters on its
own, so a single wrong row cannot hide behind a corpus.

Agreeing with zint and decoding to the id are separate claims — two
implementations could in principle share a misreading — which is why both are
asserted over the same corpus.

None of this is proof the table is correct. It is a great deal more than a
comment.

## What the geometry tests are accountable to

Two facts about the panel decide whether this app works, and the app does not
get a vote on either. Both are now in `BarcodeLayout.hpp` where a test can
reach them, rather than as literals in a TouchGFX view constructor.

**It is round.** 240×240 is addressable; only the inscribed circle is lit. Two
consequences the tests pin:

- The widest row the model treats as lit is **238 px, not 240**, and column 0 is
  dark everywhere. That is a conservative choice rather than a measurement: the
  boundary sits at 119.5 dot pitches, half a pitch inside the datasheet's
  ⌀30.24 mm active area, which is exactly 240 pitches. A pixel counts as lit
  only if its centre is inside; the outermost pixel of the centre row has its
  centre inside the circle but part of its area outside, and nothing available
  here says whether it appears lit. The bezel overlaps the glass besides. So
  "240 wide" is still the wrong number to lay anything out against — but 238 is
  a safety margin, not a hardware fact. Nothing depends on the difference: the
  bars, the id and the marks put zero ink outside a 119.5- *or* a 120-pitch
  mask.
- The white backing **fits whole**, corners included, and did not always. At
  220×110 its four corners sat outside the lit area and the display cut the tips
  off; 0.3.1 shrank the height to 94 — `2*sqrt(r² - halfWidth²)` at 220 wide —
  without shrinking the quiet zone with it. `NoBackingRowIsCut` and
  `TheWhiteBackingFitsEntirelyInsideTheLitCircle` hold it there, and
  `EveryBarRowHasTheFullBackingWidthLit` is the one that would matter again if
  the backing ever grew back.
- The **id beneath the bars** is the same problem one row down, and it was got
  wrong twice — a 15-character id put 20 pixels of ink outside the lit circle
  and a 16-character one 64 — because the simulator draws the full square and
  only the watch has a bezel. `IdRow` and `PagerMarks` cover what can be
  asserted without knowing font metrics; the note in `Layout_test.cpp` says
  what deliberately is not.

**It has four levels a channel.** 8bpp ABGR2222 through `LCD8bpp_ABGR2222`,
software rendered. Anti-aliasing a bar edge has four greys to work with, so a
boundary landing mid-pixel steps rather than blends. This is a fact about the
glass and not only about the framebuffer: the panel is a Sharp **LS012B7DD06**
(the `UNAview_LS012` board in [UNAWatch/una-hardware](https://github.com/UNAWatch/una-hardware)),
whose technical literature — spec LD-29652B — states "1 pixel has RGB each 2bit,
the pixel can display 64 colors".

**And it is physically small.** Table 3-1 of that same document gives a **0.126 mm
dot pitch** and a **⌀30.24 mm active area** for 240×240 dots, which is what turns
a module width in pixels into one a scanner cares about. The `XDimension` tests
are where that lives — and they compare against scanner *capability*, not against
a standard, because ISO/IEC 15417 does not set a minimum X-dimension: its scope
leaves it among "the parameters to be defined by applications". Everything up to
14 characters clears the 5 mil mid-density line; every length the app accepts
clears the 3 mil on an LS2208's spec sheet.

Tests are labelled by what a failure means:

- **Invariants** — true now, must stay true. A failure is a regression.
- **Characterisations** — true now and *should not be*. Named `CurrentlyX`,
  carrying the standard they fall short of. A failure means somebody improved
  it, and the test should be tightened.

### What the characterisations currently say

Alphabetic ids, which subset C cannot help — one symbol per character:

| Length | Modules | Module | Quiet zone | |
| --- | --- | --- | --- | --- |
| 4 | 79 | 2.53 px | 3.95 modules | short |
| 8 | 123 | 1.62 px | 6.15 modules | short |
| 12 | 167 | 1.19 px | 8.35 modules | short |
| 15 | 200 | 1.00 px | 10 modules | whole pixels, one px wide |
| 16 | 211 | 0.94 px | 10.55 modules | sub-pixel module |

ISO/IEC 15417 wants ten modules of quiet zone. The layout gives a fixed **10
pixels**, which is short at every length anyone would use. It only "passes" at
15 and 16 characters, and not because the margin grew — because the module
shrank to a pixel or less. Measured in pixels the margin looks generous; that
is how it came to be one.

The 15-character row is the tension in a single line: it is the only length
where every module is a whole pixel and nothing is anti-aliased, and it is also
the narrowest a bar can be. The one length that renders cleanly is the one a
scanner has least chance of reading.

### Subset C, and the parity nobody expects

Numeric ids go through subset C, which packs a digit pair into one symbol — the
5,5-modules-per-numeric-character figure the standard gives, against 11 for a
Subset B character. Against the 127 µm (5 mil) mid-density line:

| Digits | Modules | X-dim | | Digits | Modules | X-dim |
| --- | --- | --- | --- | --- | --- | --- |
| 8 | 79 | 318 µm | | 9 | 101 | 249 µm |
| 10 | 90 | 280 µm | | 11 | 112 | 225 µm |
| 12 | 101 | 249 µm | | 13 | 123 | 204 µm |
| 14 | 112 | 225 µm | | 15 | 134 | **188 µm** |
| 16 | 123 | 204 µm | | | | |

**Every numeric length clears 5 mil**, including fifteen and sixteen digits —
the two lengths that fall just under it when they are letters. That is the
whole of what subset C bought, and `EveryNumericIdLengthClearsMidDensity` is
where it is claimed.

Read down the odd column, though, and something is wrong with the intuition:
**adding a digit to an odd-length numeric id makes the barcode narrower.** Nine
digits are 101 modules and ten are 90. Fifteen are 134 and sixteen are 123.

An odd count strands a digit that cannot be paired, and carrying it costs a
symbol of its own *plus* a switch back into subset B — two symbols to say what
a pair says in one. There is no encoding that avoids it; it is a property of
pairing, not an optimisation the encoder is missing. So fifteen digits is the
worst case in the whole numeric range, and sixteen digits are comfortably
better than fifteen.

A parkrun id — a letter and seven digits — is the shape subset C helps least,
since the letter and the odd leading digit both stay in subset B. 123 modules
become 101: 204 µm to 249 µm, about 8.0 mil to 9.8 mil. Worth a test of its
own because `AParkrunIdIsComfortablyAboveMidDensity` measures eight *letters*,
which is the pessimistic stand-in rather than the id people actually carry.

### ITF, and the two things the panel decided

ITF is the third format and the first that is drawn by different *rules* rather
than by a different encoder. Two of them, and the tests are what hold each one:

**Whole pixels.** The element is rounded down to a whole number of pixels and
the symbol centred in what that leaves, instead of being stretched to fill the
band the way Code 128 is. Four levels a channel is the reason — a mid-pixel
edge steps rather than blends — and the framebuffer capture below is what turns
that from an argument into a measurement.

**Bearer bars.** ITF is continuous and has no check character, so a scan that
clips the symbol can decode as a valid shorter number. A bar above and below
means such a scan crosses ink and fails instead.

`ItfPanel.TheQuietZoneMeetsTenElementsAtEveryLength` is the test worth knowing
about, because it caught the layout being wrong. Sizing the element from the
200 px bars band passes at 2, 8, 12, 14 and 16 digits and **fails at 4, 6 and
10** — the symbol grows into the white the quiet zone needed. The element has to
be sized from the 220 px backing with the margin in the budget.

| Digits | Element | X-dim | | Digits | Element | X-dim |
| --- | --- | --- | --- | --- | --- | --- |
| 2 | 4 px | 504 µm | | 10 | 1 px | 126 µm |
| 4 | 3 px | 378 µm | | 12 | 1 px | 126 µm |
| 6 | 2 px | 252 µm | | 14 | 1 px | 126 µm |
| 8 | 2 px | 252 µm | | 16 | 1 px | 126 µm |

The encoder is diffed against **zint** width for width over 24 vectors, the
same standard Code 128 and QR are held to. Even lengths only, and that is a
stated rule rather than a gap: zint pads an odd id with a leading zero and this
encoder refuses one, so there is no width to compare. The refusal is tested
directly instead, and `ZintItfOracle.TheCorpusIsEvenLengthOnlyByRule` stops the
rule quietly lapsing.

### What the framebuffer says about the two linear formats

Captured headless under `Xvfb` and read back with zbar, the same way QR was:

| | ITF `12345678` | ITF `00012345678905` | Code 128 `A1234567` |
| --- | --- | --- | --- |
| decoded from the full 240×240 screen | **exact** | **exact** | no |
| decoded from the bars band alone | yes | yes | **exact** |
| grey levels in the band | **0 and 255 only** | **0 and 255 only** | 0, 85, 170, 255 |
| ink outside the lit circle | **0** | **0** | **0** |

The grey row is the whole-pixel decision measured rather than argued: no pixel
of either ITF screen is anti-aliased, and the Code 128 screen carries both
intermediate levels the panel can make.

The first row is **not** "Code 128 is broken" — both decode. ITF decodes from
the whole screen as captured, black surround and id text included; Code 128
needs the band cropped first. The likeliest reason is the quiet zone, which
ITF guarantees at ten elements and Code 128 leaves at a fixed 10 px — about
five modules at parkrun length, a shortfall
`QuietZone.CurrentlyShortOfTheStandardAtEveryUsefulLength` has recorded since
this suite landed. A difference in margin, not a defect, and one more argument
for the layout rules above.

## The defect these tests found, and what became of it

**`"id": null` produced a scannable barcode reading `null`.** The old
`InputConfig::getString()` never checked the JSON *type* of a value, so a
non-string was accepted and its raw text became the id. A number was benign —
`"id": 1234567` gave the digits the user meant — but a null gave four letters
that scan. That is precisely the "plausible value that scans" the app's
[README](../README.md) says must never happen.

**It is fixed, by the migration rather than by a patch.** `InputConfig` was
deleted in `5aa310c` and `SDK::AppConfig` refuses a non-string outright, so
`null`, a number, a bool, an array and an object all now end as `NoValue` with
no code adopted. `ANonStringIdIsRefusedRatherThanCoerced` pins all five, because
the harm was specific and the promise is now the SDK's rather than this app's.

The one cost is that an unquoted number is refused where it once worked: writing
`1234567` without quotes gets "no codes yet" instead of a barcode. That is the
right way round, and it is in the test so it is a decision rather than a
surprise.

The other defect this suite recorded — that the first `refresh()` on a missing
file returned `false` without reading — went with the same deletion.
`SDK::AppConfig` reads its file once, in its constructor, and `Service::load()`
builds a fresh one to re-read.

## What these tests are not evidence about

**Not that anything renders — for the bars.** Nothing in the Code 128 half of
this suite has seen a panel. The geometry tests can say a rectangle's corners
are lit, that no bar row is cut, and what a module works out to in pixels — they
cannot say a scanner reads it. The scannability numbers above are derived from
geometry and the framebuffer format, not from putting a scanner in front of a
watch. `RustGuiPoc` dumps its framebuffer to `fb_dump.bin` on a long press; the
same trick here would give a real capture to point a scanner at, and for the
bars that is still the missing evidence.

**Not that ITF is a full ITF implementation.** No check digit is computed or
verified — ITF-14's mod-10 check is part of the *number*, so a card that has one
carries it in the digits and this draws what it is given. Nothing here knows
about ITF-6, GS1 application identifiers, or bearer-box conformance as opposed
to the top-and-bottom bearers it draws.

**Not that ITF has met a scanner either.** zbar reading a framebuffer is not a
laser reading a reflective LCD through a front polariser. QR got as far as a
phone camera on the glass; ITF has not.

**For QR, half of it is no longer missing.** A capture of the simulator's
framebuffer, masked and measured, is recorded in [Docs/QR.md](../Docs/QR.md):
every one of the 625 modules matches the encoder, no pixel is anti-aliased, no
ink falls outside the lit circle, and **zbar decodes the screen to exactly the
id that was configured**, at the real 4 px module. That is end to end through
the real renderer at the real geometry, which is more than this app has ever
been able to say.

And the last step is no longer missing either, though it is not something this
suite can run: **Google Lens reads the symbol clearly off the panel**, from the
0.4.0 build installed on a watch. Specular glare was the predicted failure mode
and it did not materialise. That is one reader in one set of conditions rather
than a characterisation of the scanning envelope, and it is recorded in
[Docs/QR.md](../Docs/QR.md) as exactly that -- but the question of whether
0.504 mm modules resolve on this glass is settled, and it is settled the only
way it could be, by pointing a camera at it rather than by arithmetic.

The bars have had no such confirmation. Nothing in this suite is evidence that a
*laser* scanner reads a Code 128 symbol off this panel, and that is the app's
older and larger open question.

**Not that a same-size rewrite is picked up.** `refresh()` compares
`(size, mtime)`, and the SDK's in-memory filesystem reports `utc = 0` for every
file — so a rewrite keeping the byte count identical is invisible to these
tests. That is not a corner case: athlete ids are fixed-width, so swapping
`A1234567` for `A7654321` *is* a same-size rewrite, and on the watch it rests
entirely on mtime. `SameSizeRewriteIsInvisibleToTheseTestsNotToTheWatch` pins
the blind spot so a passing suite is not mistaken for coverage of it. Closing
it needs a settable mtime in `Tests/Host/support/FakeFileSystem`.

**Not that the table is right.** See above — the structural claims are strong
and they are not an oracle. Since `a119f9c` there *is* an oracle, and it is
where the confidence in both encoders actually comes from.

**Not that the QR encoder is a general one, or that its mask choice is
canonical.** Version 2 at level M in byte mode, and nothing else: no version
selection, no interleaving, no mode selection, so there are no tests for any of
them. And which of the eight masks gets picked is judgement rather than
correctness — every mask decodes, because the mask is recorded in the format
information — which is why `ZintQrOracle` diffs a **forced** mask across all
eight rather than comparing two penalty scores and calling a legal difference a
bug. `QrMask.TheChosenMaskScoresNoWorseThanAnyOther` holds the selector to its
own definition and nothing more.

**Not that every payload is zint-diffed.** This encoder emits a single byte-mode
segment always; zint optimises across modes and splits `a1234567` into a byte
`a` and numeric `1234567`. Both are legal QR and they are different grids, so
the oracle corpus is restricted to payloads where one byte segment is optimal —
which excludes the parkrun shapes, because they are pure alphanumerics. Those
are covered instead by rendering this encoder's own output and decoding it back
with zbar, offline, beside the generator. All 23 payloads in that run decode to
themselves.

**Not that the Code 128 encoder is a full implementation.** Subsets B and C
only — there is no Code Set A, so there are no Code Set A tests, and nothing
here is evidence about FNC characters, Shift, or the extended-ASCII escapes.
None of those has a use in an id.

This paragraph used to say Subset C was worth "about one extra character of
headroom for a numeric id", which was the wrong way to value it. Headroom was
never the constraint; the module width was. On a 16-digit id Subset C is worth
0.085 mm of X-dimension — 119 µm to 204 µm, which is the difference between
falling under the 5 mil line and clearing it comfortably.

## The service harness

[`ServiceHarness.hpp`](ServiceHarness.hpp) is the file worth reading before
changing `Service.cpp`. `SDK::TestSupport::StubAppComm` is virtual with a
`getMessage()` that returns nothing and a `sendMessage()` that drops what it is
handed; overriding both lets the test be the kernel. `Service` runs unmodified
and does not know the harness exists.

It exists for the reason SunGlance's does: a *correct* id that never reaches
the GUI looks exactly like having no file at all, and no unit test sees the
difference. SleepLab's glance sent nothing for weeks with every part of it
passing its own tests.

`queueAction()` is the part that earns it. Without a way to change the file
*between* two messages, a test could only set things up before the run and
would be asserting on a situation that never arises — the GUI resuming after
somebody rewrote `input.json` is the whole reason `BARCODE_REQUEST` exists. The
four tests that use it are the ones that matter:
`PicksUpANewIdOnRequestWithoutRestarting`,
`AFileArrivingWhileRunningIsPickedUpOnRequest`,
`AFileGoingBadWhileRunningStopsShowingTheOldBarcode` and
`TheFileBeingDeletedWhileRunningIsNoticed`. The third is the harmful direction:
if the file goes bad, the app must stop drawing the id it used to have, because
a stale barcode still scans as the wrong person.

`NoPublishedStateEverPairsAProblemWithAnId` states that as a property over
every way the file can be wrong, rather than case by case.

One thing to know before editing the harness: on the watch these messages come
from a fixed pool, so `SDK::MessageBase` deletes `operator new` and keeps its
destructor protected — an app cannot heap-allocate one. `ScriptedMessage`
restores both for itself, because the harness is standing in for the kernel side
of that pool rather than for an app.
