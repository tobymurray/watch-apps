#!/usr/bin/env python3
"""Pull SensorLab's profile and run logs off the watch over BLE.

Why BLE rather than "just plug it in": **plugging in USB terminates every
running app.** For most apps that is an inconvenience. For this one it destroys
the thing being collected -- a soak is the run whose distributions are the
measurement, and fetching its files over USB ends it mid-run and marks it
`truncated_by_usb`. There is no way to collect a run over the cable without
becoming the reason it stopped.

    python3 pull_profile.py E8:DF:D5:49:4C:40 --out SensorLab/Profiles/1.4.0-2026-08-21

By default it fetches every profile, then the run manifests, then the run logs
newest-first, skipping anything already present. `--list` shows what is there and
stops; `--profiles-only` skips the raw logs, which are the bulk.

---------------------------------------------------------------------------
This is a thin wrapper, deliberately

The protocol work and the client are not this repository's: they are
`prototype/una_ble_client.py` from the `2026-07-29-hardware-config-recovery`
investigation on `una-sdk@research`, which is validated against a real watch and
pulls files with matching CRC-16. It is *referenced* rather than copied, so there
is one copy of it to keep correct -- the same call `FwDump` made about
`reassemble_dump.py` and SleepLab about `pull_nights.py`.

Point `--client` (or `$UNA_BLE_CLIENT`) at a checkout of it:

    cd /path/to/una-sdk
    git show research:Docs/Investigations/2026-07-29-hardware-config-recovery/prototype/una_ble_client.py \\
        > /tmp/una_ble_client.py

---------------------------------------------------------------------------
What it needs, and what it cannot do about it

Everything the underlying client needs: Linux with BlueZ, an adapter, and
`dbus_fast`. The File Transfer Service requires a **bonded, encrypted link**, so
the watch must already be paired via `bluetoothctl` -- this does not pair, because
BlueZ's SMP passkey flow is far easier driven interactively.

The watch's advertising window is short. If a command times out, wake it and
retry; the fetch is resumable in the only sense that matters, since it skips
files it already has.

Windowed reads need protocol version >= 5. This client does not use them -- it
requests one chunk at a time, which is what the prototype found actually works on
this firmware -- so a profile at a few hundred KB is a few thousand round trips.
**It is slow.** A full profile is roughly 1 974 claims at ~220 bytes, so ~430 KB,
and the run logs are larger. `--profiles-only` exists because the profile is the
document and the logs are the evidence behind it: fetch the document first, and
the evidence when an analysis turns out to need re-doing.

**Order matters here.** The profile is fetched before the run logs, because a
profile without its logs is still a readable document while logs without their
profile are a heap of CSV. And `runs/*.json` are fetched before `runs/*.csv`
because a manifest is a few hundred bytes and a raw log is not: if the transfer
dies, the manifests are what make the partially-fetched logs interpretable.

**Never verified against a watch.** SleepLab's ledger row T4 says the same of
`pull_nights.py`, and it is still true: the underlying client is validated for
`.fit` files under `Apps/GpsLab/` with matching CRC-16, and nothing in the
protocol is path-specific, but neither wrapper has been run against a device.
"""

import argparse
import asyncio
import importlib.util
import os
import sys

DEFAULT_REMOTE = "/Apps/SensorLab/"


def load_client(path):
    """Import the validated BLE client from wherever it has been put."""
    if path is None:
        path = os.environ.get("UNA_BLE_CLIENT")
    if path is None:
        for candidate in ("una_ble_client.py",
                          os.path.join(os.path.dirname(__file__), "una_ble_client.py")):
            if os.path.exists(candidate):
                path = candidate
                break
    if path is None or not os.path.exists(path):
        sys.exit(
            "cannot find una_ble_client.py.\n"
            "\n"
            "It is not vendored here on purpose -- there should be one copy of it\n"
            "to keep correct, and it lives on una-sdk's research branch. Extract\n"
            "one with:\n"
            "\n"
            "  cd /path/to/una-sdk && git show \\\n"
            "    research:Docs/Investigations/2026-07-29-hardware-config-recovery/"
            "prototype/una_ble_client.py \\\n"
            "    > /tmp/una_ble_client.py\n"
            "\n"
            "then pass --client /tmp/una_ble_client.py or set $UNA_BLE_CLIENT."
        )

    spec = importlib.util.spec_from_file_location("una_ble_client", path)
    mod = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(mod)
    except ImportError as e:
        sys.exit(f"{path} could not be imported ({e}).\n"
                 "It needs Linux BlueZ and `pip install dbus_fast`.")
    return mod


