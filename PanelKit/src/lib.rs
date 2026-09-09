//! Immediate-mode GUI primitives for round, low-bit-depth, button-driven panels.
//!
//! # Why this exists and why it is small
//!
//! A panel that accepts **whole frames only** deletes the most complex
//! machinery in every competing framework. There is no partial update to
//! invalidate, so there is no retained widget tree, no damage tracking and no
//! dirty-rectangle arithmetic — a caller renders the whole frame from state
//! each tick, and that is not a limitation but the reason this crate can be a
//! fraction of the size of the alternatives without being less capable on this
//! class of device.
//!
//! What is left is the part nobody has packaged: the geometry of a round
//! display, the colour arithmetic of two bits a channel, and a navigation model
//! for four buttons rather than a pointer.
//!
//! # What it is not
//!
//! Not a replacement for [`embedded_graphics`] — a citizen of it. The surface
//! is a [`DrawTarget`](embedded_graphics::draw_target::DrawTarget), the colours
//! are [`PixelColor`](embedded_graphics::pixelcolor::PixelColor), and anything
//! in that ecosystem draws onto this one.
//!
//! Not a text renderer. [`text::TextFace`] is the interface the widgets need;
//! this crate ships no font.
//!
//! Not a pointer toolkit. There is no hit testing, because the devices this is
//! for have buttons.
//!
//! Not generic past one byte a pixel. [`Surface`] is generic over the colour
//! type but bounded on [`ByteColor`], so a 16bpp `Rgb565` panel does not
//! compile against it. That is the honest scope: low-bit-depth means one byte
//! a pixel here, and lifting it wants the stride arithmetic and the row fill
//! written against `PixelColor::Raw` rather than `u8`.
//!
//! # Tiers
//!
//! - **Tier 0** ([`surface`], [`color`], [`geometry`], [`mod@panic`]) — the parts
//!   every caller needs, and the ones that were byte-identical across seven
//!   independent copies before this crate existed.
//! - **Tier 1** ([`draw`], [`dither`]) — primitives whose constants came from
//!   a measurement, carried here with the measurement.
//! - **Tier 2** ([`widgets`]) — widgets with two existing callers or a measured
//!   platform reason, and nothing else.
//!
//! Each tier is a feature, so a renderer that draws a QR code pays for none of
//! the ones above it.
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

pub mod nav;
pub mod panic;

pub use color::Abgr2222;
pub use surface::{ByteColor, Clip, Surface};
