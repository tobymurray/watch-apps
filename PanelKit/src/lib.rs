//! Immediate-mode GUI primitives for round, low-bit-depth, button-driven panels.
//!
//! Design record, with the measurements and the architectures rejected:
//! `PanelKit/README.md`.
//!
//! # What it is not
//!
//! Not a replacement for [`embedded_graphics`] — a citizen of it. The surface is
//! a [`DrawTarget`](embedded_graphics::draw_target::DrawTarget) and the colours
//! are [`PixelColor`](embedded_graphics::pixelcolor::PixelColor).
//!
//! Not a text renderer: [`text::TextFace`] is the interface the widgets draw
//! through, and this crate ships no font.
//!
//! Not a pointer toolkit. There is no hit testing; [`nav`] is a focus ring.
//!
//! Not generic past one byte a pixel — [`Surface`] is bounded on [`ByteColor`],
//! so an `Rgb565` panel does not compile against it.
//!
//! # Tiers
//!
//! Each is a feature, so a caller pays only for what it draws: tier 0 is
//! [`surface`], [`color`], [`geometry`] and [`mod@panic`]; tier 1 is [`draw`]
//! and [`dither`]; tier 2 is [`widgets`]; tier 3 is [`nav`].
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
pub mod draw;
pub mod geometry;
pub mod surface;
pub mod text;

#[cfg(feature = "dither")]
#[cfg_attr(docsrs, doc(cfg(feature = "dither")))]
pub mod dither;

#[cfg(feature = "widgets")]
#[cfg_attr(docsrs, doc(cfg(feature = "widgets")))]
pub mod widgets;

#[cfg(feature = "scenes")]
#[cfg_attr(docsrs, doc(cfg(feature = "scenes")))]
pub mod scene;

#[cfg(feature = "preview")]
#[cfg_attr(docsrs, doc(cfg(feature = "preview")))]
pub mod preview;

pub mod nav;
pub mod panic;

pub use color::Abgr2222;
pub use surface::{ByteColor, Clip, Surface};
