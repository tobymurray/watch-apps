//! Tier 2: widgets with two existing callers or a measured platform reason.
//!
//! Nothing enters here on the grounds that it looks reusable. Each item below
//! names the callers it was distilled from.

use crate::surface::{ByteColor, Surface};

/// A centred row of marks saying which of `count` screens is showing.
///
/// Distilled from three independent implementations — `RustGuiPoc::page_dots`,
/// `Barcode::draw_pager` and `SettingsEditor::draw_position` — which used three
/// different centring expressions and two different coordinate conventions.
/// All three centre correctly; what they disagree about is rounding, and only
/// at even counts: `Barcode`'s `(panel - total) / 2` truncates the whole
/// quantity where the others truncate each term, which puts its row half a
/// pixel left of centre at counts of 2, 4, 6 and 8. This one truncates the
/// span, which is the form that stays centred at every count — see
/// `a_row_is_centred_at_every_count`.
#[derive(Clone, Copy, Debug)]
pub struct Marks<C> {
    /// Centre-to-centre distance between marks.
    pub pitch: i32,
    /// Width of one mark.
    pub width: i32,
    /// Height of one mark.
    pub height: i32,
    /// Top of the row.
    pub y: i32,
    /// The mark for the page being shown.
    pub on: C,
    /// The mark for every other page.
    pub off: C,
}

impl<C: ByteColor> Marks<C> {
    /// A round dot row, in the proportions the three callers converged on.
    pub const fn dots(y: i32, on: C, off: C) -> Self {
        Marks { pitch: 14, width: 6, height: 6, y, on, off }
    }

    /// Total ink width of a row of `count` marks.
    pub const fn span(&self, count: i32) -> i32 {
        if count <= 0 {
            0
        } else {
            (count - 1) * self.pitch + self.width
        }
    }

    /// The x of the first mark's left edge, centred on `panel_width`.
    pub const fn left(&self, count: i32, panel_width: i32) -> i32 {
        panel_width / 2 - ((count - 1) * self.pitch) / 2 - self.width / 2
    }

    /// Draws the row. A single page draws nothing: a pager that says "1 of 1"
    /// is a mark the wearer has to interpret to learn there is nothing to page.
    pub fn draw(&self, s: &mut Surface<C>, count: i32, index: i32) {
        if count < 2 {
            return;
        }
        let left = self.left(count, s.width());
        for i in 0..count {
            let color = if i == index { self.on } else { self.off };
            s.fill_rect(left + i * self.pitch, self.y, self.width, self.height, color);
        }
    }
}

/// A two-state pill, lit for on and outlined for off.
///
/// Two callers: `NotifyToggle` and `UnitToggle`, which after normalising names
/// are 56% line-identical and differ here only in their palettes.
#[derive(Clone, Copy, Debug)]
pub struct Pill<C> {
    /// Left edge.
    pub x: i32,
    /// Top edge.
    pub y: i32,
    /// Overall width, knob included.
    pub width: i32,
    /// Overall height, which is also the knob's diameter.
    pub height: i32,
    /// The track when on.
    pub on: C,
    /// The knob when on, which reads as a hole in the lit track.
    pub off: C,
    /// Drawn behind the knob when off, so the pill reads as a track.
    pub track: C,
}

