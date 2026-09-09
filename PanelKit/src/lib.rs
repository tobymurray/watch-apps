//! Immediate-mode GUI primitives for round, low-bit-depth, button-driven panels.
//!
//! Design record, with the measurements and the architectures rejected:
//! `PanelKit/Docs/DESIGN.md`.
//!
//! # What it is not
//!
//! Not a replacement for [`embedded_graphics`] — a citizen of it. The surface is
//! a [`DrawTarget`](embedded_graphics::draw_target::DrawTarget) and the colours
//! are [`PixelColor`](embedded_graphics::pixelcolor::PixelColor).
//!
//! Not a text renderer, and not a pointer toolkit: it ships no font and does no
//! hit testing.
//!
//! Not generic past one byte a pixel — [`Surface`] is bounded on [`ByteColor`],
//! so an `Rgb565` panel does not compile against it.
//!
//! # Features
//!
//! [`surface`], [`color`], [`geometry`] and [`mod@panic`] are always present;
//! [`widgets`] and [`preview`] are features, so a caller pays only for what it
//! draws.
//!
//! # Example
//!
//! ```
//! use panelkit::{color::Abgr2222, surface::Surface, widgets::Marks};
//!
//! let mut fb = [0u8; 240 * 240];
//! let mut s = Surface::<Abgr2222>::round(&mut fb, 240, 240).unwrap();
//! s.clear(Abgr2222::BLACK);
//! s.fill_rect(60, 100, 120, 40, Abgr2222::WHITE);
//! # #[cfg(feature = "widgets")]
//! Marks::dots(198, Abgr2222::WHITE, Abgr2222::GREY).draw(&mut s, 4, 1);
//! ```

#![cfg_attr(not(any(test, feature = "std")), no_std)]
#![deny(missing_docs)]
#![cfg_attr(docsrs, feature(doc_cfg))]

pub mod color;
pub mod geometry;
pub mod surface;

#[cfg(feature = "widgets")]
#[cfg_attr(docsrs, doc(cfg(feature = "widgets")))]
pub mod widgets;

#[cfg(feature = "preview")]
#[cfg_attr(docsrs, doc(cfg(feature = "preview")))]
pub mod preview;

pub mod panic;

pub use color::Abgr2222;
pub use surface::{ByteColor, Clip, Surface};
