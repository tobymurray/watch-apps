//! `ABGR2222`: one byte a pixel, two bits a channel.
//!
//! Sixty-four colours, and the only greys are 0, 85, 170 and 255, so
//! anti-aliasing is a choice among two intermediate shades rather than 254.

use embedded_graphics::pixelcolor::{
    raw::{RawData, RawU8},
    PixelColor,
};

const ALPHA_SHIFT: u8 = 6;
const BLUE_SHIFT: u8 = 4;
const GREEN_SHIFT: u8 = 2;
const RED_SHIFT: u8 = 0;
const CHANNEL_MASK: u8 = 0b11;
const CHANNEL_BITS: u8 = 2;
const ALPHA_OPAQUE: u8 = 0b11;

/// The number of levels a channel holds, minus one.
pub const CHANNEL_MAX: u8 = 3;

/// The only greys this panel has.
pub const GREY_LEVELS: [u8; 4] = [0, 85, 170, 255];

/// An opaque `ABGR2222` pixel.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default, PartialOrd, Ord, Hash)]
pub struct Abgr2222(pub u8);

const fn keep_high_bits(channel: u8) -> u8 {
    (channel >> (8 - CHANNEL_BITS)) & CHANNEL_MASK
}

impl Abgr2222 {
    /// From per-channel levels of 0 to 3, which is what the panel actually has.
    pub const fn from_levels(r2: u8, g2: u8, b2: u8) -> Self {
        Abgr2222(
            (ALPHA_OPAQUE << ALPHA_SHIFT)
                | ((b2 & CHANNEL_MASK) << BLUE_SHIFT)
                | ((g2 & CHANNEL_MASK) << GREEN_SHIFT)
                | ((r2 & CHANNEL_MASK) << RED_SHIFT),
        )
    }

    /// From 8-bit channels, truncated. What asking for a colour the panel
    /// cannot show gets you, and the reason a ramp becomes four bands.
    pub const fn rgb(r: u8, g: u8, b: u8) -> Self {
        Abgr2222::from_levels(keep_high_bits(r), keep_high_bits(g), keep_high_bits(b))
    }

    /// The red channel's level, 0 to 3.
    pub const fn r(self) -> u8 {
        (self.0 >> RED_SHIFT) & CHANNEL_MASK
    }

    /// The green channel's level, 0 to 3.
    pub const fn g(self) -> u8 {
        (self.0 >> GREEN_SHIFT) & CHANNEL_MASK
    }

    /// The blue channel's level, 0 to 3.
    pub const fn b(self) -> u8 {
        (self.0 >> BLUE_SHIFT) & CHANNEL_MASK
    }

    /// Level 0 on every channel.
    pub const BLACK: Abgr2222 = Abgr2222::from_levels(0, 0, 0);
    /// Level 3 on every channel.
    pub const WHITE: Abgr2222 = Abgr2222::from_levels(3, 3, 3);
    /// The two intermediate greys, named for what they are used for.
    pub const GREY: Abgr2222 = Abgr2222::from_levels(2, 2, 2);
    /// The dimmer of the two intermediate greys.
    pub const DARK_GREY: Abgr2222 = Abgr2222::from_levels(1, 1, 1);
    /// Full red.
    pub const RED: Abgr2222 = Abgr2222::from_levels(3, 0, 0);
    /// Full green.
    pub const GREEN: Abgr2222 = Abgr2222::from_levels(0, 3, 0);
    /// Full blue.
    pub const BLUE: Abgr2222 = Abgr2222::from_levels(0, 0, 3);
    /// Full green and blue.
    pub const CYAN: Abgr2222 = Abgr2222::from_levels(0, 3, 3);
    /// Full red and green.
    pub const YELLOW: Abgr2222 = Abgr2222::from_levels(3, 3, 0);
    /// Red at 3, green at 2.
    pub const AMBER: Abgr2222 = Abgr2222::from_levels(3, 2, 0);
}

impl PixelColor for Abgr2222 {
    type Raw = RawU8;
}

impl From<RawU8> for Abgr2222 {
    fn from(raw: RawU8) -> Self {
        Abgr2222(raw.into_inner())
    }
}

impl From<Abgr2222> for RawU8 {
    fn from(color: Abgr2222) -> Self {
        RawU8::new(color.0)
    }
}

/// `ink` at `level` of three over `ground`, channel by channel, rounded: what a
/// partially covered pixel becomes.
pub fn shade(ink: Abgr2222, ground: Abgr2222, level: u8) -> Abgr2222 {
    let ch = |i: u8, g: u8| -> u8 {
        let (i, g, l) = (i as u16, g as u16, level.min(CHANNEL_MAX) as u16);
        ((g * (CHANNEL_MAX as u16 - l) + i * l + 1) / CHANNEL_MAX as u16) as u8
    };
    Abgr2222::from_levels(
        ch(ink.r(), ground.r()),
        ch(ink.g(), ground.g()),
        ch(ink.b(), ground.b()),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn black_and_white_are_opaque() {
        assert_eq!(Abgr2222::BLACK.0, 0xC0);
        assert_eq!(Abgr2222::WHITE.0, 0xFF);
    }

    /// The panel has four greys and no others; a kit that offered more would be
    /// promising a shade the glass cannot show.
    #[test]
    fn there_are_exactly_four_greys() {
        let mut seen = [false; 4];
        for c in (0..=255u8).map(Abgr2222) {
            if c.r() == c.g() && c.g() == c.b() {
                seen[c.r() as usize] = true;
            }
        }
        assert_eq!(seen.iter().filter(|&&s| s).count(), 4);
        assert_eq!(GREY_LEVELS.len(), 4);
    }

    #[test]
    fn rgb_truncates_to_the_level_below() {
        assert_eq!(Abgr2222::rgb(0, 0, 0), Abgr2222::BLACK);
        assert_eq!(Abgr2222::rgb(255, 255, 255), Abgr2222::WHITE);
        // Truncation keeps the top two bits, so the boundaries are 64, 128 and
        // 192 -- not the displayed greys 85, 170 and 255. 200 is above 192 and
        // lands on level 3, which the panel then shows as 255.
        assert_eq!(Abgr2222::rgb(191, 191, 191).r(), 2);
        assert_eq!(Abgr2222::rgb(192, 192, 192).r(), 3);
        assert_eq!(Abgr2222::rgb(200, 200, 200).r(), 3);
    }

    #[test]
    fn shade_reaches_both_ends() {
        assert_eq!(shade(Abgr2222::WHITE, Abgr2222::BLACK, 0), Abgr2222::BLACK);
        assert_eq!(shade(Abgr2222::WHITE, Abgr2222::BLACK, 3), Abgr2222::WHITE);
    }

    #[test]
    fn shade_is_monotonic() {
        let mut last = 0;
        for level in 0..=3 {
            let v = shade(Abgr2222::WHITE, Abgr2222::BLACK, level).r();
            assert!(v >= last, "level {level} went backwards");
            last = v;
        }
    }
}
