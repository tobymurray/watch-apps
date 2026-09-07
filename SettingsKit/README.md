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

## Both fields are matched at one exact depth

A key is looked for at one brace depth and nowhere else: `units` among the
outermost object's keys, `notifications` among the `phone` object's own, with
`phone` itself found only at the outermost. Anything of the same name deeper in
is not what the kernel parses.

This is not belt-and-braces. The commit's readback compares the file against
the buffer just written rather than re-parsing it, so an edit to the wrong key
is *confirmed* rather than caught: the write reports success and the change
reverts at the next reboot. Both entry points had that fault --
`setNotifications` shipped with it in NotifyToggle 0.6.0, scoped to the `phone`
object but matching at any depth inside it:

```
in   {"phone":{"nested":{"notifications":true},"notifications":false}}
was  unchanged, result Ok, phone.notifications still false
now  phone.notifications true, the nested key untouched
```

No firmware is known to emit either shape -- `units` is the first key and
`phone` holds one. Falsified by a settings file that nests either name.

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
