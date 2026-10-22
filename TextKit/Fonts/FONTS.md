# Fonts

`src/faces.rs` is glyph data rasterized from the fonts below by `Tools/atlas.py`,
which makes it a Modified Version of each under the SIL Open Font License 1.1.
Each font's licence text ships with the crate beside this file. The font files
themselves do not: only `Tools/atlas.py` reads them, and nothing on the watch
does.

## Poppins

Poppins Regular and SemiBold, version 4.004, copyright 2020 The Poppins Project
Authors (https://github.com/itfoundry/Poppins), Indian Type Foundry; Latin by
Jonny Pinhorn. Licensed under the SIL Open Font License, Version 1.1, as the
fonts' own `name` table states; text in `OFL-Poppins.txt`. No Reserved Font
Name is declared.

| file | SHA-256 |
|---|---|
| `Poppins-Regular.ttf` | `5df9b946cba1e41cbdd84eedd1ca4d50e290c70e85b26416e7a4a5fd6533d82b` |
| `Poppins-SemiBold.ttf` | `27e639f72fcd2b1569927fdcb2ef5d8ad13a734e05e283d99eaa276545c4ec32` |
| `OFL-Poppins.txt` | `6be04893d770899a015649c7aa3b582f871b272f8747a92b78b17c3e5c8b2573` |

The two fonts are the blobs TouchGFX rasterized for every app in this repo; they
were removed from `Barcode` in `b0cf873` and are byte-identical to
`b0cf873^:Barcode/Software/Apps/TouchGFX-GUI/assets/fonts/`. Medium, Light and
the italics are in `aedd9a6` under `BikeMap/`, unused until a screen wants them.
They are not the files Google Fonts serves today, which hash differently.

`OFL-Poppins.txt` is Google Fonts' copy, byte for byte, from
<https://raw.githubusercontent.com/google/fonts/7dc16b7de42db624b902f2292b68ed9e489e5053/ofl/poppins/OFL.txt>,
because its copyright line is the one in these fonts' `name` table. The
upstream repository's `OFL.txt` (`itfoundry/Poppins` at `ade6ff1`) still reads
"Copyright 2014-2019 Indian Type Foundry", which the fonts no longer say.
