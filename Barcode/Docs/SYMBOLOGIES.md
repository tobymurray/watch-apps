# Supporting more than Code 128

An investigation into one question — *what is the best avenue toward a second
symbology?* — and the answer turns out to be less about the encoder than about
the 200 pixels the bars are drawn in.

**Steps 0 and 1 at the bottom are built**, and so is QR, which is not on this
list at all — see [QR.md](QR.md) and the section below. The seam exists
(`Encoded.hpp`, `Symbology.hpp`, `Barcode::Format`) and Code 128 now switches
into subset C wherever that shortens a barcode. The linear rest is still a
ranking, not a plan.

Everything below is derived arithmetic against `BarcodeLayout.hpp` and the
symbology definitions. **None of the linear candidates ranked here has been put
in front of a scanner.** That is still true of Code 128 as this app draws it.
The one format that *has* been scanned is QR, which is not on this list — see
[QR.md](QR.md).

## The short version

1. **The seam already exists and is in the right place.** `Code128::Encoded` is
   a run-list of bar/space widths. That is the natural intermediate form for
   *every* linear symbology, and `BarcodeWidget::drawCanvasWidget()` consumes
   nothing else. The renderer needs no change at all. Formalising the seam —
   a `Barcode::Format` enum and a dispatching `encode()` — is a pure refactor
   the existing test suite already covers.
2. **The cheapest real win is not a new symbology at all.** Code 128 **Subset
   C** is the same symbology, the same table and the same scanners, and it
   nearly halves the module count of a numeric id. It is the only option on
   this list with no configuration surface and no compatibility risk.
3. **ITF's density advantage is smaller than it looks**, because a
   standards-legal wide:narrow ratio at this X-dimension is not 2:1. It ends up
   roughly level with Subset C, while carrying a short-scan hazard that this
   app's whole thesis is about refusing.
4. **The expensive part is the configuration and the failure message**, not the
   encoding. The SDK has no enum field type and no cross-field validation, so
   the phone cannot check an id against the format the user picked — meaning a
   bad pairing installs cleanly and is only refused on the watch, where there is
   one prompt for every way a code can be undrawable.

## Why this is a scannability question

`BarcodeLayout::Scannability` has the arithmetic: the bars get 200 px and the
dot pitch is 126 µm, so the module count *is* the X-dimension.

**There is no ISO minimum to compare that against.** ISO/IEC 15417 leaves the
X-dimension among "the parameters to be defined by applications" and sets no
floor for Code 128 — an earlier version of this document asserted a 0.19 mm ISO
minimum and a 0.25 mm retail floor, and both were wrong. See f92e1ce, which
retracts them. The reference points that survive are scanner capability, not
conformance: **127 µm (5 mil)** as the low end of what scanner-selection
guidance gives for a mid-density symbology, and **76 µm (3 mil)** from the spec
sheet for Zebra's LS2208.

| Id | Code 128 B | X-dimension | |
| --- | --- | --- | --- |
| 4 chars | 79 modules | 319 µm | |
| 8 chars (parkrun-length) | 123 modules | 205 µm | |
| 12 chars | 167 modules | 151 µm | |
| 16 chars | 211 modules | 119 µm | under 5 mil, over 3 mil |
| 22 chars | 277 modules | 90 µm | the longest id accepted |

The rows past 15 characters are all past what the panel draws cleanly, which is
200 modules — one module per pixel. That is a separate limit from the two above
and it binds first; the README's *How much fits* has the measurement. It is only
reachable with subset-B content, because digits pair.

That reframing matters for what follows: a second symbology is **not** a fix for
an out-of-standard defect, because there is no such defect. It is a lever on
**modules per character**, worth pulling only where a specific card or scanner
makes it worth pulling. Here is the whole field, at 8 characters:

