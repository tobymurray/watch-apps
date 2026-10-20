# Changelog

All notable changes to `inscribed-disc`. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the crate follows
[Semantic Versioning](https://semver.org/) as Cargo applies it to `0.x`.

## [Unreleased]

## [0.2.1] — 2026-10-20

### Added

- `geometry::band_chord`, the width of glass every row of a band shares. It is
  `const`, so a line's width can be asserted against its rows at compile time.
- The `audit` module, with no feature required: `off_glass` lists every pixel
  of a finished frame, other than the ground colour, that the glass inset by a
  margin would not show.

### Changed

- Documentation: the README's install line names 0.2, and comments no longer
  describe code outside the crate.

## [0.2.0] — 2026-09-12

### Removed

- **Breaking:** the `panic` module and the `panic-handler` and `panic-message`
  features. The handler now lives outside this crate.

### Changed

- **Breaking:** `decode` moved from `preview` to `color`, so it needs only
  `abgr2222` and no PNG encoder.
- `rust-version` lowered from 1.81 to 1.71.1, the floor `embedded-graphics`
  declares.

### Fixed

- `--no-default-features --features embedded-graphics` failed to compile the
  test modules.

## [0.1.0] — 2026-09-10

Initial release: `geometry`, `DiscClipped`, `Surface` and `ByteColor`, and the
`abgr2222` and `preview` features.

[Unreleased]: https://github.com/tobymurray/watch-apps/compare/inscribed-disc-v0.2.1...HEAD
[0.2.1]: https://github.com/tobymurray/watch-apps/compare/inscribed-disc-v0.2.0...inscribed-disc-v0.2.1
[0.2.0]: https://github.com/tobymurray/watch-apps/compare/inscribed-disc-v0.1.0...inscribed-disc-v0.2.0
[0.1.0]: https://github.com/tobymurray/watch-apps/tree/inscribed-disc-v0.1.0/InscribedDisc
