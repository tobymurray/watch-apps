# Installing a `.uapp` on the watch

Everything here was learnt on 2026-09-03 by getting it wrong four times in a
row, on a watch running firmware 1.4.0. Every step names the silent failure it
exists to prevent, because **not one of these failures announces itself** — in
every case the app either does not appear or quietly goes on running the build
it already had, and the only visible symptom is that the new build's output is
missing. Which looks exactly like the new build being broken.

If you are an agent about to install something: read this first, then check the
version the app itself reports before you believe anything else it wrote.

---

## Read this first: the rule below is contradicted by a measurement

Both this repository's installers — the SDK's `Update-Watch-Apps.ps1` and
Kira's generated script — do nothing but copy the file, hash it, delete stale
`.uapp`s, and tell you to reboot. Neither writes anything else. So the rule
below is what the tooling believes.

**On 2026-09-03 it did not work.** `Squash_0.6.1.uapp` was copied into
`Apps/Squash/` as the only `.uapp` in the folder, SHA-256 and CRC-32 verified
after a physical reconnect, and the watch was restarted. The kernel *did*
rewrite `Apps/app_list.json` at that boot — mtime 14:45:14 in the watch's UTC,
matching the restart — and produced 26 entries with **no Squash row**, leaving
the app absent from the launcher and never run (`Debug/squash.log` untouched).
The app was not in `_disabled_apps_backup` and left no crash dump.

The one difference from a normal update: Squash's registry row had been pruned
beforehand, because the `.uapp` it pointed at was deleted. A plausible reading is
that the kernel reconciles *removals* at boot but never *additions*, so an app
whose row is gone cannot be brought back by putting a binary in its folder. That
is a guess. It is recorded here as a guess because four earlier explanations of
this same problem were each stated confidently and each wrong — see the table
below.

**So: follow the procedure, then verify the app actually registered, and if it
did not, use whatever installed it last time.** Do not conclude the procedure
worked because the file is in place and hashes correctly; that was true in the
failing case too.

## The rule the tooling assumes

**A `.uapp` is installed by copying it into `Apps/<AppName>/` and rebooting the
watch.**

Two corollaries that cost an afternoon:

- **`Apps/app_list.json` is the kernel's output, not its input.** Editing it to
  register a build does nothing. Neither the SDK's own installer nor Kira
  touches that file — `grep app_list` across both finds nothing at all — and
  both end by telling the operator to reboot so the launcher list is rebuilt.
- **A stale `.uapp` beside the new one keeps the old build booting.** The
  kernel loads whichever it finds first, so two files in one folder is a coin
  toss you will lose.

## The order, and why each step is where it is