| Symbology | Modules | X-dim | Elements | Notes |
| --- | --- | --- | --- | --- |
| ITF, 2.0:1 | 64 | 394 µm | 47 | ratio likely illegal at this X-dim — see below |
| **ITF, 2.5:1** | 72.5 | **348 µm** | 47 | digits only, even length, no check digit |
| **Code 128 C** | 79 | **319 µm** | 43 | digits only, even length, *same symbology* |
| ITF, 3.0:1 | 81 | 311 µm | 47 | |
| EAN-13 (13 digits) | 95 | 265 µm | 59 | fixed length, mandatory check digit |
| **Code 128 B** | 123 | 205 µm | 67 | today |
| Code 39, 3:1 | 159 | 158 µm | 99 | worse than today, and overflows at 16 chars |

Two things fall straight out of that table.

**Code 39 and Codabar are not worth building.** They are less dense than what
the app already draws, and Code 39 at 16 characters needs 179 elements against
the `Encoded::kMaxWidths` this app allows. A format that is both worse and
more expensive has no case.

**ITF is not the free lunch it first looks like.** The 2:1 ratio that produces
394 µm is the one every tutorial uses, but ISO/IEC 16390 constrains the
wide:narrow ratio as a function of the X-dimension, with the tighter floor
(around 2.5:1) applying at small X-dimensions — which is exactly the regime this
panel is in. *Confirm the exact thresholds against the standard before
committing to a number.* At 2.5:1 ITF lands at 348 µm against Subset C's 319 µm:
a real but modest margin, bought with a symbology that has no check digit.

## The seam: what actually has to change

The blast radius is six source files and two documents:

```
Barcode.hpp        Code gains a Format; Problem may need to say more
Code128.hpp        Encoded moves out to a shared header
Commands.hpp       message grows — measured below
Service.cpp        adoptCode() dispatches instead of calling Code128::encode
AppConfigFields    a fmtN field per code
BarcodeWidget      setCode(Format, const char*); the drawing code is untouched
MainView.cpp       the BadValue prompt, and the id layout's length assumptions
README.md          the title says "Code 128"
app-manifest.json  so do name and description
```

Nothing in `BarcodeWidget::drawCanvasWidget()` changes. It reads
`totalModules`, `count` and `widths`, walks the run alternating bar/space
starting with a bar, and scales to the widget width. That is a complete
description of any linear symbology's output, which is why this is a refactor
and not a rewrite. It is also why a non-integer wide:narrow ratio costs nothing:
express ITF at 2.5:1 as narrow = 2, wide = 5 and the renderer is none the wiser,
because it only ever divides by the sum.

Some measurements against the constraints that bite:

- **Message size.** `BarcodeState` must fit a 256-byte kernel pool block. Adding
  a `uint8_t format` to `Code` takes the message from **196 to 200 bytes**;
  seven codes with a format byte would be 231. The `static_assert` in
  `Commands.hpp` stays satisfied with room to spare.
- **Element buffer.** `Encoded::kMaxWidths` is 115. ITF at 16 digits needs 87
  elements, EAN-13 needs 59. Only Code 39 overflows it.
- **Field count.** The SDK allows 32 config fields. Six codes at three fields
  each is 18. Fits.
- **Build glue.** None. `libs.cmake` globs `Sources/*.cpp`, and a symbology is a
  header.
- **Flash.** Code 128's table is 642 bytes. ITF's is 50, EAN's three tables are
  120. Negligible either way.

## The part that is genuinely hard

Not the bars. The configuration.

**There is no enum field type.** `SDK::AppConfig::Type` is Bool, Int, Float,
String, and section 9.4 of `app-config-fields.md` renders a string as a plain
text box. A format selector is therefore a word the user *types* —
`stringField("fmt1", "", 0, 8)` with a pattern of `code128|itf|ean13` — which
the pattern dialect does support, but which is a poor thing to put on a phone
form six times over. An `int` with a documented mapping is worse. This is the
single biggest argument for keeping the format count small.

**The phone cannot cross-validate.** Each field's `pattern` sees only its own
value, so `id1` is checked against `[ -~]{0,22}` whatever `fmt1` says. Choose ITF
and type an odd number of digits and the companion app accepts it, writes it,
and the watch refuses it on launch. That inverts the property the README is
proudest of — that the phone validates as you type — for every code that is not
Code 128.

