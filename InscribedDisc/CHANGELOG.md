# Changelog

All notable changes to `inscribed-disc`. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the crate follows
[Semantic Versioning](https://semver.org/) as Cargo applies it to `0.x`.

## [Unreleased]

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

[Unreleased]: https://github.com/tobymurray/watch-apps/compare/inscribed-disc-v0.2.0...HEAD
[0.2.0]: https://github.com/tobymurray/watch-apps/compare/inscribed-disc-v0.1.0...inscribed-disc-v0.2.0
[0.1.0]: https://github.com/tobymurray/watch-apps/tree/inscribed-disc-v0.1.0/InscribedDisc
