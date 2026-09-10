# PanicKit

One `#[panic_handler]` for this repository's Rust halves, reporting through one
symbol the C++ host defines. `no_std`, no allocator, in-repo only — this is not
published.

## Why it is not part of `inscribed-disc`

Nothing here draws. Two reasons that matters, and the second is the one that
decides it:

- **A published graphics crate must not own `panic_impl`.** Only one
  `#[panic_handler]` can exist in a binary, so a crate that ships one collides
  with `panic-halt`, `panic-probe` and `defmt` for anyone who enables it.
  `inscribed-disc/Docs/2026-09-10-publish-or-not.md` §6 lists this as the third
  thing to regret about publishing, and it is the one that was actionable.
- **A Service half has no framebuffer.** `Spin`'s Service-side crate wants a
  panic handler and cannot take one that arrives with a disc.

## What it costs

Two features, because one half is most of the cost:

| feature | what it reports | measured, linked |
|---|---|---|
| `panic-handler` | `file:line` | +319 bytes over reporting nothing |
| `panic-message` | `file:line: message` | +2,210 more, because it needs `core::fmt` |

[`Docs/2026-09-09-panic-handler-cost.md`](Docs/2026-09-09-panic-handler-cost.md)
has the method, the four-variant probe, and the trap that cost eight times the
answer: *an operation that can itself panic, inside a panic handler, pulls its
own panic path's formatting into the binary.* `write_location` is written with
iterator zips for that reason and says so.

## Using it

```toml
[dependencies]
panickit = { path = "../../../../../PanicKit", default-features = false }

[features]
device = ["panickit/panic-handler"]
```

A `#[panic_handler]` is a lang item, so it is linked only if something
references the crate. Nothing calls it by name, so the adopting crate needs:

```rust
#[cfg(feature = "device")]
use panickit as _;
```

The host defines the symbol. A GUI process can take the macro in
[`Header/HostPanic.hpp`](Header/HostPanic.hpp); a Service reaches a different
kernel provider and defines it itself.

**`llvm-nm --undefined-only` on the app's archive is the check.** Nothing else
catches a symbol that moved: the crate compiles perfectly well with it dangling.

## Testing it

`--features panic-handler` alone does not host-test: with the handler compiled
and `std` off the crate owns `panic_impl`, and every doc-test fails to link with
`E0152`. Use `--features panic-handler,std`, where the handler is inert.

## Licence

MIT — see the repository root.
