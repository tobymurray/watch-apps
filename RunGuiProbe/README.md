# RunGuiProbe — can a glance ask for a screen?

One experiment, on one watch, answering one question that decides whether a
much larger piece of work is worth starting.

> Unofficial. Not affiliated with, endorsed or sponsored by UNA Watch Ltd.

```
        ┌────────────────────────────┐
        │        asking in 3s        │      ...and three seconds later,
        │     stay on this card      │      either a screen, or a reason.
        └────────────────────────────┘
```

## The question

The glances that ship with the watch mostly do nothing on an R1 press.
**Notifications is the exception**, and the firmware says why: `GlancesView`,
`PhoneNotificationListView` and `PhoneNotificationDetailsView` are all TouchGFX
screens compiled into the kernel, sharing one `Model`. That R1 is an internal
transition between two firmware screens. There is no seam in it for an app.

For a third-party glance there is no route at all:

| | |
| --- | --- |
| A button event | `EVENT_BUTTON` is documented as going to a GUI, and the TouchGFX port is its only consumer in the SDK. While the carousel is up there is no GUI to send it to. |
| A "detail" field | `RequestGlanceConfig` and `RequestGlanceUpdate` carry an area, an array of controls and a name. Nothing else. |
| A route to `UserAppView` | Kernel-side, and not something a `.uapp` can ask for. |

So a glance cannot *notice* R1. But there is a service message —
`RequestAppRunGui`, "kernel, launch my GUI" — and nothing in the SDK says a
glance service may not send it. Nothing says it may, either.

**If the kernel refuses it, the whole idea is dead** — including the much larger
version, where a glance service polls the button GPIO directly to notice the
press, since detecting R1 is worthless if there is nothing to do about it.
Better to learn that from twenty lines than from a driver.

## What it does

A `Glance`-type app that also carries a GUI — which
the SDK's `app_merging.py` permits, making the
`.gui` *optional* for this type rather than forbidden — and which sends the
request from its glance.

Scroll to the card. It counts down from three, then sends `RequestAppRunGui` and
writes down what came back.

The dwell is not decoration: firing on the first tick would hijack the screen
every time the carousel passed the card, and would leave nothing on it long
enough to read. Three seconds means it only happens when somebody stops and
waits — which is the gesture being stood in for.

## Reading the result

The screen is the thing under test, so the answer is not kept there. It is in
**`Apps/RunGuiProbe/probe.txt`**, which mounts over USB with everything else.

| `probe.txt` says | Means |
| --- | --- |
| `run-gui: SUCCESS -- kernel accepted the request` | The kernel will launch a GUI for a glance app on request. The route exists. |
| `run-gui: refused, result=2` | The kernel answered, and the answer was no. **This retires the idea**, GPIO polling included. |
| `run-gui: send failed` / `could not allocate the message` | The kernel never saw a request. Says nothing either way — retry before concluding. |
| *the file does not exist* | The app never ran. Most likely the loader will not accept a GUI ELF on an app of type `Glance` at all, which is a different failure from a refusal — see the control below. |

`probe-gui.txt` appears only if a GUI actually started, and it is the *only*
proof that one did: a `SUCCESS` on the request means the kernel accepted the
message, not that pixels reached the panel.

Every other message the service receives is logged too, with the interesting
ones being whatever arrives *after* the request — `gui-running`, `glance-stop`,
`app-stop`, and their order. Ticks are counted rather than logged; at 60 Hz one
viewing would otherwise fill the file.

### The control

If the app appears in the watch's own list, launch it by hand. That separates
*"this GUI never loads"* from *"this GUI never loads **from a glance**"*, and
those two want completely different next steps. Whether a `Glance`-type app is
listed at all is itself unknown, which is why this is a control and not a step.

## The screen, if there is one

Three colour bands inside a white frame. No text, because text needs a font and
a font needs either TouchGFX or a rasteriser, and neither would make the answer
any clearer. It cannot be confused with any other screen on the watch, which is
the entire requirement.

