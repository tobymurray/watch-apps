#!/usr/bin/env python3
"""Render a SensorLab profile into a Markdown report.

    profile_report.py profile-1.4.0.json [-o SENSOR-PROFILE.md]

The report is written **to be read by somebody who does not use this
repository**: an UNA engineer, or the author of the next app. It states the
device, the firmware, the date, the method, the completeness and the
limitations, and it does not require reading five other documents first.

Four sections, in the order a reader needs them:

  1. **What this is**, and how much of it is known. Completeness first, always,
     because a profile that is 40 % complete and says so is a useful document
     and one that is 40 % complete and looks finished is a liability.
  2. **Findings** -- the rows that contradict a spec, or that are surprising on
     their own. The point of the exercise.
  3. **Per sensor** -- the full claim table.
  4. **What is still UNVERIFIED and what would settle it** -- the reader's
     to-do list, generated from the claims' own method ids.

Numbers in the profile are decimal *strings*, not JSON numbers: the watch's
newlib may not link floating-point `printf`, and two of the SDK's own integer
JSON paths are broken on 64-bit builds. See the normative format note in
`Software/Libs/Header/Profile/ProfileWriter.hpp`. `float()` reads them either
way, which is what `number()` below does.
"""

from __future__ import annotations

import argparse
import datetime
import json
import sys
from pathlib import Path

SCHEMA = 1

# Verdicts, in the order a reader cares about them.
VERDICT_ORDER = ["REFUTED", "CONFIRMED", "LIKELY", "UNVERIFIED", "INAPPLICABLE"]


# ---------------------------------------------------------------------------
# Decoding
# ---------------------------------------------------------------------------

def number(v):
    """Decode a profile value.

    A decimal string, one of the non-finite names, a JSON number, or None. A
    two-element list is the pre-2026-08-21 mantissa/exponent form and is still
    read, so an early profile stays diffable. Anything else is a format the
    writer should not produce, and it is reported rather than coerced.
    """
    if v is None:
        return None
    if isinstance(v, str):
        named = {"nan": float("nan"), "+inf": float("inf"),
                 "-inf": float("-inf"), "zero": 0.0}
        if v in named:
            return named[v]
        try:
            return float(v)
        except ValueError:
            return None
    if isinstance(v, (int, float)):
        # A JSON number: counts are written this way.
        return float(v)
    if isinstance(v, list) and len(v) == 2:
        # Schema-1 profiles written before 2026-08-21 carried values as
        # [mantissa, exponent]. Still read, so an early profile stays diffable.
        return float(v[0]) * (10.0 ** int(v[1]))
    raise ValueError(f"unrecognised value {v!r}")


def fmt(v, unit="", places=6):
    """A number a person can read, with its unit."""
    n = number(v)
    if n is None:
        return "--"
    if n != n:
        return "NaN"
    if n in (float("inf"), float("-inf")):
        return "+inf" if n > 0 else "-inf"
    if n == int(n) and abs(n) < 1e15:
        s = str(int(n))
    else:
        s = f"{n:.{places}g}"
    return f"{s} {unit}".strip()


def pct(c):
    return f"{c.get('percent', 0)} %"


# ---------------------------------------------------------------------------
# Reading
# ---------------------------------------------------------------------------

def load(path: Path) -> dict:
    with path.open(encoding="utf-8") as fh:
        doc = json.load(fh)
    schema = doc.get("schema")
    if schema != SCHEMA:
        # Refuse rather than guess. A renamed key read as absent is how a report
        # comes out confidently wrong.
        raise SystemExit(
            f"{path}: schema {schema}, this tool understands {SCHEMA}. "
            f"Update profile_report.py, or use the version that wrote the file."
        )
    return doc


# ---------------------------------------------------------------------------
# Findings
# ---------------------------------------------------------------------------