async def fetch(client, bus, char, remote, local, force):
    """Fetch one file unless it is already here. Returns bytes written, or 0."""
    if not force and os.path.exists(local) and os.path.getsize(local) > 0:
        return 0

    data = await client.read_file(bus, char, remote)
    if data is None:
        print(f"  !! {remote}: read failed")
        return 0

    os.makedirs(os.path.dirname(local) or ".", exist_ok=True)
    # Written to a temporary name and renamed, so an interrupted fetch never
    # leaves a truncated file that the next run would then skip as "already
    # here". The advertising window is short and interruptions are normal.
    tmp = local + ".part"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, local)
    return len(data)


async def names_in(client, bus, char, remote):
    entries = await client.list_dir(bus, char, remote)
    return [name for (_i, _t, _a, name) in entries if name not in (".", "..")]


async def run(args):
    client = load_client(args.client)

    bus = await client.MessageBus(bus_type=client.BusType.SYSTEM,
                                  negotiate_unix_fd=True).connect()
    char = await client.find_fts_characteristic(bus, args.address)

    top = await names_in(client, bus, char, args.remote)
    if not top:
        print(f"{args.remote} is empty or unreadable. Has the app been opened?")
        return

    try:
        runs = await names_in(client, bus, char, args.remote + "runs/")
    except Exception:
        runs = []

    if args.list:
        for name in sorted(top):
            print(f"  {name}")
        for name in sorted(runs):
            print(f"  runs/{name}")
        return

    os.makedirs(args.out, exist_ok=True)
    total = 0

    # The profiles first: they are the document, and each is named with the
    # firmware version it describes -- which is what `profile_diff.py` keys on.
    profiles = sorted(n for n in top
                      if n.startswith("profile-") and n.endswith(".json"))
    if not profiles:
        print("no profile-*.json on the watch. The app writes one when a run "
              "closes; open it and let the existence sweep finish.")
    for name in profiles:
        n = await fetch(client, bus, char, args.remote + name,
                        os.path.join(args.out, name), args.all)
        print(f"  {name}  {'(have it)' if n == 0 else f'{n} bytes'}")
        total += n

    # state.json, which says which run was open and what the run counter is at.
    # Small, and it is how a truncated collection is understood afterwards.
    if "state.json" in top:
        n = await fetch(client, bus, char, args.remote + "state.json",
                        os.path.join(args.out, "state.json"), force=True)
        print(f"  state.json  {n} bytes")
        total += n

    if args.profiles_only:
        print(f"\n{total} bytes into {args.out}")
        print("Raw run logs skipped. Fetch them with --all when an analysis "
              "needs re-doing against its inputs.")
        return

    # Manifests before logs: a manifest is a few hundred bytes and a raw log is
    # not, so if the transfer dies the manifests are what make the
    # partially-fetched logs interpretable at all.
    manifests = sorted((n for n in runs if n.endswith(".json")),
                       key=lambda s: -int(s.split(".")[0]) if s.split(".")[0].isdigit() else 0)
    for name in manifests:
        n = await fetch(client, bus, char, args.remote + "runs/" + name,
                        os.path.join(args.out, "runs", name), args.all)
        if n:
            print(f"  runs/{name}  {n} bytes")
            total += n

    # Newest first, so an interrupted run has fetched the logs somebody actually
    # wants to look at.
    logs = sorted((n for n in runs if n.endswith(".csv")),
                  key=lambda s: -int(s.split(".")[0]) if s.split(".")[0].isdigit() else 0)
    if args.limit:
        logs = logs[:args.limit]

    for name in logs:
        n = await fetch(client, bus, char, args.remote + "runs/" + name,
                        os.path.join(args.out, "runs", name), args.all)
        if n == 0:
            print(f"  runs/{name}  (have it)")
        else:
            print(f"  runs/{name}  {n} bytes")
            total += n

    print(f"\n{total} bytes into {args.out}")
    print("Nothing was stopped. A soak that was running is still running, and "
          "nothing about it was lost to fetching it.")
    print(f"\nNext: python3 profile_report.py {args.out}/profile-*.json "
          f"-o SENSOR-PROFILE.md")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("address", help="the watch's BLE address, e.g. E8:DF:D5:49:4C:40")
    ap.add_argument("--out", default="./profile", help="where to put them")
    ap.add_argument("--remote", default=DEFAULT_REMOTE,
                    help=f"directory on the watch (default {DEFAULT_REMOTE})")
    ap.add_argument("--client", default=None,
                    help="path to una_ble_client.py from una-sdk@research")
    ap.add_argument("--all", action="store_true",
                    help="re-fetch files already present locally")
    ap.add_argument("--list", action="store_true",
                    help="show what is on the watch and stop")
    ap.add_argument("--profiles-only", action="store_true",
                    help="skip the raw run logs, which are the bulk")
    ap.add_argument("--limit", type=int, default=0,
                    help="fetch at most N run logs, newest first")
    asyncio.run(run(ap.parse_args()))


if __name__ == "__main__":
    main()
