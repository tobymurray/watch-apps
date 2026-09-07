# SettingsKit — reading and writing the watch's real settings, from an app

The shared half of [`NotifyToggle`](../NotifyToggle) and
[`UnitToggle`](../UnitToggle): the firmware gate, the address table, the live
struct read/write, and the commit that puts a change into `2:/settings.json`
without disturbing a byte the app does not own.

This is a directory, not an app — it builds nothing on its own. Each app folds
it into its own GUI target:

```cmake
set(SETTINGSKIT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../SettingsKit")
include(${SETTINGSKIT_PATH}/settingskit.cmake)
# ... then ${SETTINGSKIT_SOURCES} in GUI_SOURCES and
#     ${SETTINGSKIT_INCLUDE_DIRS} in GUI_INCLUDE_DIRS.
```

## Why it is shared rather than copied

Everything here is derived from one firmware image by hand, and none of it is
checkable by the compiler. An address that turns out to be wrong, a commit
rollback that turns out to have a hole, a refusal that turns out to be too
loose — each is found once and has to be fixed everywhere at once. Two copies
drifting apart on a part with no MPU is not a maintenance annoyance; it is one
app writing to an address the other has already learned is wrong.

`NotifyToggle/README.md` is the long-form design record for the mechanism — how
each address was derived, why the gate has the steps it has and in that order,
and what was proved on a watch. Read it before changing anything here.

## What an app supplies

Two descriptors, and nothing else:

- **`LiveSettings::Flag`** — the byte's offset in the live struct, paired with a
  name for the log. The offset is a per-firmware runtime value out of
  `SettingsAddresses::AddressSet`, so an app builds this once the address set is
  resolved rather than declaring it as a constant.
- **`SettingsPersist::Field`** — the splice function for the field it owns, and
  the scratch paths it commits under.

Every path in `Field` must be that app's alone. The commit moves the wearer's
only settings file aside under `prevPath` before renaming the replacement into
place, and recovery finds it again by that exact name; two apps sharing one
would each treat the other's interrupted commit as its own. `NotifyToggle`'s
`ntprev` name in particular is fixed for good — a watch left holding one by a
commit that lost power is recovered by matching it.

## The two fields, and how they differ

| | `phone.notifications` | `units` |
|---|---|---|
| In the file | nested boolean, `true`/`false` | top-level string, `"metric"`/`"imperial"` |
| Length delta on rewrite | 1 byte | 2 bytes |
| In the live struct | one byte at `+5` | one byte at `+4`, 0 = metric |
| Readable through the SDK | no | yes, `RequestSystemSettings::imperialUnits` |

That last row is the one that matters. `phone.notifications` appears in no
app-facing message at all, so `NotifyToggle` needs a raw pointer even to show
the current state. `units` does appear, so `UnitToggle` reads it the supported
way and needs the raw pointer only to change it — and can check its own write
against what the kernel then reports.

## A live fault this kit has, and does not yet fix

`setNotifications` narrows to the `phone` object and then matches
`"notifications"` at any depth inside it, so a `notifications` key nested within
`phone` is rewritten in preference to `phone.notifications` itself. Measured
against the real function:

```
in   {"phone":{"nested":{"notifications":true},"notifications":false}}
out  unchanged, result Ok, phone.notifications still false
```

The commit reports success, because the readback compares the file against the
buffer just written rather than re-parsing it — so the wrong key is confirmed,
and the change reverts at the next reboot. `setUnits` was given depth scoping
against exactly this; `setNotifications` predates it and is shipped in
NotifyToggle, so fixing it is its own change against its own baseline rather
than something to fold into a units app.

No firmware is known to emit such a file: `phone` holds one key on 1.4.0.
Falsified by a settings file with a `notifications` key nested inside `phone`.

## Tests

The splice is the only part that runs without a kernel — it decides which bytes
of a real personal settings file get rewritten, so it is where the tests are:

```sh
cmake -B build -S Tests && cmake --build build && ctest --test-dir build
```

Both fields are covered against real files read off a watch, including the
metric/imperial pair read either side of a flip on 2026-09-06. What the tests
cannot reach is everything above them: the addresses, the `File` primitives and
the commit rename only ever run on a watch.
