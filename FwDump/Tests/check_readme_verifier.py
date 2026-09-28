#!/usr/bin/env python3
"""Run the check README.txt tells a user to run, over a synthetic dump.

usage: check_readme_verifier.py <fwdump-export-synthetic> <scratch-dir>

Passes when the command accepts the intact bundle and rejects one with a
single flipped byte.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

exporter, scratch = sys.argv[1], Path(sys.argv[2])
shutil.rmtree(scratch, ignore_errors=True)
scratch.mkdir(parents=True)
subprocess.run([exporter, str(scratch)], check=True, stdout=subprocess.DEVNULL)

readme = (scratch / "README.txt").read_text(encoding="ascii")
match = re.search(r'^python3 -c "([^"]*)"$', readme, re.MULTILINE)
if not match:
    sys.exit("README.txt has no python3 -c command")
code = match.group(1)


def verify(directory):
    out = subprocess.run([sys.executable, "-c", code], cwd=directory, check=True,
                         capture_output=True, text=True).stdout.strip()
    print(f"{directory.name}: {out}")
    return out


good = verify(scratch)
if not (good.startswith("ok 32/32 ") and good.endswith(" matches")):
    sys.exit("the README's check rejected an intact dump")

damaged = scratch.parent / (scratch.name + "-damaged")
shutil.rmtree(damaged, ignore_errors=True)
shutil.copytree(scratch, damaged)
chunk = damaged / "dump_100000.bin"
data = bytearray(chunk.read_bytes())
data[100] ^= 0xFF
chunk.write_bytes(bytes(data))

bad = verify(damaged)
if not (bad.startswith("ok 31/32 ") and bad.endswith(" DIFFERS")):
    sys.exit("the README's check accepted a damaged dump")
