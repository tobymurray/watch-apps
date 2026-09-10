//! Where the glass is on a round panel, and draw targets that will not paint
//! off it.
//!
//! The framebuffer is square and the display is not, so a box inset from the
//! buffer's edge is not inset from the glass, and no simulator shows the
//! difference.
//!
//! Design record, with the measurements and the architectures rejected:
//! `InscribedDisc/Docs/DESIGN.md`.
//!
//! # What it is not
//!
//! Not a replacement for [`embedded_graphics`] — a citizen of it. Both targets
//! are a [`DrawTarget`](embedded_graphics::draw_target::DrawTarget) and the
//! colours are [`PixelColor`](embedded_graphics::pixelcolor::PixelColor).
//!
//! Not a set of round drawing primitives: there is no arc, no radial layout and
//! no text on a curve.
//!
//! Not a text renderer, and not a pointer toolkit: it ships no font and does no
//! hit testing.
//!
//! # Features
//!
//! [`geometry`] is always present and needs nothing, so a consumer who wants
//! only the predicate and the chords resolves an empty dependency tree.
//!
//! | feature | default | brings |
//! |---|---|---|
//! | — | always | [`geometry`] |
//! | `embedded-graphics` | on | [`clip`], [`surface`] |
//! | `abgr2222` | off | [`color`] |
//! | `widgets` | off | [`widgets`] |
//! | `preview` | off | [`preview`]; implies `std` and `abgr2222` |
//! | `panic-handler` | off | [`mod@panic`], location only |
//! | `panic-message` | off | implies `panic-handler`; adds the message |
//! | `std` | off | host assertions |
//!
//! # Example
//!
//! Any colour whose `PixelColor::Raw` is `RawU8` satisfies
//! [`ByteColor`] with no impl written anywhere, `Gray8` included:
//!
//! ```
//! # #[cfg(feature = "embedded-graphics")] {
//! use embedded_graphics::{pixelcolor::Gray8, prelude::*};
//! use inscribed_disc::Surface;
//!
//! let mut fb = [0u8; 240 * 240];
//! let mut s = Surface::<Gray8>::round(&mut fb, 240, 240).unwrap();
//! s.clear(Gray8::BLACK);
//! s.fill_rect(60, 100, 120, 40, Gray8::WHITE);
//! # }
//! ```
//!
//! For a panel wider than a byte a pixel, [`DiscClipped`] applies the same clip
//! to a [`DrawTarget`](embedded_graphics::draw_target::DrawTarget) you already
//! have.

#![cfg_attr(not(any(test, feature = "std")), no_std)]
#![deny(missing_docs)]
#![cfg_attr(docsrs, feature(doc_cfg))]

pub mod geometry;

#[cfg(feature = "embedded-graphics")]
#[cfg_attr(docsrs, doc(cfg(feature = "embedded-graphics")))]
pub mod clip;

#[cfg(feature = "embedded-graphics")]
#[cfg_attr(docsrs, doc(cfg(feature = "embedded-graphics")))]
pub mod surface;

#[cfg(feature = "abgr2222")]
#[cfg_attr(docsrs, doc(cfg(feature = "abgr2222")))]
pub mod color;

#[cfg(feature = "widgets")]
#[cfg_attr(docsrs, doc(cfg(feature = "widgets")))]
pub mod widgets;

#[cfg(feature = "preview")]
#[cfg_attr(docsrs, doc(cfg(feature = "preview")))]
pub mod preview;

#[cfg(feature = "panic-handler")]
#[cfg_attr(docsrs, doc(cfg(feature = "panic-handler")))]
pub mod panic;

#[cfg(feature = "embedded-graphics")]
pub use clip::DiscClipped;
#[cfg(feature = "abgr2222")]
pub use color::Abgr2222;
#[cfg(feature = "embedded-graphics")]
pub use surface::{ByteColor, Clip, Surface};
