#!/usr/bin/env python3
"""Decode SensorLab's raw sample chunks.

    raw_decode.py runs/                      # summarise every chunk
    raw_decode.py runs/ --csv out.csv        # one row per sample
    raw_decode.py runs/ --type 0x10 --csv accel.csv
    raw_decode.py runs/ --verify runs/2.json # cross-check against a manifest

`raw/<run>-<seq>.bin` holds every batch the app received, verbatim -- the format
is specified in `Software/Libs/Header/Profile/RawLog.hpp`. This is the reader for
it, and the reason it exists is that **an analysis without its inputs cannot be
corrected**. `profile.json` holds conclusions and `runs/<id>.csv` holds
per-interval statistics; both embed the question somebody thought to ask. This
holds the samples, so a different question can be asked later.

Things you can get from here and from nowhere else:

  * the *shape* of a delivery gap -- did the stream stop, thin, or arrive in one
    burst? A `longest_gap_ms` of 4020 distinguishes none of those;
  * whether two sensors' samples are simultaneous, which needs both streams'
    timestamps side by side;
  * a field's whole trajectory, not its min, max and mean;
  * what a frame from one of the five parser-less types actually contains;
  * whether the profile's statistics are *right*, by recomputing them.

---------------------------------------------------------------------------
Field interpretation, and the one place this script guesses

A sample is a 4-byte `mTimeStamp`, a 4-byte `mTimeStampUs`, then N 4-byte
fields. The fields are a union -- float, uint32 or int32 -- and **the frame does
not say which**. So `--csv` emits each field three ways, as `fN_f`, `fN_u` and
`fN_i`, and lets you pick. That is deliberate: choosing for you would be
interpreting, and the whole point of the binary format is that the device did
not interpret either.

`--kinds` uses the SDK parsers' own accessors, via the generated type table, to
label which reading each field is *meant* to be. Meant, not is: for one of the
five types with no parser there is no answer, and for `HEART_RATE_EX`'s `SOURCE`
the parser reads a float where every other enum on the platform is a uint32.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import struct
import sys
from pathlib import Path

SCHEMA = 1
MAGIC = b"SLRW"
CHUNK_HEADER = 32
RECORD_HEADER = 16
DATA_HEADER = 8      # mTimeStamp + mTimeStampUs
FIELD = 4


class Truncated(Exception):
    """A chunk ended mid-record. Expected: the cable does not wait for a flush."""


def read_chunk(path: Path):
    """Yield (typeValue, handle, arrivalMs, count, stride, payload) per record.

    Stops at the first short record and reports how many bytes it ignored,
    rather than guessing at a partial frame -- a truncated final record is the
    normal outcome of a run that ended when the cable went in.
    """
    blob = path.read_bytes()
    if len(blob) < CHUNK_HEADER:
        raise ValueError(f"{path}: shorter than a chunk header")
    if blob[:4] != MAGIC:
        raise ValueError(f"{path}: not a SensorLab raw chunk (bad magic)")

    schema, run_id, seq, start_uptime = struct.unpack_from("<IIII", blob, 4)
    (start_wall,) = struct.unpack_from("<q", blob, 20)
    if schema != SCHEMA:
        raise ValueError(
            f"{path}: schema {schema}, this tool understands {SCHEMA}. "
            f"Use the version of raw_decode.py that shipped with the app."
        )

    header = {"run_id": run_id, "seq": seq, "start_uptime_ms": start_uptime,
              "start_wall_utc": start_wall, "bytes": len(blob)}

    records = []
    off = CHUNK_HEADER
    while off + RECORD_HEADER <= len(blob):
        type_value, handle, arrival = struct.unpack_from("<III", blob, off)
        count, stride = struct.unpack_from("<HH", blob, off + 12)
        payload_at = off + RECORD_HEADER
        payload_len = count * stride
        if payload_at + payload_len > len(blob):
            break
        records.append((type_value, handle, arrival, count, stride,
                        blob[payload_at:payload_at + payload_len]))
        off = payload_at + payload_len

    header["ignored_trailing_bytes"] = len(blob) - off
    return header, records


def fields_in(stride: int) -> int:
    """The frame's field count, from the stride -- `DataBatch`'s own arithmetic.

    Returns 0 for a stride that is not a whole number of fields, which is not an
    error to be smoothed over: it is the most interesting thing a raw log can
    contain, and the app records such a frame deliberately.
    """
    if stride < DATA_HEADER + FIELD:
        return 0
    extra = stride - (DATA_HEADER + FIELD)
    if extra % FIELD != 0:
        return 0
    return extra // FIELD + 1


def samples_of(payload: bytes, count: int, stride: int):
    """Yield (tsMs, tsUs, [raw 4-byte words]) per sample."""
    for i in range(count):
        at = i * stride
        ts_ms, ts_us = struct.unpack_from("<II", payload, at)
        words = []
        off = at + DATA_HEADER
        while off + FIELD <= at + stride:
            words.append(payload[off:off + FIELD])
            off += FIELD
        yield ts_ms, ts_us, words


def load_type_table(app_root: Path):
    """Field kinds per type, parsed out of the generated table.

    A tiny parse rather than a code generator: the table is C++ and this only
    needs the type value and each field's declared kind, for `--kinds`.
    """
    gen = app_root / "Software/Libs/Header/Catalogue/SensorTypeTable.generated.hpp"
    if not gen.exists():
        return {}

    import re
    text = gen.read_text(encoding="utf-8")

    field_arrays: dict[str, list[tuple[str, str]]] = {}
    for m in re.finditer(r"constexpr FieldSpec kFields(\w+)\[\] = \{(.*?)\};",
                         text, re.DOTALL):
        rows = re.findall(r'\{\s*"([^"]*)",\s*FieldKind::(\w+)', m.group(2))
        field_arrays[m.group(1)] = rows

    parsers: list[str] = []
    m = re.search(r"constexpr ParserSpec kParsers\[\] = \{(.*?)\};", text, re.DOTALL)
    if m:
        parsers = re.findall(r'\{\s*"([^"]*)"', m.group(1))

    out: dict[int, dict] = {}
    m = re.search(r"constexpr TypeSpec kTypes\[\] = \{(.*?)\};", text, re.DOTALL)
    if m:
        for row in re.finditer(
                r'\{\s*"([^"]*)",\s*(0x[0-9A-Fa-f]+)u,\s*"(?:[^"\\]|\\.)*",\s*(\w+|\d+)',
                m.group(1)):
            name, value, parser = row.group(1), int(row.group(2), 16), row.group(3)
            entry = {"name": name, "fields": []}
            if parser != "kNoParser" and parser.isdigit():
                idx = int(parser)
                if idx < len(parsers):
                    entry["parser"] = parsers[idx]
                    entry["fields"] = field_arrays.get(parsers[idx], [])
            out[value] = entry
    return out


def summarise(chunks, table):
    per_type: dict[int, dict] = {}
    total_batches = total_samples = 0
    ignored = 0

    for header, records in chunks:
        ignored += header["ignored_trailing_bytes"]
        for type_value, handle, arrival, count, stride, payload in records:
            t = per_type.setdefault(type_value, {
                "batches": 0, "samples": 0, "handles": set(),
                "strides": set(), "first_arrival": arrival,
                "last_arrival": arrival, "first_ts": None, "last_ts": None,
                "nonmonotonic": 0, "us_over_999": 0,
            })
            t["batches"] += 1
            t["samples"] += count
            t["handles"].add(handle)
            t["strides"].add(stride)
            t["last_arrival"] = arrival
            for ts_ms, ts_us, _w in samples_of(payload, count, stride):
                if t["first_ts"] is None:
                    t["first_ts"] = ts_ms
                elif ((ts_ms - t["last_ts"]) & 0xFFFFFFFF) > 0x7FFFFFFF:
                    # Unsigned wrap-safe: a difference in the top half of the
                    # range is a step backwards, not a 49-day jump.
                    t["nonmonotonic"] += 1
                if ts_us > 999:
                    t["us_over_999"] += 1
                t["last_ts"] = ts_ms
            total_batches += 1
            total_samples += count

    print(f"{len(chunks)} chunk(s), {total_batches} batches, "
          f"{total_samples} samples")
    if ignored:
        print(f"{ignored} trailing byte(s) ignored -- a truncated final record, "
              f"which is what a run ending at a cable event looks like")
    print()
    print(f"{'type':>7}  {'name':<26} {'batches':>8} {'samples':>9} "
          f"{'fields':>6} {'nonmono':>8} {'us>999':>7}")
    for value in sorted(per_type):
        t = per_type[value]
        info = table.get(value, {})
        strides = sorted(t["strides"])
        fields = "/".join(str(fields_in(s)) or "?" for s in strides)
        # More than one stride for one type inside one run is `RUNNING_CADENCE`'s
        # 4 -> 2 shrink happening live, and it is worth shouting about.
        flag = "  <-- STRIDE CHANGED" if len(strides) > 1 else ""
        print(f"{value:>#7x}  {info.get('name', '?'):<26} "
              f"{t['batches']:>8} {t['samples']:>9} {fields:>6} "
              f"{t['nonmonotonic']:>8} {t['us_over_999']:>7}{flag}")
        if len(t["handles"]) > 1:
            print(f"         handles: {sorted(t['handles'])}  <-- MORE THAN ONE")
    return per_type


def write_csv(chunks, table, out_path: Path, only_type, kinds: bool):
    """One row per sample, every field three ways.

    Three ways because the frame does not say which member of the union a field
    is, and choosing for you would be interpreting. `--kinds` adds a header
    comment naming what each field is *meant* to be per the SDK's own parser.
    """
    widest = 0
    for _h, records in chunks:
        for _tv, _hd, _a, _c, stride, _p in records:
            widest = max(widest, max(0, (stride - DATA_HEADER) // FIELD))

    with out_path.open("w", encoding="utf-8") as fh:
        if kinds:
            fh.write("# field kinds per type, from the SDK parsers' own "
                     "accessors (meant, not measured):\n")
            for value in sorted(table):
                fs = table[value].get("fields") or []
                if not fs:
                    continue
                labels = ", ".join(f"f{i}={n}:{k}"
                                   for i, (n, k) in enumerate(fs))
                fh.write(f"#   {value:#x} {table[value]['name']}: {labels}\n")
            fh.write("# a type with no line above ships no parser: its field "
                     "semantics are undocumented anywhere.\n")

        cols = ["chunk_seq", "type", "name", "handle", "arrival_ms",
                "batch_index", "sample_index", "ts_ms", "ts_us", "stride",
                "fields"]
        for i in range(widest):
            cols += [f"f{i}_f", f"f{i}_u", f"f{i}_i"]
        fh.write(",".join(cols) + "\n")

        batch_index = 0
        for header, records in chunks:
            for type_value, handle, arrival, count, stride, payload in records:
                if only_type is not None and type_value != only_type:
                    batch_index += 1
                    continue
                name = table.get(type_value, {}).get("name", "")
                nfields = fields_in(stride)
                for si, (ts_ms, ts_us, words) in enumerate(
                        samples_of(payload, count, stride)):
                    row = [str(header["seq"]), f"{type_value:#x}", name,
                           str(handle), str(arrival), str(batch_index), str(si),
                           str(ts_ms), str(ts_us), str(stride),
                           str(nfields) if nfields else "malformed"]
                    for i in range(widest):
                        if i < len(words):
                            w = words[i]
                            row += [repr(struct.unpack("<f", w)[0]),
                                    str(struct.unpack("<I", w)[0]),
                                    str(struct.unpack("<i", w)[0])]
                        else:
                            row += ["", "", ""]
                    fh.write(",".join(row) + "\n")
                batch_index += 1
    print(f"wrote {out_path}")


def verify(chunks, manifest_path: Path) -> int:
    """Cross-check the chunks against the run manifest's own counts.

    The manifest is written by the app and the chunks by the app, so agreeing
    proves only internal consistency -- but disagreeing proves something is
    wrong, and `dropped_batches` is how the app admits to a gap that the files
    themselves cannot show.
    """
    with manifest_path.open(encoding="utf-8") as fh:
        doc = json.load(fh)
    raw = doc.get("manifest", {}).get("raw")
    if raw is None:
        print(f"{manifest_path}: no `raw` block -- written by a build before "
              f"raw capture existed")
        return 2

    batches = sum(len(r) for _h, r in chunks)
    samples = sum(c for _h, r in chunks
                  for (_t, _hd, _a, c, _s, _p) in r)

    ok = True
    print(f"manifest says   {raw['batches']} batches, {raw['samples']} samples, "
          f"{raw['chunks']} chunks, {raw['bytes']} bytes")
    print(f"chunks contain  {batches} batches, {samples} samples, "
          f"{len(chunks)} chunks")

    if int(raw["batches"]) != batches:
        print("!! batch counts disagree")
        ok = False
    if int(raw["samples"]) != samples:
        print("!! sample counts disagree")
        ok = False

    dropped = int(raw.get("dropped_batches", 0))
    if dropped:
        print(f"!! the app dropped {dropped} batches: **this raw log does not "
              f"contain everything the run saw**"
              + (" (byte cap reached)" if raw.get("cap_reached") else "")
              + (f" ({raw['write_failures']} write failures)"
                 if raw.get("write_failures") else ""))
        ok = False
    elif not raw.get("capture"):
        print("raw capture was off for this run -- a valid run, and it measures "
              "the sensor layer without the flash traffic capture adds")
    else:
        print("the app reports nothing dropped, so this raw log is complete")

    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", type=Path,
                    help="a raw/ directory, a chunk, or a collection directory")
    ap.add_argument("--csv", type=Path, help="write one row per sample here")
    ap.add_argument("--type", help="only this sensor type, e.g. 0x10")
    ap.add_argument("--kinds", action="store_true",
                    help="label field kinds from the SDK parsers in the CSV")
    ap.add_argument("--verify", type=Path, metavar="RUN.json",
                    help="cross-check against a run manifest")
    ap.add_argument("--app-root", type=Path,
                    default=Path(__file__).resolve().parent.parent,
                    help="SensorLab root, for the generated type table")
    args = ap.parse_args()

    if args.path.is_dir():
        files = sorted(glob.glob(str(args.path / "*.bin")))
        if not files:
            files = sorted(glob.glob(str(args.path / "raw" / "*.bin")))
    else:
        files = [str(args.path)]

    if not files:
        print(f"no .bin chunks under {args.path}", file=sys.stderr)
        return 2

    # Chunk order matters: records within a run are chronological across chunks,
    # and "1-10.bin" sorts before "1-2.bin" as a string.
    def seq_of(p: str) -> tuple:
        stem = os.path.basename(p).rsplit(".", 1)[0]
        parts = stem.split("-")
        try:
            return (int(parts[0]), int(parts[1]))
        except (IndexError, ValueError):
            return (0, 0)

    chunks = []
    for f in sorted(files, key=seq_of):
        try:
            chunks.append(read_chunk(Path(f)))
        except ValueError as e:
            print(f"skipping: {e}", file=sys.stderr)

    if not chunks:
        return 2

    table = load_type_table(args.app_root)
    only = int(args.type, 16) if args.type else None

    rc = 0
    if args.verify:
        rc = verify(chunks, args.verify)
        print()

    summarise(chunks, table)

    if args.csv:
        write_csv(chunks, table, args.csv, only, args.kinds)

    return rc


if __name__ == "__main__":
    raise SystemExit(main())
