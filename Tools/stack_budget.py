#!/usr/bin/env python3
"""Hold a watch ELF's stack frames and deepest call chain to a budget.

Reads `arm-none-eabi-objdump -d` of each ELF, sizes every function's frame from
its prologue (push, vpush, sub sp), and walks direct calls from the entry point
to find the deepest chain. The stack is the ELF's own `.stack` section.

What it cannot see: calls through a pointer (virtuals, the kernel interface,
`dyn` trait objects) and exception entry. The chain limit leaves room for them
rather than pretending they are zero, which is why it is not the whole stack.

    Tools/stack_budget.py build/SpinService.elf build/SpinGUI.elf
"""

import argparse
import functools
import re
import subprocess
import sys

FUNC = re.compile(r"^([0-9a-f]{8}) <(.*)>:$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$")
CALL = re.compile(r"^([0-9a-f]{8}) <(.*)>$")
OFFSET = re.compile(r"\+0x[0-9a-f]+$")
REGS = re.compile(r"\{(.*)\}")


def reg_count(regs, width):
    n = 0
    for r in regs.split(","):
        r = r.strip()
        if "-" in r:
            lo, hi = r.split("-")
            n += int(hi[1:]) - int(lo[1:]) + 1
        else:
            n += 1
    return n * width


def parse(disassembly):
    funcs = {}
    cur = None
    consts = {}
    for line in disassembly.splitlines():
        m = FUNC.match(line)
        if m:
            cur = {"name": m.group(2), "frame": 0, "calls": set(), "unsized": False}
            funcs[int(m.group(1), 16)] = cur
            consts = {}
            continue
        m = INSN.match(line)
        if cur is None or not m:
            continue
        op, args = m.group(2), m.group(3)
        base = op.split(".")[0]

        if base in ("movw", "mov", "movs") and re.match(r"r\d+, #\d+", args):
            reg, imm = re.match(r"(r\d+), #(\d+)", args).groups()
            consts[reg] = int(imm)
        elif base == "push" or (base == "stmdb" and args.startswith("sp!")):
            cur["frame"] += reg_count(REGS.search(args).group(1), 4)
        elif base == "vpush":
            cur["frame"] += reg_count(REGS.search(args).group(1), 8)
        elif base in ("sub", "subw") and re.match(r"sp, (sp, )?#\d+", args):
            cur["frame"] += int(re.search(r"#(\d+)", args).group(1))
        elif base in ("sub", "subw") and re.match(r"sp, (sp, )?r\d+", args):
            reg = re.search(r"(r\d+)$", args).group(1)
            if reg in consts:
                cur["frame"] += consts[reg]
            else:
                cur["unsized"] = True
        elif base == "mov" and args.startswith("sp, r"):
            # Restoring sp from the frame pointer is an epilogue; anything else
            # moves sp by an amount only the running code knows.
            if not args.startswith("sp, r7"):
                cur["unsized"] = True

        if base in ("bl", "b") or op in ("b.w",):
            c = CALL.match(args)
            if c and not OFFSET.search(c.group(2)):
                cur["calls"].add(int(c.group(1), 16))
    return funcs


def deepest(funcs, root):
    sys.setrecursionlimit(20000)

    @functools.lru_cache(maxsize=None)
    def walk(addr, on_path):
        f = funcs.get(addr)
        if f is None or addr in on_path:
            return 0, ()
        best = (0, ())
        path = on_path | {addr}
        for c in f["calls"]:
            d = walk(c, path)
            if d[0] > best[0]:
                best = d
        return f["frame"] + best[0], (addr,) + best[1]

    return walk(root, frozenset())


def stack_size(elf, objdump):
    headers = subprocess.run([objdump, "-h", elf], capture_output=True, text=True, check=True).stdout
    for line in headers.splitlines():
        cols = line.split()
        if len(cols) > 2 and cols[1] == ".stack":
            return int(cols[2], 16)
    return None


def entry(elf, funcs, objdump):
    headers = subprocess.run([objdump, "-f", elf], capture_output=True, text=True, check=True).stdout
    addr = int(re.search(r"start address 0x([0-9a-f]+)", headers).group(1), 16) & ~1
    return addr if addr in funcs else None


def check(elf, args):
    dis = subprocess.run([args.objdump, "-d", "-C", "--no-show-raw-insn", elf],
                         capture_output=True, text=True, check=True).stdout
    funcs = parse(dis)
    stack = args.stack or stack_size(elf, args.objdump)
    if not stack:
        print(f"::error::{elf}: no .stack section; pass --stack")
        return False

    frame_limit = stack * args.frame_percent // 100
    chain_limit = stack * args.chain_percent // 100
    ok = True

    print(f"{elf}: stack {stack} bytes; frame limit {frame_limit}, chain limit {chain_limit}")
    biggest = sorted(funcs.values(), key=lambda f: -f["frame"])
    for f in biggest[:5]:
        print(f"  frame {f['frame']:6d}  {f['name'][:100]}")
    for f in biggest:
        if f["frame"] <= frame_limit:
            break
        print(f"::error::{elf}: {f['name']} has a {f['frame']}-byte frame, over {frame_limit}")
        ok = False

    for f in funcs.values():
        if f["unsized"]:
            print(f"::warning::{elf}: {f['name']} moves sp by an amount this cannot size")

    root = entry(elf, funcs, args.objdump)
    if root is None:
        print(f"::error::{elf}: entry point is not the start of a function")
        return False
    depth, path = deepest(funcs, root)
    print(f"  deepest direct-call chain {depth} bytes:")
    for a in path:
        print(f"    {funcs[a]['frame']:6d}  {funcs[a]['name'][:100]}")
    if depth > chain_limit:
        print(f"::error::{elf}: deepest chain is {depth} bytes, over {chain_limit}")
        ok = False
    return ok


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("elf", nargs="+")
    p.add_argument("--objdump", default="arm-none-eabi-objdump")
    p.add_argument("--stack", type=int, help="stack bytes, when the ELF has no .stack section")
    p.add_argument("--frame-percent", type=int, default=25, help="largest one frame may be, %% of the stack")
    p.add_argument("--chain-percent", type=int, default=50, help="largest the deepest chain may be, %% of the stack")
    args = p.parse_args()
    results = [check(elf, args) for elf in args.elf]
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