**One prompt has to cover more ground.** `Problem::BadValue` currently reads
*"That ID cannot be drawn: 1-22 plain characters"*, which is the complete truth
while there is one format. With three it is neither true nor actionable: "ITF
needs an even number of digits" and "that is 23 characters" are different
problems with different fixes, on a device with no keyboard where the screen is
the only place a fault can be reported. The Concessions section already records
losing this ability once; adding formats loses more of it, and the honest fix is
more `Problem` values or a format-aware prompt, not a vaguer sentence.

There is a tempting way around the first two: put the format in the id itself,
`itf:0123456789`, one field with a pattern like
`[ -~]{0,22}|itf:([0-9]{2}){1,10}|ean13:[0-9]{13}` that the phone *can* fully
enforce. It is rejected here, for the reason the README gives for deleting the
old single-space sentinel: it is in-band signalling in the one field the app's
safety story rests on, and it spends four id characters on a prefix.

## Recommended order

**Step 0 — split the symbology from the encoder. Ship it with one format. ✅ done**

`Encoded` moved out of `Code128.hpp` into `Encoded.hpp`, `Barcode::Format` went
into `Barcode.hpp`, and `Symbology.hpp` is now the only file that names both a
format and an encoder. `Code` carries a `Format`, `adoptCode()` validates
through the dispatcher, and `BarcodeWidget::setCode()` takes one. Nothing
user-visible changed. The message went 196 → 200 bytes against the 256-byte
static_assert.

**Step 1 — Code 128 Subset C, chosen automatically. ✅ done**

The best value on the list, and it delivered more than this document first
estimated. It is not a new symbology, so there is no config field, no new
prompt, no phone-side validation gap, and no scanner that reads this app today
stops reading it.

| Id | Before | After | X-dimension |
| --- | --- | --- | --- |
| `12345678` | 123 | 79 | 205 → 319 µm |
| `A1234567` (parkrun) | 123 | **101** | 205 → **249 µm** |
| `1234567890123456` | 211 | 123 | 119 → 204 µm |

The parkrun figure is the one worth correcting: this document originally
guessed 112 modules, on the assumption that a B→C switch would save a single
symbol. It saves two, because spending the odd leading digit in subset B lands
the remaining six on an even boundary and they pair up — 9 symbols against 11.

The headline claim is that **every numeric length now clears the 5 mil
mid-density line**, including fifteen and sixteen digits, which are the two
lengths that fall just under it when they are letters. Long numeric ids are no
longer the awkward case.

The host suite covers it: `Tests/Code128_test.cpp` gained a `Code128SubsetC`
group and a subset-aware round-trip decoder, and `Tests/Layout_test.cpp` gained
the numeric X-dimension characterisations. 82 tests pass.

**The tests found something the arithmetic here had assumed away.** An odd
number of digits strands one that cannot be paired, and carrying it costs a
symbol of its own *plus* a switch back to subset B — so **adding a digit to an
odd-length numeric id makes the barcode narrower**. Nine digits are 101 modules
and ten are 90; fifteen are 134 and sixteen are 123. Fifteen digits is
therefore the worst case in the whole numeric range at 188 µm — still half
again the 5 mil line, but the one length where adding a digit would help. Two
draft tests asserting tidy monotonic thresholds failed, which is how this was
noticed.

Also worth pinning, and now pinned: symbol value 99 means Code C in subset B
and the digit pair "99" in subset C, so `999999999999` is six data symbols that
a decoder testing the value before the subset would read as an empty string.

**Step 2 — a second symbology, only once there is a card it is needed for.**

Between the two candidates worth building:

- **EAN-13/UPC-A** is the more defensible. Fixed 95 modules regardless of
  content, 265 µm, and — uniquely on this list — a **mandatory check digit**,
  which means the app can refuse a mistyped card number. That is the only format
  that lets this app detect the harm it exists to prevent, rather than merely
  decline to guess. Cost: guard bars conventionally extend below the bar height,
  and the human-readable digits use a 1-6-6 arrangement, neither of which a
  scanner needs but both of which a user will notice missing.
