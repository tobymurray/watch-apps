# ButtonProbe — which pin is which button, and can a glance see it

The second of two experiments. [`RunGuiProbe`](../RunGuiProbe) established that a
glance can open a screen by asking the kernel. This one asks whether a glance
can find out that you want one.

> Unofficial. Not affiliated with, endorsed or sponsored by UNA Watch Ltd.
> It reads hardware registers on a watch you own. It never writes one.

## The two questions

A glance service is never sent `EVENT_BUTTON` — that message goes to a GUI, and
while the carousel is up there is no GUI. So the only way a glance could notice
R1 is to look at the pin itself. An app on this watch runs privileged with the
MPU off, which is what [`FwDump`](../FwDump) is built on, so it can.

1. **Which GPIO pin is which button?** Nothing published says. It has to be
   measured.
2. **Can a glance service catch the edge?** Different process, different
   lifecycle, and a carousel that owns the buttons. Being *able* to read a
   register says nothing about being scheduled at the right moment.

They need different setups, so the app has two halves and a switch between them.

## Nothing here writes a register

`GPIOx_IDR`, eight of them, `const volatile`. That is the complete list of
hardware this app touches.

That restraint is not fastidiousness. **R1 is on the power line** — the SDK's own
button table names SW2 as `PWR_ON_1V8_L`. Reading `IDR` is inert; writing that
port's `MODER`, `OTYPER`, `PUPDR` or `BSRR` is not, and getting it wrong is
considerably worse than a button that stops working.

The addresses are not from memory or from a datasheet read once. Every base is
copied from `FwDump`'s sweep table, where each was **read successfully on this
exact watch** on 2026-07-29 and corroborated against RM0456. `IDR` is at offset
`0x10`, inside the twelve words that sweep already reads — so this app reads
nothing this unit has not already survived.

`GPIOI` is deliberately absent: it is not in that validated table, and adding it
would mean reading an address nothing has confirmed. If no button turns up on
A–H, that is the next thing to try, on purpose and on its own.

## How it finds the pins

Not from a list. Diffing every bit of every port drowns — most of those pins are
SPI clocks and display buses toggling faster than anything can sample them.

So for **half a second at startup, before anybody touches the watch**, every port
is sampled as fast as the loop goes round. Any bit seen both high *and* low in
that window is a signal, not a button, and is struck out. What survives is the
set of pins that sat still while the watch was busy — and a button, held at one
level by its pull-up until a finger arrives, is exactly a pin that sits still.

The card and the screen both say when the calibration window is open. Pressing
something during it is the one way to ruin a run.

### The prediction, kept out of the search

The recorded 2026-07-29 sweep narrows it a long way on its own. Reading `MODER`
and `PUPDR` back, the pins configured as input-with-pull-up are:

| Port | Candidates |
| --- | --- |
| A | `PA5` |
| D | `PD4`, `PD8`, `PD15` |
| G | `PG7` |
| E | ten of them — `PE1`, `PE3`, `PE5`, `PE6`, `PE7`, `PE9`, `PE11`, `PE13`, `PE14`, `PE15` |

Four of those are the buttons. That list is a **prediction to check the answer
against, not an input to the search.** Wiring it in would mean the app could only
ever confirm what was already assumed, and would find nothing at all on a watch
whose firmware muxed a pin differently.

`GPIOH` reads `FFFFFFFF` and is treated as absent. That test is exact rather than
a guess at "all ones": the top sixteen bits of a real `IDR` are reserved and read
zero, so any value with one set is a port that is not answering — where a genuine
port with all sixteen pins pulled high would read `0000FFFF` and still count.

## Polling, not ticking

`RunGuiProbe` measured the glance tick at **about 1 Hz**, not the 60 Hz
`CommandMessages.hpp` gives as its example. A press and release is over inside
one of those intervals, so anything sampling on the tick would miss most presses
and could not tell a miss from an absence.

Neither half samples on a tick. Both block on `getMessage(msg, 8ms)`, which
returns either a message or a timeout, and poll on every turn. The queue drains
normally; the timeout just puts a floor under how often the pins are looked at.
**That structure is the real proposition being tested** — if a glance service can
see a button at all, this is how.

## Running it

Two phases, chosen by a file. Drop an empty **`gui.on`** into `Apps/ButtonProbe/`
over USB and the card hands the screen to the GUI; delete it and the same card
samples pins itself. The card says which mode it is in.

### Phase 1 — the labelled map (`gui.on` present)