def findings(doc: dict) -> list[str]:
    """The rows worth a reader's attention, most consequential first.

    Everything here is derived from the profile rather than hard-coded, so a
    finding that stops being true stops appearing.
    """
    out: list[str] = []
    # Absent, silent and stuck are three different findings with three different
    # causes, so they are three separate lines -- but a dozen absent types is one
    # line naming a dozen sensors, not a dozen lines.
    absent: list[str] = []
    silent: list[str] = []
    m = doc["manifest"]

    # The primary key. A profile that cannot be diffed answers none of the
    # questions this app exists for, so this is the first thing said.
    if not m.get("firmware_read_from_kernel"):
        if m.get("firmware"):
            out.append(
                f"**The firmware version was declared, not read.** "
                f"`{m['firmware']}` came from `settings.json`; the kernel did "
                f"not answer `RequestSystemInfo`. Every comparison keys on this "
                f"field, so treat it as a label rather than as evidence."
            )
        else:
            out.append(
                "**The firmware version is unknown.** The kernel did not answer "
                "`RequestSystemInfo` and `settings.json` declared nothing. "
                "**This profile cannot be diffed against another.**"
            )

    end = m.get("end")
    if end and end not in ("completed",):
        out.append(
            f"**The run that produced this ended `{end}`.** Its distributions "
            f"are shorter than they look. Plugging in USB terminates every "
            f"running app, so a profiling run cannot be watched over the cable."
        )
    if m.get("saw_charging"):
        out.append(
            "**The cable was detected during this run.** Anything measured "
            "after that point was measured on charge."
        )

    undocumented = [s for s in doc["sensors"] if s.get("missing_from_doc")]
    if undocumented:
        names = ", ".join(f"`{s['name']}` ({s['type']})" for s in undocumented)
        out.append(
            f"**{len(undocumented)} sensor types are absent from "
            f"`Docs/SensorsLayer.md` entirely**, though `SensorTypes.hpp` "
            f"declares them: {names}. This is a documentation gap rather than a "
            f"behavioural one, and it is the cheapest thing in this report to "
            f"act on."
        )

    noparser = [s for s in doc["sensors"] if s.get("parser") is None]
    if noparser:
        names = ", ".join(f"`{s['name']}` ({s['type']})" for s in noparser)
        out.append(
            f"**{len(noparser)} types ship no data parser**: {names}. For these, "
            f"the delivered field count and per-field behaviour below are the "
            f"only description of the frame that exists anywhere -- and the "
            f"field *semantics* are inferred, not documented."
        )

    hazards = [s for s in doc["sensors"] if s.get("parser_reads_before_count")]
    for s in hazards:
        out.append(
            f"**`{s['parser']}::isDataValid()` reads a field before it checks "
            f"the field count.** On a short frame that is an out-of-bounds read: "
            f"`&&` short-circuits left to right and `DataView`'s bounds assert is "
            f"compiled out at `-Os`. Affects `{s['name']}` ({s['type']})."
        )

    atleast = [s for s in doc["sensors"]
               if s.get("parser_validity") == "at_least"]
    exact = [s for s in doc["sensors"] if s.get("parser_validity") == "exact"]
    if atleast and exact:
        lenient = sorted({s["parser"] for s in atleast})
        strict = sorted({s["parser"] for s in exact})
        out.append(
            f"**{len(strict)} of {len(strict) + len(lenient)} shipped parsers "
            f"test the delivered field count for exact equality**, so a single "
            f"appended field silently invalidates every sample they read. The "
            f"exception"
            + ("s are " if len(lenient) != 1 else " is ")
            + ", ".join(f"`{p}`" for p in lenient)
            + ", which uses `>=` deliberately so a future kernel can extend the "
              "frame. The asymmetry is the finding: one appended field would be "
              "a no-op for some apps on this platform and total data loss for "
              "the rest."
        )

    # Per-claim findings.
    for s in doc["sensors"]:
        for c in s["claims"]:
            cid = c["claim_id"]
            v = c["verdict"]

            if v == "REFUTED":
                out.append(
                    f"**REFUTED: `{cid}`.** {claim_line(c)} "
                    f"{c.get('notes') or ''}".strip()
                )
                continue

            if c.get("conformance") == "DIFFERS" and v == "CONFIRMED":
                out.append(
                    f"**Does not conform: `{cid}`.** Measured "
                    f"{fmt(c['value'], c.get('unit', ''))} against "
                    f"{fmt(c.get('expected'))} from "
                    f"`{c.get('expected_source') or 'an unnamed source'}` "
                    f"(n = {c['n']}, {c['method_id']})."
                )
                continue

            if v != "CONFIRMED":
                continue

            n = number(c.get("value"))
            if n is None:
                continue

            # A field that never changed. Not broken enough to be absent, not
            # working enough to be usable -- the hardest failure mode to notice.
            if cid.endswith("_ever_changed") and n == 0:
                field = cid.split(".")[-1].replace("_ever_changed", "")
                out.append(
                    f"**`{s['name']}` field {field} is stuck.** It never changed "
                    f"across {c['n']} samples. A sensor that is not absent and "
                    f"not working is the hardest kind to notice: check "
                    f"`{cid.replace('_ever_changed', '_stuck_max_run')}` for how "
                    f"long the run was."
                )
            if cid.endswith(".existence.default_resolves") and n == 0:
                absent.append(f"`{s['name']}` ({s['type']})")
            if cid.endswith(".liveness.samples_per_min") and n == 0:
                silent.append(f"`{s['name']}` ({s['type']})")
            if cid.endswith(".timing.ts_us_over_999") and n > 0:
                out.append(
                    f"**`{s['name']}`'s `mTimeStampUs` exceeded 999 on "
                    f"{int(n)} samples.** `DataView::getTimestampUs()` computes "
                    f"`mTimeStamp * 1000 + mTimeStampUs`, which assumes it never "
                    f"does. If this is real, every microsecond timestamp in "
                    f"every app on this platform is wrong."
                )
            if cid.endswith(".timing.ts_monotonic") and n == 0:
                out.append(
                    f"**`{s['name']}`'s sample timestamps go backwards.** "
                    f"A pipeline that reorders is a finding; nothing in this app "
                    f"sorts them, so the raw log carries the evidence."
                )
            if cid.endswith("_nonfinite") and n > 0:
                out.append(
                    f"**`{cid}` saw {int(n)} non-finite samples.** "
                    f"One NaN once poisoned every subsequent epoch of another app "
                    f"on this platform to exactly zero."
                )

    if absent:
        out.append(
            f"**{len(absent)} types resolve no driver at all.** "
            f"`RequestDefault` returned nothing to subscribe to, so there is no "
            f"producer for them on this firmware: " + ", ".join(sorted(absent))
            + ". These are measured negatives, not untested rows -- which is the "
              "distinction that makes them useful."
        )
    if silent:
        out.append(
            f"**{len(silent)} types resolved a driver and then delivered "
            f"nothing**: " + ", ".join(sorted(silent))
            + ". A different finding from having no driver, with a different "
              "cause -- and for an event sensor it may be the correct behaviour, "
              "so check the cadence column before reading it as a fault."
        )

    # Deduplicate while keeping order: several sensors can produce the same
    # sentence about a shared parser.
    seen: set[str] = set()
    unique = []
    for line in out:
        if line not in seen:
            seen.add(line)
            unique.append(line)
    return unique