- **ITF** is the one to reach for when the *receiving system* wants ITF, not for
  density. It is continuous, has no self-checking, and is famous for decoding a
  partial scan as a valid shorter number — which is precisely the
  "plausible value that scans" failure the README says must never happen. If it
  is built, **bearer bars are a correctness requirement, not decoration**, and
  they have to come out of a quiet zone that `Layout_test` already characterises
  as short at every useful length.

## QR codes are a different project — and it got built

**This section is superseded by [QR.md](QR.md)**, which answers the product
question this one left open, disagrees with three of the estimates below, and
records what was built. Kept here because the framing was right even where the
numbers were not.

What this section got right, and it is the important half: **none of the work
above helps.** Everything on this page is a *linear* symbology. The seam that
makes ITF or EAN-13 cheap is `Barcode::Encoded` — a run of alternating bar and
space widths — and that is a complete description of what a linear symbology
produces and a useless one for a matrix symbology. QR produces a square grid of
light and dark modules with no left-to-right run structure at all. `Encoded`
cannot carry it, and `BarcodeWidget::drawCanvasWidget()`, which walks widths and
draws full-height bars, cannot draw it.

That is exactly how it was built: `Barcode::Matrix` is a second intermediate
form and `QrWidget` is a second renderer, and **`Encoded` was not touched**.
Making it a variant would have put a discriminant and a branch on the path that
draws every parkrun barcode, to serve a format that path cannot draw.

Three things this section got wrong:

- **"A few kilobytes of code and tables ... would need a genuine third-party
  implementation or a very well-tested one of our own."** The second half
  stands. The first is high, because fixing the version and the error-correction
  level deletes most of QR: no version selection, no interleaving, no block
  structure table, no alignment-position table, no version-information BCH
  (needed from version 7), no mode selection. Measured at **+3,680 bytes on the
  package**, of which the encoder is 2,856.
- **"The screen is *not* the blocker."** True in principle and incomplete in
  practice. The largest square inside the circle is 168 px, not "about 169", and
  that is not the binding number: a symbol must end above the id row, which
  leaves 144 px. So the 5.8 px module quoted below is not available — a 5 px
  module misses fitting by **one pixel row** — and the module is 4 px, 0.504 mm.
  Every size that fits also overlaps the caption band, so a QR code shows no
  name. The *layout* was more of a blocker than the screen.
- **"There is still no oracle in the tree."** There is, since `a119f9c`, and
  extending it to QR is the single thing that made this feature defensible.
  `generate_qr.cpp` commits 144 zint grids — eighteen payloads by eight forced
  masks — and the encoder matches every one, module for module.

What this section got right about the product, and what changed: QR and Code 128
are indeed not substitutes, and the asymmetry runs one way. A laser funnel
scanner reads Code 128 and cannot read QR at all; an *imaging* scanner reads
both, and most modern handhelds are imagers rather than lasers. So QR is
opt-in per code, `Format::Code128` stays 0 and stays the default, and the
"two apps sharing a config file" worry does not apply — because the thing built
is a second way to draw *the same sixteen-character id*, not a way to carry a
new kind of value. Raising `kMaxIdLength` for URLs and tickets is the change
that would make it two apps, and QR.md argues against it: six 63-character ids
do not fit the 256-byte message, and a URL has no readable rendering under the
symbol.

## What the tests will and will not give you

`BarcodeLayout::Scannability` is parameterised on module count, not on the id,
so every quiet-zone and X-dimension claim generalises to a new symbology for
free. Only `modulesFor()` in `Layout_test.cpp` is Code 128 arithmetic.

The encoder side does not generalise for free. There is an oracle now — zint,
since `a119f9c`, and `generate_qr.cpp` for the matrix side — and it is the right
first reach for any new symbology. Beyond it, each one still needs its own
structural claims in the spirit of the five that hold the Code 128 table
together — element counts, module sums, the
parity property where one exists, distinctness — plus hand-worked golden
vectors. A symbology added with only "it renders" as evidence would reintroduce
exactly the risk the Code 128 table tests were written to close.

And as before: none of this is evidence that anything scans. The framebuffer
dump trick from `RustGuiPoc` remains the missing piece, and it matters more with
each format added, not less.