impl<C: ByteColor> Pill<C> {
    /// Draws the pill in the given state.
    pub fn draw(&self, s: &mut Surface<C>, on: bool) {
        let knob = self.height;
        s.fill_rect(self.x, self.y, self.width, self.height, if on { self.on } else { self.track });
        let knob_x = if on { self.x + self.width - knob } else { self.x };
        s.fill_rect(knob_x, self.y, knob, knob, if on { self.off } else { self.on });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::color::Abgr2222;

    const W: u32 = 240;
    const H: u32 = 240;

    fn extent(buf: &[u8], row: i32) -> Option<(i32, i32)> {
        let lit: Vec<i32> = (0..W as i32)
            .filter(|&x| buf[(row * W as i32 + x) as usize] != Abgr2222::BLACK.0)
            .collect();
        Some((*lit.first()?, *lit.last()? + 1))
    }

    /// The defect the three source implementations disagreed about, held as a
    /// test: the ink is centred on the panel at every count, odd and even.
    /// Falsified by a pitch or width that makes an exactly centred row
    /// impossible, which is when this should start failing.
    #[test]
    fn a_row_is_centred_at_every_count() {
        let marks = Marks::dots(198, Abgr2222::WHITE, Abgr2222::GREY);
        for count in 2..=8 {
            let mut buf = vec![0u8; (W * H) as usize];
            {
                let mut s = Surface::<Abgr2222>::round(&mut buf, W, H).unwrap();
                s.clear(Abgr2222::BLACK);
                marks.draw(&mut s, count, 0);
            }
            let (a, b) = extent(&buf, 200).expect("nothing drawn");
            let centre = (a + b) as f32 / 2.0;
            assert!(
                (centre - W as f32 / 2.0).abs() < 0.01,
                "count {count}: ink spans [{a},{b}), centre {centre}, want {}",
                W / 2
            );
        }
    }

    #[test]
    fn one_page_draws_nothing() {
        let mut buf = vec![0u8; (W * H) as usize];
        {
            let mut s = Surface::<Abgr2222>::round(&mut buf, W, H).unwrap();
            s.clear(Abgr2222::BLACK);
            Marks::dots(198, Abgr2222::WHITE, Abgr2222::GREY).draw(&mut s, 1, 0);
        }
        assert!(buf.iter().all(|&b| b == Abgr2222::BLACK.0));
    }

    #[test]
    fn the_current_mark_differs_from_the_rest() {
        let mut buf = vec![0u8; (W * H) as usize];
        let marks = Marks::dots(198, Abgr2222::WHITE, Abgr2222::GREY);
        {
            let mut s = Surface::<Abgr2222>::round(&mut buf, W, H).unwrap();
            s.clear(Abgr2222::BLACK);
            marks.draw(&mut s, 5, 2);
        }
        let left = marks.left(5, W as i32);
        let at = |i: i32| buf[(200 * W as i32 + left + i * marks.pitch + 1) as usize];
        assert_eq!(at(2), Abgr2222::WHITE.to_byte());
        assert_eq!(at(0), Abgr2222::GREY.to_byte());
        assert_eq!(at(4), Abgr2222::GREY.to_byte());
    }

    #[test]
    fn a_span_matches_what_was_drawn() {
        let marks = Marks::dots(198, Abgr2222::WHITE, Abgr2222::GREY);
        for count in 2..=8 {
            let mut buf = vec![0u8; (W * H) as usize];
            {
                let mut s = Surface::<Abgr2222>::round(&mut buf, W, H).unwrap();
                s.clear(Abgr2222::BLACK);
                marks.draw(&mut s, count, 0);
            }
            let (a, b) = extent(&buf, 200).unwrap();
            assert_eq!(b - a, marks.span(count), "count {count}");
        }
    }

    #[test]
    fn a_pill_shows_its_state() {
        let pill = Pill {
            x: 90,
            y: 110,
            width: 60,
            height: 24,
            on: Abgr2222::GREEN,
            off: Abgr2222::BLACK,
            track: Abgr2222::DARK_GREY,
        };
        for state in [false, true] {
            let mut buf = vec![0u8; (W * H) as usize];
            {
                let mut s = Surface::<Abgr2222>::round(&mut buf, W, H).unwrap();
                s.clear(Abgr2222::BLACK);
                pill.draw(&mut s, state);
            }
            let track = buf[(120 * W as i32 + 120) as usize];
            assert_eq!(
                track,
                if state { Abgr2222::GREEN.to_byte() } else { Abgr2222::DARK_GREY.to_byte() }
            );
        }
    }
}
