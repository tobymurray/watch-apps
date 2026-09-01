#!/usr/bin/env python3
"""Look up a FIT field number, cross-checked against two independent sources.

    ./lookup.py session time_in_hr_zone
    ./lookup.py lap                      # every field in the message
    ./lookup.py session 57               # what actually lives at a number

WHY THIS FETCHES INSTEAD OF READING A VENDORED COPY

Garmin's profile is not in this repository and must not be. It ships under the
Flexible and Interoperable Data Transfer (FIT) Protocol License, which grants a
licence "for Licensee's internal business purposes" and then forbids, in section
2(c), making the Licensed Technology "available to any third party for any
reason" -- and in 2(d), distributing it "so that any part of it becomes subject
to any license that ... others have the right to modify". This repository is
public and MIT-licensed, which is precisely the licence 2(d) describes. Section
4 additionally designates the whole SDK as Garmin's Confidential Information.

So the profile is fetched on demand, by whoever is looking something up, under
their own acceptance of Garmin's terms. Nothing is cached into the tree.

The individual field numbers this repository relies on are recorded in
README.md, because those are facts about a wire format that every open-source
FIT decoder already carries -- not Garmin's file.
"""
import argparse
import importlib.util
import re
import sys
import tempfile
import urllib.request
from pathlib import Path

GARMIN = "https://raw.githubusercontent.com/garmin/fit-python-sdk/main/garmin_fit_sdk/profile.py"
FITPARSE = "https://raw.githubusercontent.com/dtcooper/python-fitparse/master/fitparse/profile.py"


def fetch(url, name):
    dest = Path(tempfile.gettempdir()) / f"fit_profile_{name}.py"
    if not dest.exists():
        print(f"fetching {name} profile...", file=sys.stderr)
        urllib.request.urlopen(url, timeout=60)  # fail fast on no network
        urllib.request.urlretrieve(url, dest)
    return dest


def garmin_messages():
    """{message_name: {field_num: {...}}} from Garmin's own generated profile."""
    path = fetch(GARMIN, "garmin")
    spec = importlib.util.spec_from_file_location("gp", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    profile = mod.Profile
    version = profile["version"]
    out = {}
    for msg in profile["messages"].values():
        out[msg["name"]] = msg["fields"]
    return out, f"Garmin FIT SDK {version['major']}.{version['minor']}.{version['patch']}{version['type']}"


def fitparse_messages():
    """{message_name: {field_num: field_name}}, parsed as text.

    A deliberately dumber reader than the one above: this is the cross-check,
    and it is worth less if it shares an implementation with what it checks.
    """
    src = fetch(FITPARSE, "fitparse").read_text()
    version = re.search(r"SDK VERSION ([\d.]+)", src)

    # Entries look like:  18: MessageType(\n        name='session',\n  fields={
    # ...and a Field( may carry a trailing comment before its name=, which is
    # what a naive pattern misses.
    starts = [(m.start(), m.group(1)) for m in
              re.finditer(r"^    \d+: MessageType\(\n\s*name='([a-z0-9_]+)'", src, re.M)]
    out = {}
    for i, (pos, name) in enumerate(starts):
        end = starts[i + 1][0] if i + 1 < len(starts) else len(src)
        body = src[pos:end]
        out[name] = {int(n): fn for n, fn in re.findall(
            r"(\d+):\s*Field\((?:\s*#[^\n]*)?\s*name='([a-z0-9_]+)'", body)}
    return out, f"python-fitparse (SDK {version.group(1) if version else '?'})"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("message", help="e.g. session, lap, record")
    ap.add_argument("field", nargs="?", help="field name, or a number to look up")
    args = ap.parse_args()

    g, g_ver = garmin_messages()
    f, f_ver = fitparse_messages()
    print(f"sources: {g_ver} | {f_ver}\n")

    if args.message not in g:
        sys.exit(f"no such message: {args.message}")
    fields = g[args.message]
    cross = f.get(args.message, {})

    def row(num, meta):
        name = meta["name"]
        other = cross.get(num)
        if other is None:
            agree = "(not in cross-check)"
        elif other == name:
            agree = "agrees"
        else:
            agree = f"*** DISAGREES: cross-check says {other!r} ***"
        scale = meta.get("scale")
        scale = scale[0] if isinstance(scale, list) and scale else scale
        print(f"  {num:<5} {name:<28} {str(meta.get('type')):<12} "
              f"scale={str(scale):<6} units={str(meta.get('units') or '-'):<6} "
              f"array={str(meta.get('array')):<6} {agree}")

    if args.field is None:
        print(f"--- {args.message}: all fields ---")
        for num in sorted(fields):
            row(num, fields[num])
        return

    if args.field.isdigit():
        num = int(args.field)
        if num not in fields:
            sys.exit(f"{args.message} has no field {num}")
        print(f"--- {args.message} field {num} ---")
        row(num, fields[num])
        return

    hits = [(n, m) for n, m in fields.items() if m["name"] == args.field]
    if not hits:
        sys.exit(f"{args.message} has no field named {args.field!r}. "
                 f"Run without a field name to list them all.")
    print(f"--- {args.message}.{args.field} ---")
    for num, meta in sorted(hits):
        row(num, meta)


if __name__ == "__main__":
    main()