**It always leaves.** The GUI owns the message queue and swallows every button,
so an app with no way out is an app you reboot the watch to escape — and a
reboot destroys the evidence. So there are two exits, and the second does not
depend on anything working:

- **R2** (SW4), back, as in every SDK app; and
- **a 30-second deadline**, after which it exits on its own whether or not a
  button event ever arrived.

If buttons turn out not to reach a GUI launched this way, that is a finding, not
a stuck watch. Every button that does arrive is logged with its raw id and event
number, so the mapping is written down from the device rather than from a header.

## What this does not answer

- **Whether R1 can ever be noticed.** It cannot, through the SDK. If this probe
  succeeds, the trigger still has to come from polling the button GPIO from the
  service — the buttons are plain GPIO + EXTI (`HWButtons::Btn::init` in the
  firmware), an app runs privileged with the MPU off, and reading an input
  register is the same operation [FwDump](../FwDump) already performs. That is
  the next experiment, and it is only worth running if this one succeeds.
- **Whether any of this survives a firmware update.** A GPIO pin assignment is a
  property of the board and will; a kernel's willingness to honour this request
  is not promised anywhere and may not.
- **Whether it should be done.** Working around the platform on a watch you own
  is one thing. Shipping it is another, and this app is not evidence about that.

## Layout of the code

```
Software/
├── Libs/
│   ├── Header/Sources
│   │   ├── ProbePlan.*   The dwell and the wording. Standard library only.
│   │   ├── ProbeLog.*    The output file. Open, write, close, per line.
│   │   └── Service.*     The glance half: waits, asks, records.
└── Apps/
    ├── CustomGUI/        Gui.* — the screen, on the SDK's non-TouchGFX seam.
    └── RunGuiProbe-CMake/
```

The GUI is a **CustomGUI** binary rather than a TouchGFX one, following
[RustGuiPoc](../RustGuiPoc) minus the Rust. A TouchGFX half would have made the
`.uapp` many times larger to answer a question that needs three rectangles. The
whole app, both halves, is **26 KB**.

One deliberate difference from every other glance in this repo: the service does
**not** return from `run()` on `EVENT_GLANCE_STOP` once a launch has been
granted. The carousel handing the screen to a GUI is likely to stop the glance,
and a service that exits at that moment may take its own GUI down with it and
turn a success into a puzzle.

## Building

Needs `$UNA_SDK` pointing at an **`apps-v1.4.0`** checkout.

```sh
export UNA_SDK=/path/to/una-sdk          # apps-v1.4.0
cd Software/Apps/RunGuiProbe-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 . && cmake --build build
```

Or in the container SleepLab uses, which is the one that actually links:

```sh
docker run --rm --platform linux/amd64 \
    -v "$PWD:/w" -v "$UNA_SDK:/sdk" -e UNA_SDK=/sdk \
    -w /w/RunGuiProbe/Software/Apps/RunGuiProbe-CMake sleeplab-arm:latest \
    bash -lc 'cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 . && cmake --build build -j$(nproc)'
```

Deploy by copying the `.uapp` into `Apps/RunGuiProbe/` on the USB-MSC volume.
There is no `registry/` manifest: the app has no settings, so Kira has nothing
to write, and it is a probe rather than something anybody should install from a
catalogue.

`AppID` is `E54CA618B8BD8B02` =
`sha256("https://github.com/tobymurray/watch-apps#rungui-probe")[0:8]`, the repo
convention.

## Tests

```sh
export UNA_SDK=/path/to/una-sdk
cd Tests
cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)
```

Two executables, fifteen tests, covering the two things whose failure would be
invisible on the watch: the dwell timer (including the millisecond wrap, which
would otherwise fire the request instantly on one card in every 49 days) and the
log file (including handle leaks and a filesystem that will not write).

Most of this app is not testable on a host, and that is the point — the question
is what a *kernel* does with a message, and no double can answer it.

## Licence

MIT — see [../LICENSE](../LICENSE).