def claim_line(c: dict) -> str:
    bits = []
    if c.get("value") is not None:
        bits.append(fmt(c["value"], c.get("unit", "")))
    sp = c.get("spread")
    if sp:
        bits.append(
            f"(p05 {fmt(sp['p05'])} / p50 {fmt(sp['p50'])} / p95 {fmt(sp['p95'])})"
        )
    bits.append(f"n = {c['n']}")
    bits.append(f"`{c['method_id']}`")
    return ", ".join(bits)


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

def render(doc: dict, source: Path) -> str:
    m = doc["manifest"]
    overall = doc["completeness"]["overall"]
    L: list[str] = []

    fw = m.get("firmware") or "unknown"
    L.append(f"# UNA Watch sensor profile -- firmware {fw}")
    L.append("")
    L.append(
        "Measured on one physically-owned UNA Watch by **SensorLab**, an "
        "instrument app built out of tree against the UNA SDK. Unofficial; not "
        "affiliated with, endorsed or sponsored by UNA Watch Ltd."
    )
    L.append("")
    L.append(f"Generated {datetime.date.today().isoformat()} from `{source.name}`.")
    L.append("")

    # -- Read this first ----------------------------------------------------
    L.append("## Completeness -- how much of this is known")
    L.append("")
    L.append(
        f"**{pct(overall)} complete**: {overall['answered']} of "
        f"{overall['applicable']} applicable claims have an answer. "
        f"{overall['confirmed']} confirmed, {overall['likely']} likely, "
        f"{overall['refuted']} refuted, "
        f"{overall['applicable'] - overall['answered']} still unverified. "
        f"A further {overall['inapplicable']} claims cannot apply to this "
        f"device and are excluded from the denominator rather than counted as "
        f"gaps."
    )
    L.append("")
    L.append(
        "Every figure below carries the method that produced it and the number "
        "of samples behind it. A figure with neither is not in this document."
    )
    L.append("")
    L.append("| Probe layer | Applicable | Answered | Complete |")
    L.append("| --- | --- | --- | --- |")
    for row in doc["completeness"]["by_layer"]:
        c = row["completeness"]
        L.append(f"| {row['layer']} | {c['applicable']} | {c['answered']} "
                 f"| {pct(c)} |")
    L.append("")

    dropped = doc["completeness"].get("spreads_dropped", 0)
    if dropped:
        L.append(
            f"> {dropped} distributions lost their quantiles because the "
            f"device's spread table filled up. Their headline values stand; "
            f"their p05/p50/p95 are absent rather than wrong."
        )
        L.append("")

    # -- The device ---------------------------------------------------------
    L.append("## The device this describes")
    L.append("")
    L.append("| | |")
    L.append("| --- | --- |")
    L.append(f"| Firmware | `{fw}`"
             + ("" if m.get("firmware_read_from_kernel")
                else " *(declared in settings.json, not read from the kernel)*")
             + " |")
    L.append(f"| Hardware | `{m.get('hardware') or 'unknown'}` |")
    L.append(f"| Kernel interface version | {m.get('kernel_interface_version')} "
             f"(the version this app was built against) |")
    L.append(f"| SensorLab version | `{m.get('app_version')}` |")
    L.append(f"| Catalogue version | {m.get('catalogue_version')} |")
    L.append(f"| Sensor type table | version {m.get('type_table_version')}, "
             f"generated from `{m.get('sdk_tag')}` |")
    L.append(f"| Run | {m.get('run_id')}, "
             f"{m.get('duration_ms', 0) // 1000} s, ended `{m.get('end')}` |")
    L.append(f"| Requested period / latency | "
             f"{fmt(m.get('requested_period_ms'), 'ms')} / "
             f"{m.get('requested_latency_ms')} ms |")
    L.append(f"| Screen attached during the run | "
             f"{'yes' if m.get('gui_attached') else 'no'} |")
    L.append("")
    L.append(
        "The requested period and latency are what the app *asked for*. What "
        "the device delivered is in the tables below, and on this platform the "
        "two are known to differ."
    )
    L.append("")

    # -- Findings ------------------------------------------------------------
    f = findings(doc)
    L.append("## Findings")
    L.append("")
    if not f:
        L.append(
            "Nothing in this profile contradicts a documented claim, and no "
            "sensor behaved surprisingly. That is a weaker statement than it "
            "looks: see the completeness table above for how much was measured."
        )
    else:
        for line in f:
            L.append(f"- {line}")
    L.append("")

    # -- Existence table ----------------------------------------------------
    L.append("## Every sensor type, and whether it exists")
    L.append("")
    L.append(
        "`resolves` is whether `RequestDefault` returned a handle -- whether "
        "there is a producer at all. `connects` is a separate question. "
        "`drivers` is `RequestList`'s answer, which no app had ever asked for "
        "before this one. `descriptor` is `RequestGetDesc`: the kernel naming "
        "its own driver."
    )
    L.append("")
    L.append("| Type | Name | Resolves | Connects | Drivers | Descriptor | "
             "Delivered fields | Parser fields | Complete |")
    L.append("| --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    for s in doc["sensors"]:
        by_id = {c["claim_id"]: c for c in s["claims"]}

        def flag(suffix, yes="yes", no="no"):
            c = by_id.get(f"{s['type']}.{suffix}")
            if c is None or c["verdict"] == "UNVERIFIED":
                return "?"
            n = number(c.get("value"))
            if n is None:
                return "?"
            return yes if n else no

        drivers = by_id.get(f"{s['type']}.existence.driver_count")
        dn = "?" if drivers is None or drivers["verdict"] == "UNVERIFIED" \
            else fmt(drivers["value"])
        fields = by_id.get(f"{s['type']}.frame.field_count")
        fn = "--" if fields is None or fields["verdict"] == "UNVERIFIED" \
            else fmt(fields["value"])
        pf = s.get("parser_field_count")
        L.append(
            f"| `{s['type']}` | {s['name']} "
            f"| {flag('existence.default_resolves')} "
            f"| {flag('existence.connect_succeeds')} "
            f"| {dn} "
            f"| {('`' + s['descriptor'] + '`') if s.get('descriptor') else '--'} "
            f"| {fn} "
            f"| {pf if pf is not None else '*none shipped*'} "
            f"| {pct(s['completeness'])} |"
        )
    L.append("")

    # -- Per sensor ---------------------------------------------------------
    L.append("## Per sensor, in full")
    L.append("")
    for s in doc["sensors"]:
        answered = [c for c in s["claims"] if c["verdict"] != "UNVERIFIED"]
        L.append(f"### `{s['type']}` {s['name']}")
        L.append("")
        if s.get("doc"):
            L.append(f"> {s['doc']}")
            L.append("")
        notes = []
        if s.get("missing_from_doc"):
            notes.append("Not listed in `Docs/SensorsLayer.md`.")
        if s.get("parser"):
            notes.append(
                f"Parser `{s['parser']}`, {s['parser_field_count']} fields, "
                f"field-count test `{s['parser_validity']}`"
                + (", plus a value range check" if s.get("parser_range_checked")
                   else "")
                + f" (`{s.get('parser_header', '')}`)."
            )
        else:
            notes.append(
                "**No parser ships for this type.** Any frame description below "
                "is measured; the field *semantics* are inferred, not documented."
            )
        notes.append(f"{pct(s['completeness'])} complete "
                     f"({s['completeness']['answered']} of "
                     f"{s['completeness']['applicable']} applicable claims).")
        for n in notes:
            L.append(f"- {n}")
        L.append("")

        if s.get("parser_fields"):
            L.append("| Field | Name | Read as | Parser's own comment |")
            L.append("| --- | --- | --- | --- |")
            for fd in s["parser_fields"]:
                L.append(f"| {fd['index']} | `{fd['name']}` | {fd['kind']} "
                         f"| {fd.get('doc', '')} |")
            L.append("")

        if not answered:
            L.append("*Nothing measured yet.*")
            L.append("")
            continue

        L.append("| Claim | Verdict | Value | Spread | n / min | Method | "
                 "Conformance |")
        L.append("| --- | --- | --- | --- | --- | --- | --- |")
        for c in sorted(answered,
                        key=lambda c: (VERDICT_ORDER.index(c["verdict"])
                                       if c["verdict"] in VERDICT_ORDER else 9,
                                       c["claim_id"])):
            sp = c.get("spread")
            spread = (f"{fmt(sp['p05'])} / {fmt(sp['p50'])} / {fmt(sp['p95'])}"
                      if sp else "--")
            conf = c.get("conformance", "NO_CLAIM")
            if conf in ("MATCHES", "DIFFERS"):
                conf = (f"{conf} vs {fmt(c.get('expected'))} "
                        f"(`{c.get('expected_source')}`)")
            note = c.get("notes")
            reason = c.get("inapplicable_reason")
            extra = ""
            if c["verdict"] == "INAPPLICABLE" and reason:
                extra = f" -- {reason}"
            elif note:
                extra = f" -- {note}"
            L.append(
                f"| `{c['claim_id'].split('.', 1)[1]}` | {c['verdict']}{extra} "
                f"| {fmt(c.get('value'), c.get('unit', ''))} | {spread} "
                f"| {c['n']} / {c.get('minimum_n', 1)} | `{c['method_id']}` "
                f"| {conf} |"
            )
        L.append("")

    # -- Platform claims ---------------------------------------------------
    plat = [c for c in doc.get("platform_claims", [])
            if c["verdict"] != "UNVERIFIED"]
    if plat:
        L.append("## Claims that are not about one sensor")
        L.append("")
        L.append("| Claim | Verdict | Value | n / min | Method |")
        L.append("| --- | --- | --- | --- | --- |")
        for c in plat:
            L.append(f"| `{c['claim_id']}` | {c['verdict']} "
                     f"| {fmt(c.get('value'), c.get('unit', ''))} "
                     f"| {c['n']} / {c.get('minimum_n', 1)} "
                     f"| `{c['method_id']}` |")
        L.append("")

    # -- The to-do list ----------------------------------------------------
    L.append("## What is still UNVERIFIED, and what would settle it")
    L.append("")
    L.append(
        "This is the reader's to-do list, generated from the claims' own method "
        "identifiers. Grouped by method, because a method run once answers "
        "every claim under it."
    )
    L.append("")

    todo: dict[str, list[str]] = {}
    for s in doc["sensors"] + [{"claims": doc.get("platform_claims", [])}]:
        for c in s["claims"]:
            if c["verdict"] != "UNVERIFIED":
                continue
            todo.setdefault(c["method_id"], []).append(c["claim_id"])

    if not todo:
        L.append("Nothing. Every applicable claim has an answer.")
    else:
        L.append("| Method | Claims waiting | Examples |")
        L.append("| --- | --- | --- |")
        for method in sorted(todo, key=lambda k: (-len(todo[k]), k)):
            ids = todo[method]
            sample = ", ".join(f"`{i}`" for i in sorted(ids)[:3])
            more = f" (+{len(ids) - 3} more)" if len(ids) > 3 else ""
            L.append(f"| `{method}` | {len(ids)} | {sample}{more} |")
    L.append("")

    # -- Limitations -------------------------------------------------------
    L.append("## Limitations of this document")
    L.append("")
    L.append(
        "- **One device.** Everything here was measured on a single physically-"
        "owned watch. Nothing distinguishes a property of the platform from a "
        "property of this unit."
    )
    L.append(
        "- **One firmware.** The profile is named with its firmware version for "
        "that reason. `profile_diff.py` is what turns two of these into a "
        "change list."
    )
    L.append(
        "- **No reference instruments in this run.** Layer 7 -- the guided "
        "protocols against a chest strap, a weather station, a surveyed point, a "
        "counted flight of stairs -- is what would give any of these values an "
        "external check. Rows waiting on it appear in the to-do list above."
    )
    L.append(
        "- **The simulator sources nothing here.** It has four sensor sources, "
        "and its sample-rate adapter thins delivery in a way the hardware "
        "demonstrably does not. Every sensor claim above came from the device."
    )
    L.append(
        "- **Reading cannot confirm.** Rows marked `LIKELY` were inferred from "
        "another measurement or from documentation that is itself unverified. "
        "Rows marked `CONFIRMED` were measured on this hardware, with the sample "
        "count and method stated."
    )
    L.append("")
    return "\n".join(L)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("profile", type=Path)
    ap.add_argument("-o", "--out", type=Path,
                    help="write here instead of stdout")
    args = ap.parse_args()

    doc = load(args.profile)
    text = render(doc, args.profile)

    if args.out:
        args.out.write_text(text, encoding="utf-8")
        print(f"wrote {args.out}", file=sys.stderr)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