From `Utilities/Scripts/Update-Watch-Apps.ps1` in the SDK, restated in
[Kira's README](https://github.com/tobymurray/kira#installing-safely). The order
is load-bearing.

### 1. Check the CRC-32 footer before the file touches the watch

```sh
python3 -c '
import zlib, struct, sys
d = open(sys.argv[1], "rb").read()
print("stored 0x%08X  computed 0x%08X  %s" % (
    struct.unpack("<I", d[-4:])[0],
    zlib.crc32(d[:-4]) & 0xffffffff,
    "OK" if struct.unpack("<I", d[-4:])[0] == zlib.crc32(d[:-4]) & 0xffffffff else "BAD"))
' path/to/App_x.y.z.uapp
```

The app packer prints the same number when it builds the file. **A `.uapp` that
fails CRC is dropped *silently* by the kernel**, so the app simply never appears
and nothing says why.

### 2. Write the new file into `Apps/<AppName>/`

New file first, always. The app's `settings.json`, `input.json`, `Activity/` and
any research output are in that same folder and must survive — never remove the
folder.

### 3. Read it back and compare the length to the source

If it differs, delete the bad copy and **stop**. Do not proceed to step 4: the
folder must never be left without a working binary.

### 4. Only now, delete every other `.uapp` in the folder

Not tidiness — see the corollary above. Doing it in this order is what
guarantees a failed copy leaves the previous build installed.

### 5. Eject, reconnect, and verify by hash

```sh
shasum -a 256 path/to/App_x.y.z.uapp "/Volumes/UNA WATCH/Apps/<AppName>/App_x.y.z.uapp"
```

**Hashing straight after writing reads the OS write cache and can report a false
OK.** The eject-and-reconnect is what makes the second hash a statement about
flash. Ejecting is also the only way the write is guaranteed to have landed at
all, which is why pulling the cable instead is a data-loss risk rather than a
shortcut.

### 6. Reboot the watch

A real power-cycle. **A USB replug is not a reboot**: a replug prunes registry
rows whose files have gone but adds nothing, so an app copied in and replugged
is an app that has vanished from the launcher. Only a boot rebuilds the list.

## Then check it registered, and check the version

```sh
python3 -c '
import json, sys
d = json.load(open("/Volumes/UNA WATCH/Apps/app_list.json"))
print([a for a in d["apps"] if a["name"] == sys.argv[1]] or "NOT REGISTERED")
' Squash
```

An app with no row here does not appear in the launcher, whatever is in its
folder. This is the check that would have saved the afternoon: the file was
perfect and the app was not installed.

## Then check the version, not the behaviour

```sh
cat "/Volumes/UNA WATCH/Apps/<AppName>/Debug/<app>.log"
```

If the app writes a launch line carrying its own `BUILD_VERSION` — as `Squash`
does, deliberately, for this reason — read it and confirm it is the build you
installed. If it does not, add one; an app that cannot say which build is
running cannot be debugged over USB, and that is the position every failure
below was diagnosed from.

## What went wrong, so nobody re-derives it

| Attempt | What was done | What happened |
| --- | --- | --- |
| 1 | Copied the `.uapp` in and hand-edited `app_list.json` to register it | The kernel rewrote the file; the app kept running a build from three versions earlier, and `Debug/` never appeared, which read as the logging being broken |
| 2 | Concluded the phone must be the installer | No phone install had taken place. The conclusion was invented to fit a folder listing |
| 3 | Concluded the stale `.uapp` was the whole story | Half of it. The output-only registry was the other half |
| 4 | Concluded copying installs and the reboot registers | Wrote it into this file, and then a copy plus a real restart did not register the app at all — see the top of this document |

The `.uapp` files `Squash_0.3.0.uapp` and `Squash_0.4.0.uapp` sat inert in
`Apps/Squash/` from 2026-09-01 to 2026-09-03 for exactly this reason: they had
been copied in correctly and left beside an older file, so an older build kept
booting and nothing said so.

## The eject trap: Spotlight

`diskutil unmount` on the watch appeared to hang indefinitely. It does not — it
takes **159 seconds** and then reports:

```
failed to unmount: dissented by PID 560 (…/Metadata.framework/…/mds)
```

**Spotlight indexes the watch volume and dissents the unmount.** Corroborating
evidence, in case this needs re-checking: `.Spotlight-V100` had been on the
volume for weeks, `.fseventsd` was being written during the session, `mds` had
193 minutes of CPU, `lsof` on the volume was clean (a DiskArbitration dissent is
not an open file handle), and the FSKit exfat extension was idle at 0.03 s of
CPU — so it was blocked, never the filesystem driver's fault. macOS 26.6.2
mounts exFAT through FSKit (`com.apple.fskit.exfat.appex`), which is why the
mount options say `fskit`.

It also makes `sync` appear to hang, and it is the mechanism behind Kira's
warning about a write cache reporting a false OK.

Turn it off, both of these:

```sh
touch "/Volumes/UNA WATCH/.metadata_never_index"   # travels with the watch
touch "/Volumes/UNA WATCH/.fseventsd/no_log"       # stops the FSEvents journal
rm -rf "/Volumes/UNA WATCH/.Spotlight-V100"        # reclaim the stale index
```

`.metadata_never_index` is the better of the two mechanisms because it lives on
the volume, so it protects every Mac the watch is plugged into. Adding the
volume under System Settings → Spotlight → Search Privacy covers this Mac
before the marker is read. `sudo mdutil -i off -d` also works but macOS has a
habit of re-enabling indexing for removable volumes on remount.

## Things worth knowing while you are in there

- **Plugging in USB terminates every running app**, and autostart relaunches on
  unplug — so a watch on charge records nothing. `MapManager` established this
  from the kernel's own log; see `MapManager/README.md`.
- **`diskutil list` misreports the partition** as `Windows_NTFS`. The mounted
  filesystem is exFAT; the partition type byte is what is wrong, not the volume.
- **`._`-prefixed files** appear beside anything copied from macOS. Harmless so
  far, but delete the ones you create rather than leaving them.
- **An APP_ID the kernel has never seen installs fine.** `Squash`'s changed
  once, which was suspected of blocking installs and was not the cause.
