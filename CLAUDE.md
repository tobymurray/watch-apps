# watch-apps

UNA Watch apps. Each top-level directory is one app: a C++ Service, and a GUI that is
either TouchGFX or a C++ shell around a Rust `no_std` renderer.

## Comments

The rule below governs how comments are **written**, not just how they are cleaned up.
`/cleanup-comments` is the same rule applied after the fact; read it for the procedure
and the worked examples.

**Write no comment that this file cannot keep true.** Before writing one, ask:

> Could someone change another file, another class, the firmware, or a spec and make
> this comment wrong, with nothing here changing?

If yes, do not write it. Never owned: what a caller does; what another class, screen or
message handler does or used to do; the name of a test, scene or constant that lives
elsewhere; counts of tests or fields; ticket numbers; planned work; dates. When the
reason lives elsewhere, name the one place that owns it and stop — a pointer, never a
retelling.

**A comment is one sentence**, and never restates the name above it. If the code needs
more than that to be followed, fix the code — rename, extract a function, name the
constant, add a predicate. Reaching for a comment to explain unclear code is the
mistake; the comment is not the fix.

### The carve-out, and it is narrow

This is firmware for one piece of hardware writing a frozen wire format, so three kinds
of fact are unrecoverable from the code and are kept at whatever length they need:

1. **Hardware behaviour proven on the watch** — `enMusicControl` makes the system eat
   `HOLD_1S`; the panel holds four levels a channel; wrist-tilt does not fire with hands
   on the bars.
2. **Frozen wire format** — `session` field 57 is `avg_temperature`, not
   `time_in_hr_zone`; `session.avg_power` is 20 while `lap.avg_power` is 19. A wrong
   number is silent, permanent, and looks fine locally.
3. **A measurement, with its numbers** — the arc sampler leaves 95 unpainted pixels at
   0.85 and 4 at 0.75; consecutive HR samples differ by 0.50 and 0.18 bpm over two real
   rides.

**Every carved-out comment must name what would falsify it** — the setting, the field
number, the rerun. That is the ownership test satisfied, not waived: a comment that says
how to prove it wrong is one someone can notice rotting.

A "why-not" earns its place only under one of those three. An alternative rejected on
taste is not a measurement — leave it out.

The one signature exception is **units and sentinels**, which a C++ or Rust type cannot
show: `uint16_t workKilojoules; ///< kJ; 0 = nobody said`. One line, about the value
only.

`Spin/README.md` is the design record and the register to aim for — the long-form
reasoning lives in prose there, where it is owned and read, not scattered through the
source.

## Working here

- **Prefer measurement over assertion.** This codebase settled its arc-sampling
  threshold by counting unpainted pixels and its HR hold window from dropout statistics
  across two real rides. If you are choosing a number, measure it and record what you
  measured.
- **Logic worth testing goes somewhere it can be tested without a kernel** — a Rust
  module in the app's crate, or a header-only file free of SDK types.
- **Installing a build on the watch has an order, and getting it wrong is
  silent.** Copy the `.uapp` in, then reboot; `Apps/app_list.json` is the
  kernel's output and editing it does nothing, and a stale `.uapp` left beside
  the new one keeps the old build booting. Read `Docs/INSTALLING.md` before
  touching a watch volume — it also explains why `diskutil unmount` appears to
  hang for 159 seconds.
- **Conventional commits, one logical change per commit.** Stage deliberately; a
  `git add <App>` once swept an unrelated change into a commit whose message described
  only the intended fix. CI bumps the version and cuts the release from the commit type,
  so never bump `appVersion` by hand.
