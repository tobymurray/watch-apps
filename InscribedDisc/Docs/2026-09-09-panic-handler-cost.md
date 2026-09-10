# What a panic handler that says where it panicked costs

2026-09-09. Linked, on `thumbv8m.main-none-eabihf`, against the `b"panic"`
literal `Spin` and `Barcode` shipped.

The question is not academic. On this device a panic is a silent hang with a
ride in progress, there is no debugger, and the only recovery is a reboot. But
`.text` executes from the same 600 KiB window as the framebuffer, so
diagnostics are charged against the pixels.

## Why the earlier number was wrong

The first estimate was **+116 bytes**, taken from `Spin`'s own object in the
archive. It is out by a factor of 28, and the reason is structural: a panic
handler's cost is almost entirely what it drags out of `core` —
`core::fmt::write`, `pad_integral`, `<str as Display>::fmt` — and those live in
`core`'s object, not the app's. **An archive-level measurement cannot see a
panic handler's cost at all.**

Fixed by linking a real ELF with `rust-lld` and a minimal linker script, so
`--gc-sections` decides what is actually present. That is what the numbers below
are, and it is the first linked measurement in this repository.

## The result, on `Spin`'s real renderer

| handler | `.text` | `.rodata` | total | vs literal |
|---|---|---|---|---|
| `b"panic"`, as shipped | 7,772 | 25,674 | 33,446 | — |
| `file:line` | 8,102 | 26,050 | 34,152 | **+706** |
| `file:line: message` | 9,982 | 26,690 | 36,672 | **+3,226** |

**The location is cheap and the message is not.** The location costs 706 bytes;
the message adds 2,520 more, so it is 78% of the total. That is `core::fmt`,
and it is why the two are separate features: `panic-handler` gets the line,
`panic-message` adds the text.

## The trap, which cost eight times the answer

An isolated probe, to separate what is being paid for:

| handler | `.text` | `.rodata` | total |
|---|---|---|---|
| `b"panic"` | 654 | 5 | 659 |
| `file:line`, written not to panic | 858 | 120 | **978** |
| `file:line`, written naively | 2,472 | 692 | **3,164** |
| `file:line: message` | 3,162 | 888 | 4,050 |

Both location handlers produce byte-identical output. The naive one costs
**eight times more** because it used slice indexing and `copy_from_slice` — and
*a panicking operation inside a panic handler pulls its own panic path's
formatting into the binary*. Writing the same thing with iterator zips, which
cannot panic, removes it.

This is the sharpest lesson here: in a panic handler, an operation that can
panic is not merely a correctness worry, it is most of the code size. The
implementation in `src/panic.rs` is written that way and says so.

## Re-run on 2026-09-10, and the falsifier fired

The probe's absolute figures do not reproduce on rustc 1.98.1; they were taken
on 1.97.1. Every row moved, including `literal`, which no change here touched,
so this is the compiler and not the code:

| handler | `.text` | `.rodata` | total | on 1.97.1 |
|---|---|---|---|---|
| `b"panic"` | 730 | 5 | 735 | 659 |
| `file:line`, written not to panic | 942 | 204 | **1,146** | 978 |
| `file:line`, written naively | 2,502 | 804 | **3,306** | 3,164 |
| `file:line: message` | 3,306 | 1,004 | 4,310 | 4,050 |

**The lesson survives, smaller.** Over the `literal` baseline the careful
location handler costs 411 bytes and the naive one 2,571 — **6.3×**, where
1.97.1 gave 319 against 2,505, which is the 8× quoted above. An operation that
can panic inside a panic handler is still most of the code size.

The `file:line: message` row now measures **the crate's own handler** rather
than a copy of it: `panic` moved behind the `panic-handler` feature, so `full`
turns on `inscribed-disc/panic-message` and the probe defines no handler of its
own for that variant. A replica would have stopped tracking what ships.

The Spin-linked figures in "The result" above were **not** re-run, and are
still 1.97.1 numbers.

## What would falsify this

- A different `opt-level`. This is `z`.
- An app that already links `core::fmt` for another reason, where the message is
  then nearly free. `Spin` does not: its renderer formats numbers by hand into
  caller-owned buffers precisely to avoid it, so this is the expensive case.
- More distinct panic sites, which grows the `.rodata` of file-path strings
  independently of the `.text`. The probe's 120 bytes against `Spin`'s 376 is
  that effect.
- A future `core` whose formatting is smaller, or a `panic_immediate_abort`
  build, where all of this collapses.

## Re-running it

```
cd InscribedDisc/Docs/measurements/panic-handler
for f in literal location_min location full; do
  cargo build --release --target thumbv8m.main-none-eabihf --features $f
  llvm-size --format=sysv target/thumbv8m.main-none-eabihf/release/panicprobe
done
```

`tmp/spinlink` is the same harness pointed at `Spin`'s crate; it is not checked
in because it needs a throwaway copy of that crate carrying the old handler.
