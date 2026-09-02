# Measurements behind `Docs/TEXT.md`

Host-side only. Nothing in here is evidence about the glass.

| path | what it measures | how to run |
|---|---|---|
| `build-all.sh` | fresh `.text`/`.bss`/`.uapp` for the three Rust GUIs, in CI's toolchain image by ID | `./build-all.sh`; read `<App>/Output/<App>GUI.elf.elf.map` |
| `sizeproto/` | code, data and scratch of four `no_std` thumbv8m prototypes: empty harness, u8g2 supersample-and-shrink, 2bpp atlas blit, `ttf-parser` plus a row-sweep rasterizer | `cargo build --release`, then `llvm-size -A target/thumbv8m.main-none-eabihf/release/proto_*`; `gen_atlas.py` regenerates `proto_b_atlas/src/atlas.rs`; `proto_c_outline` needs the two subset TTFs copied into its `src/` |
| `sq/` | the apps' supersample-and-shrink, lifted verbatim, writing one PGM per string with cap-height metrics | `cargo build --release`; `sq <face> <dst_h> <text> <out.pgm>` |
| `ref/render.py` | FreeType reference renders of the inventory beside `sq`'s output, the three research conditions, the stem sheet, and `metrics.csv` | a venv with `fonttools brotli pillow freetype-py numpy`; expects the Poppins TTFs in `../fonts/` |
| `parity.py` | whether FreeType light autohint + round-to-third reproduces TouchGFX's shipped Poppins glyphs in `Squash/.../generated/fonts/src/` | same venv |

The Poppins TTFs are not checked in; recover them with
`git show b0cf873^:Barcode/Software/Apps/TouchGFX-GUI/assets/fonts/Poppins-Regular.ttf`
(and `-SemiBold`), and Medium from `aedd9a6:BikeMap/Software/Apps/TouchGFX-GUI/assets/fonts/Poppins-Medium.ttf`.
Versions the numbers were taken with: FreeType 2.13.2 (`freetype-py`), fontTools 4.60.2,
Rust 1.97.1 with `rust-std-thumbv8m.main-none-eabihf`, `ttf-parser` 0.25.1, `u8g2-fonts` 0.8.0.
