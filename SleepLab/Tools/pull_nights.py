#!/usr/bin/env python3
"""Pull SleepLab's nights off the watch over BLE, leaving the service running.

Why this exists rather than "just plug it in": **plugging in USB terminates
every running app** and autostart relaunches them on unplug. So fetching last
night's files over USB kills the recorder, and doing it in the morning while a
session is still open costs the tail of the night. BLE leaves everything
running.

    python3 pull_nights.py E8:DF:D5:49:4C:40 --out ./nights

By default it fetches only what is new: the index, then any night whose `.csv`
and `.json` are not already in the output directory. `--all` re-fetches
everything, `--list` only shows what is there.

---------------------------------------------------------------------------
This is a thin wrapper, deliberately

The protocol work and the client are not this repo's: they are
`prototype/una_ble_client.py` from the `2026-07-29-hardware-config-recovery`
investigation on `una-sdk@research`, which is validated against a real watch
and pulls `.fit` files with matching CRC-16. It is *referenced* rather than
copied, so there is one copy of it to keep correct -- the same call `FwDump`
made about `reassemble_dump.py`.

Point `--client` (or `$UNA_BLE_CLIENT`) at a checkout of it. To extract one:

    cd /path/to/una-sdk
    git show research:Docs/Investigations/2026-07-29-hardware-config-recovery/prototype/una_ble_client.py \\
        > /tmp/una_ble_client.py

---------------------------------------------------------------------------
What it needs, and what it cannot do about it

Everything the underlying client needs: Linux with BlueZ, an adapter, and
`dbus_fast`. The File Transfer Service requires a **bonded, encrypted link**,
so the watch must already be paired via `bluetoothctl` -- this does not pair,
because BlueZ's SMP passkey flow is far easier driven interactively.

The watch's advertising window is short. If a command times out, wake it and
retry; the fetch is resumable in the only sense that matters, since it skips
files it already has.

Windowed reads need protocol version >= 5. This client does not use them -- it
requests one chunk at a time, which is what the prototype found actually works
on this firmware -- so a night's CSV at ~46 KB is a few hundred round trips. It
is not fast. It is slower than USB and it does not stop the recorder, which is
the entire trade.
"""

import argparse
import asyncio
import importlib.util
import os
import sys

DEFAULT_REMOTE = "/Apps/SleepLab/Nights/"


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

    # Written to a temporary name and renamed, so an interrupted fetch never
    # leaves a truncated file that the next run would then skip as "already
    # here". The advertising window is short and interruptions are normal.
    tmp = local + ".part"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, local)
    return len(data)


async def run(args):
    client = load_client(args.client)

    bus = await client.MessageBus(bus_type=client.BusType.SYSTEM,
                                  negotiate_unix_fd=True).connect()
    char = await client.find_fts_characteristic(bus, args.address)

    entries = await client.list_dir(bus, char, args.remote)
    names = [name for (_i, _t, _a, name) in entries
             if name not in (".", "..")]
    if not names:
        print(f"{args.remote} is empty or unreadable. Has a night been recorded?")
        return

    if args.list:
        for name in sorted(names):
            print(f"  {name}")
        return

    os.makedirs(args.out, exist_ok=True)

    # The index first and always re-fetched: it grows by a row a night, it is
    # the cheapest file here, and it is the one that says what the others are.
    total = 0
    if "index.csv" in names:
        n = await fetch(client, bus, char, args.remote + "index.csv",
                        os.path.join(args.out, "index.csv"), force=True)
        print(f"  index.csv  {n} bytes")
        total += n

    # Newest first, so an interrupted run has fetched the nights somebody
    # actually wants to look at.
    nights = sorted((n for n in names if n != "index.csv"), reverse=True)
    if args.limit:
        nights = nights[:args.limit]

    for name in nights:
        n = await fetch(client, bus, char, args.remote + name,
                        os.path.join(args.out, name), args.all)
        if n == 0:
            print(f"  {name}  (have it)")
        else:
            print(f"  {name}  {n} bytes")
            total += n

    print(f"\n{total} bytes into {args.out}")
    print("The service was never stopped. Nothing about the night was lost to "
          "fetching it.")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("address", help="the watch's BLE address, e.g. E8:DF:D5:49:4C:40")
    ap.add_argument("--out", default="./nights", help="where to put them")
    ap.add_argument("--remote", default=DEFAULT_REMOTE,
                    help=f"directory on the watch (default {DEFAULT_REMOTE})")
    ap.add_argument("--client", default=None,
                    help="path to una_ble_client.py from una-sdk@research")
    ap.add_argument("--all", action="store_true",
                    help="re-fetch files already present locally")
    ap.add_argument("--list", action="store_true",
                    help="show what is on the watch and stop")
    ap.add_argument("--limit", type=int, default=0,
                    help="fetch at most N nights, newest first")
    asyncio.run(run(ap.parse_args()))


if __name__ == "__main__":
    main()