A GUI receives `EVENT_BUTTON`, and it says *which* button. It can also read the
pins. Run both in one loop and the log answers question 1 with the kernel itself
supplying the labels:

```
t=41218 pin PD4 -> 0
t=41219 BUTTON id=1 event=0
t=41402 pin PD4 -> 1
t=41403 BUTTON id=1 event=2
```

One clock, two streams, one file — the correlation is a fact in `button-gui.txt`
rather than a claim about what you remember pressing.

Press each of the four buttons a few times. Amber screen means calibrating —
hands off. After that, the square changes colour on every edge the sampler sees,
so you know it is working without unplugging anything.

**Press R2 last**: it exits. Not instantly — it schedules the exit 400 ms out so
the release edge of the press that ends the session is in the log like every
other one. There is a two-minute deadline behind that, so no sequence of events
leaves the watch stuck in here.

### Phase 2 — the real question (`gui.on` deleted)

Now the card samples the pins itself. It shows the number of edges seen and the
last pin that moved:

```
        edges: 3
        PD4>0
```

Press R1 while the card is up. **If the card names a pin, a glance service can
see a button** — and everything behind this line of work is possible. Reading it
needs no USB cable.

Expect the carousel to scroll away as you press; the edge is logged the moment it
is sampled, so a viewing that ends immediately still leaves its evidence in
`button-glance.txt`.

## Reading the result

| File | Written by | Holds |
| --- | --- | --- |
| `button-gui.txt` | phase 1 | the calibration mask, then interleaved pin edges and labelled button events |
| `button-glance.txt` | phase 2 | the calibration mask, then whatever edges the glance service managed to catch |

Both open with the watched mask, port by port. Read it first. A port that says
`nothing to watch` is a port where every pin was busy — and if the buttons turn
out to be on it, that run's silence means nothing at all.

**A negative result is worth as much as a positive one, but only if the mask says
the pins were being watched.** "No edges seen, and PD4 was in the mask" is
evidence. "No edges seen" on its own is not.

## What it does not answer

- **Whether the pin mapping survives a firmware update.** It should — a pin
  assignment is a property of the board, not the software, which is the one part
  of this whole approach that is not built on an unpromised kernel behaviour.
- **Whether polling at 8 ms is enough for every press.** A very fast tap could
  fall between two samples. The log's timestamps show the gaps, so a run that
  looks lossy can be diagnosed rather than argued about.
- **Whether any of this should ship.** Working around the platform on a watch you
  own is one thing; a catalogue app that reads hardware registers behind the
  kernel's back is another.

## Layout of the code

```
Software/
├── Libs/
│   ├── Header/Sources
│   │   ├── GpioPorts.*   The eight IDR reads. The only hardware in the app.
│   │   ├── PinWatch.*    Which pins are still, and which just moved. No SDK.
│   │   ├── Sampler.*     The poll loop's body, shared by both halves.
│   │   ├── ProbeLog.*    A copy of RunGuiProbe's; see its header for why.
│   │   └── Service.*     The glance half, and the gui.on switch.
└── Apps/
    ├── CustomGUI/        Gui.* — the labelled half.
    └── ButtonProbe-CMake/
```

`PinWatch` compiles against nothing but the standard library, which is what lets
the pin-selection argument be had at a desk rather than on a wrist.

## Building

Needs `$UNA_SDK` on **`apps-v1.4.0`**.

```sh
export UNA_SDK=/path/to/una-sdk
cd Software/Apps/ButtonProbe-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 . && cmake --build build
```

Or in the container that actually links, as
[RunGuiProbe](../RunGuiProbe#building) shows.

Deploy by copying the `.uapp` into `Apps/ButtonProbe/` on the USB-MSC volume.
No `registry/` manifest: `gui.on` is a switch for one experiment, not a setting,
and this is not something to install from a catalogue.

`AppID` is `A6B4C5BC71F9CD3E` =
`sha256("https://github.com/tobymurray/watch-apps#button-probe")[0:8]`.

## Tests

```sh
export UNA_SDK=/path/to/una-sdk
cd Tests
cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)
```

Sixteen tests over synthetic snapshots and an in-memory filesystem. They cover
the reasoning applied to what the registers return — pin selection in both
directions, the absent-port test, edge detection and its buffer bound — and none
of them cover the measurement itself, which is a read of a hardware register on a
watch and cannot be had here.

## Licence

MIT — see [../LICENSE](../LICENSE).
