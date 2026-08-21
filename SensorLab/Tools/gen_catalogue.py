#!/usr/bin/env python3
"""Generate SensorLab's sensor type table from the SDK headers.

Why this exists
---------------
`Docs/SensorsLayer.md` in the SDK lists 31 sensor types. `SensorTypes.hpp`
declares 37. The doc is missing `HEART_RATE_EX` (0x43), `STEP_COUNTER_DAILY`
(0x52), `RUNNING_CADENCE` (0x53), `FLOOR_COUNTER_DAILY` (0x61), the
`ACTIVITY_TIME` / `ACTIVITY_TIME_DAILY` split (0xE0/0xE1) and `GRADE` (0x150).

An app that typed its own copy of that table would inherit whichever version
its author read.  So SensorLab does not have a typed table: this script reads
`$UNA_SDK/Libs/Header/SDK/SensorLayer/SensorTypes.hpp` and the 29 headers in
`DataParsers/`, and emits `SensorTypeTable.generated.hpp`.  The generated file
is committed, because the TouchGFX simulator's Makefile has no place to run a
generator and requiring python3 for every build would be worse than a checked-in
artifact -- but `CatalogueGeneration_test.cpp` re-runs the parse at test time
against the SDK the tests are configured with, so a table that has fallen behind
its headers is a red test rather than a silent wrong answer.

What it cannot derive
---------------------
Which parser belongs to which sensor type.  Nothing in the SDK states it: the
parser headers carry no type value, and the type enum carries no parser name.
`PARSER_TYPES` below is therefore hand-maintained -- and checked.  Every parser
must be claimed by at least one type, every type must claim at most one parser,
and a type this script has never seen is a hard error.  A new type in
`SensorTypes.hpp` stops the build until somebody has decided whether it has a
parser, which is the correct amount of friction.

Usage
-----
    UNA_SDK=/path/to/una-sdk python3 Tools/gen_catalogue.py \
        --out Software/Libs/Header/Catalogue/SensorTypeTable.generated.hpp

    # or, to check a committed file is current without rewriting it:
    UNA_SDK=... python3 Tools/gen_catalogue.py --check --out <path>
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

# ---------------------------------------------------------------------------
# The one hand-maintained fact: parser <-> type.
#
# Keyed by the parser's file stem after `SensorDataParser`.  A parser serving
# two types is deliberate and rare: the "daily" variants share their since-boot
# counterpart's frame, and `Activity` serves both halves of the 0xE0/0xE1 split.
# ---------------------------------------------------------------------------
PARSER_TYPES: dict[str, list[str]] = {
    "Accelerometer":        ["ACCELEROMETER"],
    "AccelerometerRaw":     ["ACCELEROMETER_RAW"],
    "Gyroscope":            ["GYROSCOPE"],
    "GyroscopeRaw":         ["GYROSCOPE_RAW"],
    "HeartRate":            ["HEART_RATE"],
    "HeartRateMetrics":     ["HEART_RATE_METRICS_DAILY"],
    "HeartRateEx":          ["HEART_RATE_EX"],
    "StepDetector":         ["STEP_DETECTOR"],
    "StepCounter":          ["STEP_COUNTER", "STEP_COUNTER_DAILY"],
    "RunningCadence":       ["RUNNING_CADENCE"],
    "FloorCounter":         ["FLOOR_COUNTER", "FLOOR_COUNTER_DAILY"],
    "Temperature":          ["AMBIENT_TEMPERATURE"],
    "Pressure":             ["PRESSURE"],
    "Altimeter":            ["ALTIMETER"],
    "WristMotion":          ["WRIST_MOTION"],
    "MotionDetect":         ["MOTION_DETECT"],
    "ActivityRecognition":  ["ACTIVITY_RECOGNITION"],
    "Activity":             ["ACTIVITY_TIME", "ACTIVITY_TIME_DAILY"],
    "Spo2":                 ["SPO2"],
    "GpsLocation":          ["GPS_LOCATION"],
    "GpsSpeed":             ["GPS_SPEED"],
    "GpsDistance":          ["GPS_DISTANCE"],
    "BatteryLevel":         ["BATTERY_LEVEL"],
    "BatteryCharging":      ["BATTERY_CHARGING"],
    "BatteryMetrics":       ["BATTERY_METRICS"],
    "Fusion":               ["FUSION"],
    "FusionRaw":            ["FUSION_RAW"],
    "Touch":                ["TOUCH_DETECT"],
    "Grade":                ["GRADE"],
}

# Types with no parser anywhere in the SDK.  Listed explicitly rather than
# inferred, so that a parser appearing for one of them is a change somebody
# has to acknowledge -- publishing a discovered frame layout for any of these
# is the single highest-value thing Tier 1 can produce.
NO_PARSER: set[str] = {
    "MAGNETIC_FIELD",
    "HEART_BEAT",
    "GESTURE_RECOGNITION",
    "PPG",
    "ECG",
}

# The C++ enumerator that stands in for "there is no parser".
NO_PARSER_SENTINEL = "kNoParser"


@dataclass
class SensorType:
    name: str
    value: int
    doc: str


@dataclass
class ParserField:
    name: str
    index: int
    # 'f' float, 'u' uint32, 'i' int32, '?' the parser never reads it.
    accessor: str = "?"
    doc: str = ""


@dataclass
class Parser:
    stem: str          # e.g. "Accelerometer"
    cls: str           # the C++ class name
    header: str        # include path under SDK/
    fields: list[ParserField] = field(default_factory=list)
    # "==" for exact field-count equality, ">=" for at-least.
    validity: str = "=="
    # True when isDataValid() also range-checks one or more field values.
    range_checked: bool = False
    # True when isDataValid() reads a field before it has checked the count.
    reads_before_count: bool = False


# ---------------------------------------------------------------------------
# Parsing SensorTypes.hpp
# ---------------------------------------------------------------------------

_ENUMERATOR = re.compile(
    r"^\s*(?P<name>[A-Z][A-Z0-9_]*)\s*=\s*(?P<value>0x[0-9A-Fa-f]+|[A-Z][A-Z0-9_]*)\s*,"
    r"(?:\s*(?://[/!]?<?|/\*\*?<?)\s*(?P<doc>.*?)\s*(?:\*/)?\s*)?$"
)


def parse_sensor_types(path: Path) -> tuple[list[SensorType], dict[str, str]]:
    """Return (distinct types in declaration order, alias -> target)."""
    types: list[SensorType] = []
    aliases: dict[str, str] = {}
    seen_values: dict[int, str] = {}

    text = path.read_text(encoding="utf-8")
    body = text.split("enum class Type", 1)
    if len(body) != 2:
        raise SystemExit(f"{path}: no `enum class Type` found")

    for line in body[1].splitlines():
        if line.strip().startswith("};"):
            break
        m = _ENUMERATOR.match(line)
        if not m:
            continue
        name = m.group("name")
        raw = m.group("value")
        doc = (m.group("doc") or "").strip().rstrip("+").strip()

        if raw.startswith("0x"):
            value = int(raw, 16)
        else:
            # An alias spelled as another enumerator.
            target = raw
            prior = next((t for t in types if t.name == target), None)
            if prior is None:
                raise SystemExit(f"{path}: alias {name} = {target}, target unknown")
            aliases[name] = target
            continue

        if name == "UNKNOWN" and value == 0:
            continue

        if value in seen_values:
            aliases[name] = seen_values[value]
            continue

        seen_values[value] = name
        types.append(SensorType(name=name, value=value, doc=doc))

    if not types:
        raise SystemExit(f"{path}: parsed no sensor types")
    return types, aliases


# ---------------------------------------------------------------------------
# Parsing a parser header
# ---------------------------------------------------------------------------

_FIELD_ENUM = re.compile(r"enum\s+(?:class\s+)?Field\s*(?::\s*\w+\s*)?\{(?P<body>.*?)\}\s*;",
                         re.DOTALL)
_FIELD_ENTRY = re.compile(
    r"^\s*(?P<name>[A-Z][A-Z0-9_]*)\s*(?:=\s*(?P<value>\d+)\s*)?,?"
    r"(?:\s*(?://[/!]?<?|/\*\*?<?)\s*(?P<doc>.*?)\s*(?:\*/)?\s*)?$"
)
_CLASS = re.compile(r"\bclass\s+(?P<cls>\w+)\s*(?:final\s*)?(?::|\{)")
_IS_VALID_OPEN = re.compile(r"bool\s+isDataValid\s*\(\s*\)\s*const\s*\{")


def _is_valid_body(text: str, path: Path) -> str:
    """The body of `isDataValid()`, brace-balanced.

    Not a regex, because five of the 29 parsers write the check as a sequence of
    early returns rather than one expression, and a lazy `.*?}` stops at the
    first inner `if`'s closing brace. Getting this wrong reported
    `ActivityRecognition` as checking no field count at all, which is precisely
    the kind of quiet wrong answer this whole app exists to avoid.
    """
    m = _IS_VALID_OPEN.search(text)
    if m is None:
        raise SystemExit(f"{path}: no isDataValid() found")
    depth = 1
    i = m.end()
    while i < len(text) and depth > 0:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
        i += 1
    if depth != 0:
        raise SystemExit(f"{path}: isDataValid() has unbalanced braces")
    return text[m.end():i - 1]


def parse_parser(path: Path) -> Parser:
    stem = path.stem.replace("SensorDataParser", "")
    text = path.read_text(encoding="utf-8")

    m = _FIELD_ENUM.search(text)
    if not m:
        raise SystemExit(f"{path}: no `enum Field` found")

    fields: list[ParserField] = []
    index = 0
    for line in m.group("body").splitlines():
        entry = _FIELD_ENTRY.match(line)
        if not entry:
            continue
        name = entry.group("name")
        if entry.group("value") is not None:
            index = int(entry.group("value"))
        if name == "COUNT":
            if index != len(fields):
                raise SystemExit(
                    f"{path}: Field::COUNT is {index} but {len(fields)} fields precede it"
                )
            break
        fields.append(ParserField(name=name, index=index,
                                  doc=(entry.group("doc") or "").strip()))
        index += 1

    if not fields:
        raise SystemExit(f"{path}: Field enum has no fields before COUNT")

    # Which union member each field is read through.  This is the only place the
    # SDK states a field's type: the doc comments disagree with each other and
    # `Docs/SensorsLayer.md` disagrees with both.
    for f in fields:
        for accessor in ("f", "u", "i"):
            if re.search(rf"mData\.{accessor}\[\s*(?:Field::)?{f.name}\s*\]", text):
                if f.accessor != "?" and f.accessor != accessor:
                    raise SystemExit(
                        f"{path}: {f.name} read through both .{f.accessor} and .{accessor}"
                    )
                f.accessor = accessor

    parser = Parser(
        stem=stem,
        cls=_class_name(text, stem, path),
        header=f"SDK/SensorLayer/DataParsers/{path.name}",
        fields=fields,
    )

    body = _is_valid_body(text, path)
    if re.search(r"getFieldCount\(\)\s*>=", body):
        parser.validity = ">="
    elif re.search(r"getFieldCount\(\)\s*(?:==|!=)", body):
        # `!=` inside an early-return guard is the same predicate as `==` in a
        # single expression: exactly this many fields or the sample is dropped.
        parser.validity = "=="
    else:
        raise SystemExit(f"{path}: isDataValid() checks no field count")

    # A value check beyond the field count.  Any mData access inside
    # isDataValid() is one, by definition.
    parser.range_checked = bool(re.search(r"mData\.[fui]\[", body))

    # ...and if the first such access comes before the field-count check, the
    # parser reads out of bounds on a short frame.  `&&` short-circuits left to
    # right and `DataView`'s bounds assert is compiled out at -Os with NDEBUG.
    if parser.range_checked:
        first_value = re.search(r"mData\.[fui]\[", body)
        first_count = re.search(r"getFieldCount\(\)", body)
        parser.reads_before_count = (
            first_value is not None and first_count is not None
            and first_value.start() < first_count.start()
        )

    return parser


def _class_name(text: str, stem: str, path: Path) -> str:
    for m in _CLASS.finditer(text):
        if m.group("cls") == stem:
            return stem
    # Every parser in 1.4 names its class after its file stem; if one stops
    # doing so, say which rather than emitting a name that will not compile.
    raise SystemExit(f"{path}: no `class {stem}` found")


# ---------------------------------------------------------------------------
# Emitting the header
# ---------------------------------------------------------------------------

BANNER = """\
/**
 ******************************************************************************
 * @file    SensorTypeTable.generated.hpp
 * @brief   GENERATED. Every sensor type the SDK declares, and its parser.
 ******************************************************************************
 *
 * Do not edit. Regenerate with:
 *
 *     UNA_SDK=/path/to/una-sdk python3 SensorLab/Tools/gen_catalogue.py \\
 *         --out SensorLab/Software/Libs/Header/Catalogue/SensorTypeTable.generated.hpp
 *
 * Generated from:
 *   @@TYPES_REL@@
 *   @@PARSERS_REL@@  (@@N_PARSERS@@ parsers)
 *
 * Why generated rather than typed: `Docs/SensorsLayer.md` is six types behind
 * `SensorTypes.hpp`, and an app that typed its own table would inherit
 * whichever of the two its author happened to read. See Tools/gen_catalogue.py.
 *
 * This file states what the SDK *claims*. It is the `expected` column of every
 * conformance row and never the measured one -- reading a header can refute a
 * claim about the device but can never confirm one.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_SENSORTYPETABLE_GENERATED_HPP
#define SENSORLAB_SENSORTYPETABLE_GENERATED_HPP

#include <cstddef>
#include <cstdint>

namespace SensorLab::Catalogue
{

/// Bumped by the generator whenever the emitted shape changes, so a profile
/// carries the table version that produced it.
constexpr uint32_t kTypeTableVersion = 1;

/// The SDK tree this table was generated from, for the run manifest.
constexpr char kGeneratedFromSdk[] = "@@SDK_TAG@@";

/// Which union member of `SDK::Sensor::Data::Field` a field is read through.
///
/// Taken from the parser's own accessors, which is the only place in the SDK
/// that states a field's type unambiguously: the doc comments contradict each
/// other and `SensorsLayer.md` contradicts both. `Unread` means the shipped
/// parser declares the field and never reads it.
enum class FieldKind : uint8_t { Unread = 0, Float, U32, I32 };

/// How a parser's `isDataValid()` compares the delivered field count.
///
/// 28 of 29 parsers are `Exact`, so a single appended field silently
/// invalidates every sample. `HeartRateEx` is `AtLeast`, deliberately, so a
/// future kernel can extend the frame without breaking apps. That asymmetry is
/// a conformance finding in its own right -- see Docs/FINDINGS.md.
enum class Validity : uint8_t { Exact = 0, AtLeast };

struct FieldSpec
{
    const char *name;
    FieldKind   kind;
    /// The parser's doc comment for this field, verbatim. Quoted rather than
    /// paraphrased so the report can cite it.
    const char *doc;
};

struct ParserSpec
{
    /// C++ class name under `SDK::SensorDataParser`.
    const char      *cls;
    /// Header path, for `expected_source` citations.
    const char      *header;
    const FieldSpec *fields;
    uint8_t          fieldCount;
    Validity         validity;
    /// `isDataValid()` also range-checks at least one field value, so a frame
    /// with the right shape and an out-of-range value reads as invalid.
    bool             rangeChecked;
    /// `isDataValid()` dereferences a field *before* checking the field count.
    /// On a short frame that is an out-of-bounds read, because `DataView`'s
    /// bounds assert is compiled out at -Os. Only the profiler meets short
    /// frames, so only the profiler must not construct these parsers blind.
    bool             readsBeforeCount;
};

/// Index into `kParsers`, or `kNoParser`.
constexpr uint8_t kNoParser = 0xFF;

struct TypeSpec
{
    /// The enumerator name in `SensorTypes.hpp`, e.g. "ACCELEROMETER".
    const char *name;
    /// Its value, e.g. 0x10. This is what a claim_id is keyed on.
    uint32_t    value;
    /// The enum's own doc comment, verbatim.
    const char *doc;
    /// Index into `kParsers`, or `kNoParser` for the five types the SDK ships
    /// no parser for. For those, a measured frame layout is the only
    /// description of the frame that exists anywhere.
    uint8_t     parser;
    /// True when `Docs/SensorsLayer.md`'s table does not list this type at all.
    bool        missingFromDoc;
};

"""


DOC_LISTED_TYPES = {
    # Exactly the types `Docs/SensorsLayer.md`'s table names, as of
    # apps-v1.4.0. Checked into the generator rather than parsed out of the
    # markdown, because the point of the comparison is that the markdown is
    # stale -- a parser of it would have to be kept correct against a moving
    # document to tell us something we already know.
    "ACCELEROMETER", "ACCELEROMETER_RAW", "GYROSCOPE", "GYROSCOPE_RAW",
    "MAGNETIC_FIELD", "HEART_BEAT", "HEART_RATE", "HEART_RATE_METRICS_DAILY",
    "STEP_DETECTOR", "STEP_COUNTER", "FLOOR_COUNTER", "AMBIENT_TEMPERATURE",
    "PRESSURE", "ALTIMETER", "WRIST_MOTION", "MOTION_DETECT",
    "ACTIVITY_RECOGNITION", "GESTURE_RECOGNITION", "ACTIVITY_TIME_DAILY",
    "PPG", "SPO2", "ECG", "GPS_LOCATION", "GPS_SPEED", "GPS_DISTANCE",
    "BATTERY_LEVEL", "BATTERY_CHARGING", "BATTERY_METRICS", "FUSION",
    "FUSION_RAW", "TOUCH_DETECT",
}


def cstr(s: str) -> str:
    out = s.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{out}"'


_KIND = {"f": "FieldKind::Float", "u": "FieldKind::U32",
         "i": "FieldKind::I32", "?": "FieldKind::Unread"}


def emit(types: list[SensorType], parsers: list[Parser], sdk_tag: str,
         types_rel: str, parsers_rel: str) -> str:
    banner = (BANNER
              .replace("@@TYPES_REL@@", types_rel)
              .replace("@@PARSERS_REL@@", parsers_rel)
              .replace("@@N_PARSERS@@", str(len(parsers)))
              .replace("@@SDK_TAG@@", sdk_tag))
    out: list[str] = [banner]

    # Field arrays, one per parser.
    for p in parsers:
        out.append(f"// {p.cls} -- {p.header}")
        out.append(f"constexpr FieldSpec kFields{p.cls}[] = {{")
        for f in p.fields:
            out.append(f"    {{ {cstr(f.name)}, {_KIND[f.accessor]}, {cstr(f.doc)} }},")
        out.append("};\n")

    out.append("constexpr ParserSpec kParsers[] = {")
    for p in parsers:
        validity = "Validity::AtLeast" if p.validity == ">=" else "Validity::Exact"
        out.append(
            f"    {{ {cstr(p.cls)}, {cstr(p.header)}, kFields{p.cls}, "
            f"{len(p.fields)}, {validity}, "
            f"{'true' if p.range_checked else 'false'}, "
            f"{'true' if p.reads_before_count else 'false'} }},"
        )
    out.append("};")
    out.append(f"constexpr size_t kParserCount = {len(parsers)};\n")

    parser_index = {p.stem: i for i, p in enumerate(parsers)}
    type_parser: dict[str, int] = {}
    for stem, names in PARSER_TYPES.items():
        for n in names:
            type_parser[n] = parser_index[stem]

    out.append("constexpr TypeSpec kTypes[] = {")
    for t in types:
        pi = type_parser.get(t.name)
        pref = NO_PARSER_SENTINEL if pi is None else str(pi)
        missing = "true" if t.name not in DOC_LISTED_TYPES else "false"
        out.append(
            f"    {{ {cstr(t.name)}, 0x{t.value:X}u, {cstr(t.doc)}, {pref}, {missing} }},"
        )
    out.append("};")
    out.append(f"constexpr size_t kTypeCount = {len(types)};\n")

    # Aggregates the claim store is sized from. Computed here rather than at
    # runtime so the store is a static array with a compile-time bound.
    total_fields = 0
    for t in types:
        pi = type_parser.get(t.name)
        total_fields += len(parsers[pi].fields) if pi is not None else 0
    n_no_parser = sum(1 for t in types if t.name not in type_parser)
    n_missing_doc = sum(1 for t in types if t.name not in DOC_LISTED_TYPES)

    out.append("/// Fields across every type that ships a parser. Layer 5 opens one claim")
    out.append("/// row per field, so this plus `kAssumedFieldsWhenNoParser` per unparsed")
    out.append("/// type is what sizes the store.")
    out.append(f"constexpr size_t kParsedFieldTotal = {total_fields};")
    out.append(f"constexpr size_t kTypesWithoutParser = {n_no_parser};")
    out.append("/// Types `Docs/SensorsLayer.md` does not mention. Finding number one, and")
    out.append("/// the reason this file is generated.")
    out.append(f"constexpr size_t kTypesMissingFromDoc = {n_missing_doc};\n")

    out.append("} // namespace SensorLab::Catalogue\n")
    out.append("#endif // SENSORLAB_SENSORTYPETABLE_GENERATED_HPP")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Consistency checks
# ---------------------------------------------------------------------------

def check(types: list[SensorType], parsers: list[Parser]) -> None:
    names = {t.name for t in types}
    stems = {p.stem for p in parsers}

    unknown_stems = set(PARSER_TYPES) - stems
    if unknown_stems:
        raise SystemExit(
            "PARSER_TYPES names parsers that do not exist in the SDK: "
            + ", ".join(sorted(unknown_stems))
        )
    unclaimed = stems - set(PARSER_TYPES)
    if unclaimed:
        raise SystemExit(
            "the SDK ships parsers PARSER_TYPES does not claim: "
            + ", ".join(sorted(unclaimed))
            + " -- decide which sensor type each serves and add it."
        )

    claimed: dict[str, str] = {}
    for stem, tnames in PARSER_TYPES.items():
        for n in tnames:
            if n not in names:
                raise SystemExit(
                    f"PARSER_TYPES maps {stem} to {n}, which SensorTypes.hpp "
                    f"does not declare (renamed? removed?)"
                )
            if n in claimed:
                raise SystemExit(
                    f"{n} is claimed by both {claimed[n]} and {stem}; a type "
                    f"may have at most one parser."
                )
            claimed[n] = stem

    unaccounted = names - set(claimed) - NO_PARSER
    if unaccounted:
        raise SystemExit(
            "SensorTypes.hpp declares types this generator has never seen: "
            + ", ".join(sorted(unaccounted))
            + "\nAdd each to PARSER_TYPES or to NO_PARSER -- and open a "
              "Docs/FINDINGS.md row if SensorsLayer.md is missing it too."
        )

    stale_no_parser = NO_PARSER - names
    if stale_no_parser:
        raise SystemExit(
            "NO_PARSER lists types SensorTypes.hpp no longer declares: "
            + ", ".join(sorted(stale_no_parser))
        )

    both = NO_PARSER & set(claimed)
    if both:
        raise SystemExit(
            "these types are in NO_PARSER and also have a parser: "
            + ", ".join(sorted(both))
            + " -- a parser appearing for one of these is news; say so in "
              "Docs/FINDINGS.md before removing it from NO_PARSER."
        )

    unknown_doc = DOC_LISTED_TYPES - names
    if unknown_doc:
        raise SystemExit(
            "DOC_LISTED_TYPES names types SensorTypes.hpp does not declare: "
            + ", ".join(sorted(unknown_doc))
        )


# ---------------------------------------------------------------------------

def sdk_tag(sdk: Path) -> str:
    """A short, stable description of the SDK tree, for the manifest.

    Deliberately not a git call: this runs inside build containers where the
    SDK is a bind mount without a working `git`, and a manifest field that is
    sometimes empty is worse than one that names the file it read.
    """
    header = sdk / "Libs/Header/SDK/Interfaces/IKernel.hpp"
    if header.exists():
        m = re.search(r"#define\s+KERNEL_INTERFACE_VERSION\s*\(?\s*(\d+)",
                      header.read_text())
        if m:
            return f"kernel-interface-{m.group(1)}"
    return "unknown"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sdk", default=os.environ.get("UNA_SDK"),
                    help="SDK root; defaults to $UNA_SDK")
    ap.add_argument("--out", required=True, help="header to write")
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero if --out is not what would be written")
    args = ap.parse_args()

    if not args.sdk:
        print("gen_catalogue: set UNA_SDK or pass --sdk", file=sys.stderr)
        return 2
    sdk = Path(args.sdk)

    types_path = sdk / "Libs/Header/SDK/SensorLayer/SensorTypes.hpp"
    parsers_dir = sdk / "Libs/Header/SDK/SensorLayer/DataParsers"
    if not types_path.exists():
        print(f"gen_catalogue: {types_path} not found", file=sys.stderr)
        return 2
    if not parsers_dir.is_dir():
        print(f"gen_catalogue: {parsers_dir} not found", file=sys.stderr)
        return 2

    types, aliases = parse_sensor_types(types_path)
    parsers = [parse_parser(p) for p in sorted(parsers_dir.glob("SensorDataParser*.hpp"))]
    check(types, parsers)

    text = emit(types, parsers, sdk_tag(sdk),
                "Libs/Header/SDK/SensorLayer/SensorTypes.hpp",
                "Libs/Header/SDK/SensorLayer/DataParsers/")

    out = Path(args.out)
    if args.check:
        if not out.exists():
            print(f"gen_catalogue: {out} does not exist", file=sys.stderr)
            return 1
        if out.read_text(encoding="utf-8") != text:
            print(f"gen_catalogue: {out} is stale -- rerun without --check",
                  file=sys.stderr)
            return 1
        print(f"gen_catalogue: {out} is current "
              f"({len(types)} types, {len(parsers)} parsers, "
              f"{len(aliases)} aliases skipped)")
        return 0

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text, encoding="utf-8")
    print(f"gen_catalogue: wrote {out} -- {len(types)} types, {len(parsers)} parsers, "
          f"aliases skipped: {', '.join(sorted(aliases)) or 'none'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
