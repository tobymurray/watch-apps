#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(not(feature = "std"))]
use core::fmt::Write as _;

use embedded_graphics::{
    pixelcolor::{raw::RawU8, PixelColor},
    prelude::*,
    primitives::{
        CornerRadii, CornerRadiiBuilder, Line, PrimitiveStyle, PrimitiveStyleBuilder, Rectangle,
        RoundedRectangle,
    },
};
use textkit::{faces, Align, Canvas, Face};

#[cfg(not(feature = "std"))]
extern "C" {
    fn unit_toggle_host_panic(msg: *const u8, len: u32);
}

#[cfg(not(feature = "std"))]
#[panic_handler]
fn on_panic(info: &core::panic::PanicInfo) -> ! {
    let mut msg = Buf::<192>::new();
    if let Some(loc) = info.location() {
        let _ = write!(msg, "{}:{}: ", loc.file(), loc.line());
    }
    let _ = write!(msg, "{}", info.message());

    let s = msg.as_str();
    unsafe { unit_toggle_host_panic(s.as_ptr(), s.len() as u32) };
    loop {}
}

/// What the last read or write achieved -- each leaves the setting somewhere
/// different, so each draws a different screen.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Status {
    /// Read, and any change also reached settings.json.
    Ok,
    /// The gate refused, so the choice cannot be moved. The units are still
    /// known: they come from a supported message, which needs no gate.
    Unsupported,
    /// The units could not be confirmed at all, so `imperial` means nothing.
    Unreadable,
    /// The live value changed and took effect, but the file was not written.
    NotSaved,
    /// Saving is switched off, so the change is live only. Nothing went wrong.
    LiveOnly,
}

impl Status {
    /// Anything unrecognised reads as `Unreadable`, so a value this build does
    /// not know about cannot be drawn as a confident answer.
    fn from_u8(raw: u8) -> Status {
        match raw {
            0 => Status::Ok,
            1 => Status::Unsupported,
            3 => Status::NotSaved,
            4 => Status::LiveOnly,
            _ => Status::Unreadable,
        }
    }

    /// True when the units are known, whether or not they can be changed here.
    fn shows_a_readable_choice(self) -> bool {
        self != Status::Unreadable
    }
}

/// Mirrors `unit_toggle_state` (`unit_toggle_gui.h`) field for field: a
/// read-only view of the real watch-wide units setting, not app state of its own.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct State {
    pub imperial: u8,
    pub known: u8,
    pub status: u8,
    pub _pad: [u8; 1],
}

impl State {
    fn is_imperial(&self) -> bool {
        self.imperial != 0
    }

    fn is_known(&self) -> bool {
        self.known != 0
    }

    fn status(&self) -> Status {
        Status::from_u8(self.status)
    }
}

const ALPHA_SHIFT: u8 = 6;
const BLUE_SHIFT: u8 = 4;
const GREEN_SHIFT: u8 = 2;
const RED_SHIFT: u8 = 0;
const CHANNEL_MASK: u8 = 0b11;
const CHANNEL_BITS: u8 = 2;
const ALPHA_OPAQUE: u8 = 0b11;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Abgr2222(pub u8);

const fn keep_high_bits(channel: u8) -> u8 {
    (channel >> (8 - CHANNEL_BITS)) & CHANNEL_MASK
}

impl Abgr2222 {
    pub const fn from_levels(r2: u8, g2: u8, b2: u8) -> Self {
        Abgr2222(
            (ALPHA_OPAQUE << ALPHA_SHIFT)
                | ((b2 & CHANNEL_MASK) << BLUE_SHIFT)
                | ((g2 & CHANNEL_MASK) << GREEN_SHIFT)
                | ((r2 & CHANNEL_MASK) << RED_SHIFT),
        )
    }

    pub const fn rgb(r: u8, g: u8, b: u8) -> Self {
        Abgr2222::from_levels(keep_high_bits(r), keep_high_bits(g), keep_high_bits(b))
    }

    pub const BLACK: Abgr2222 = Abgr2222::rgb(0, 0, 0);
    pub const WHITE: Abgr2222 = Abgr2222::rgb(255, 255, 255);
    pub const GRAY: Abgr2222 = Abgr2222::rgb(170, 170, 170);
    pub const DARK_GRAY: Abgr2222 = Abgr2222::rgb(85, 85, 85);
    pub const AMBER: Abgr2222 = Abgr2222::rgb(255, 170, 0);
}

impl Default for Abgr2222 {
    fn default() -> Self {
        Abgr2222::BLACK
    }
}

impl PixelColor for Abgr2222 {
    type Raw = RawU8;
}

// Every label on this screen is bright text on a dark fill: RustGuiPoc's
// hardware runs found dark thin glyphs on a light fill drop out on this panel
// (Docs/FINDINGS.md), so light-on-dark is a rule here, not a preference. It is
// why the chosen side is marked by a *dark* grey fill rather than a bright one
// -- a bright fill would need dark text on it.
const GROUND: Abgr2222 = Abgr2222::BLACK;
const HEADING: Abgr2222 = Abgr2222::WHITE;
const CHROME: Abgr2222 = Abgr2222::GRAY;
const CHOSEN_FILL: Abgr2222 = Abgr2222::DARK_GRAY;
const CHOSEN_TEXT: Abgr2222 = Abgr2222::WHITE;
const UNCHOSEN_TEXT: Abgr2222 = Abgr2222::GRAY;
const WARN_ACCENT: Abgr2222 = Abgr2222::AMBER;

struct FrameBuf<'a> {
    buf: &'a mut [u8],
    w: u32,
    h: u32,
}

impl OriginDimensions for FrameBuf<'_> {
    fn size(&self) -> Size {
        Size::new(self.w, self.h)
    }
}

impl DrawTarget for FrameBuf<'_> {
    type Color = Abgr2222;
    type Error = core::convert::Infallible;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        let (w, h) = (self.w as i32, self.h as i32);
        for Pixel(coord, color) in pixels {
            if coord.x >= 0 && coord.y >= 0 && coord.x < w && coord.y < h {
                let idx = (coord.y as u32 * self.w + coord.x as u32) as usize;
                self.buf[idx] = color.0;
            }
        }
        Ok(())
    }
}

#[cfg(not(feature = "std"))]
struct Buf<const N: usize> {
    b: [u8; N],
    n: usize,
}

#[cfg(not(feature = "std"))]
impl<const N: usize> Buf<N> {
    fn new() -> Self {
        Buf { b: [0; N], n: 0 }
    }

    fn as_str(&self) -> &str {
        core::str::from_utf8(&self.b[..self.n]).unwrap_or("")
    }
}

#[cfg(not(feature = "std"))]
impl<const N: usize> core::fmt::Write for Buf<N> {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        for &c in s.as_bytes() {
            if self.n >= N {
                return Err(core::fmt::Error);
            }
            self.b[self.n] = c;
            self.n += 1;
        }
        Ok(())
    }
}

// Layout: one screen, nothing to configure, so every position is a literal
// rather than something computed from panel geometry -- this app only ever
// draws on the 240x240 round panel every UNA Watch has.
const PANEL_CX: i32 = 120;
const TITLE_BASELINE_Y: i32 = 46;

/// MEASURED: `METRIC` is 67 px and `IMPERIAL` 80 px in the word face, so equal
/// halves of 102 px leave the wider label 22 px of padding. The whole control
/// spans x 18..222, and the round mask's chord at its corner rows (92 and 148)
/// is 233 px wide -- re-measure with `textkit`'s `measure` example if either
/// label changes.
const SEG_W: i32 = 204;
const SEG_H: i32 = 56;
const SEG_X: i32 = PANEL_CX - SEG_W / 2;
const SEG_Y: i32 = 92;
const SEG_RADIUS: i32 = 12;
const SEG_STROKE_W: u32 = 2;
const SEG_HALF_W: i32 = SEG_W / 2;
const METRIC_CX: i32 = SEG_X + SEG_HALF_W / 2;
const IMPERIAL_CX: i32 = SEG_X + SEG_HALF_W + SEG_HALF_W / 2;
/// The word face's ascent is 19, so this centres the caps in the control.
const SEG_TEXT_BASELINE_Y: i32 = SEG_Y + SEG_H / 2 + 9;

const LABEL_BASELINE_Y: i32 = SEG_Y + SEG_H + 32;
/// MEASURED: the lit chord is 129 px wide at row 220 against 122 at row 222, and
/// the widest footer here is 118 px in its face. Re-measure with `textkit`'s
/// `measure` example if a string or face changes.
const FOOTER_BASELINE_Y: i32 = 220;

/// Poppins SemiBold for every word, Regular for the button hint, from the atlases
/// TextKit generates; `Docs/TEXT.md` at the repo root is why these and not others.
static WORD_FACE: &Face = &faces::SEMIBOLD_18_ASCII;
static HINT_FACE: &Face = &faces::REGULAR_12_ASCII;

fn text(fb: &mut FrameBuf, face: &Face, s: &str, x: i32, baseline: i32, color: Abgr2222) {
    let mut canvas = Canvas::round(fb.buf, fb.w, fb.h);
    face.draw(&mut canvas, s, x, baseline, Align::Center, color.0);
}

/// The chosen half's fill, or none when nothing may be claimed as chosen.
fn chosen_half_x(state: &State) -> Option<i32> {
    if !state.is_known() || !state.status().shows_a_readable_choice() {
        return None;
    }
    Some(if state.is_imperial() { SEG_X + SEG_HALF_W } else { SEG_X })
}

fn draw_choice(fb: &mut FrameBuf, state: &State) {
    // The outline carries the same news as the footer, so a glance gets it
    // without reading.
    let outline = match state.status() {
        _ if !state.is_known() => WARN_ACCENT,
        Status::NotSaved => WARN_ACCENT,
        Status::Unsupported => CHROME,
        _ => HEADING,
    };

    // Only the half's outer corners are rounded, so the fill cannot show
    // outside the control's own curve.
    if let Some(x) = chosen_half_x(state) {
        let round = Size::new(SEG_RADIUS as u32, SEG_RADIUS as u32);
        let corners = if x == SEG_X {
            CornerRadiiBuilder::new().top_left(round).bottom_left(round).build()
        } else {
            CornerRadiiBuilder::new().top_right(round).bottom_right(round).build()
        };
        RoundedRectangle::new(
            Rectangle::new(Point::new(x, SEG_Y), Size::new(SEG_HALF_W as u32, SEG_H as u32)),
            corners,
        )
        .into_styled(PrimitiveStyle::with_fill(CHOSEN_FILL))
        .draw(fb)
        .ok();
    }

    RoundedRectangle::new(
        Rectangle::new(Point::new(SEG_X, SEG_Y), Size::new(SEG_W as u32, SEG_H as u32)),
        CornerRadii::new(Size::new(SEG_RADIUS as u32, SEG_RADIUS as u32)),
    )
    .into_styled(
        PrimitiveStyleBuilder::new()
            .stroke_color(outline)
            .stroke_width(SEG_STROKE_W)
            .build(),
    )
    .draw(fb)
    .ok();

    Line::new(
        Point::new(SEG_X + SEG_HALF_W, SEG_Y),
        Point::new(SEG_X + SEG_HALF_W, SEG_Y + SEG_H),
    )
    .into_styled(PrimitiveStyle::with_stroke(outline, SEG_STROKE_W))
    .draw(fb)
    .ok();

    let chosen = chosen_half_x(state);
    let metric_color = if chosen == Some(SEG_X) { CHOSEN_TEXT } else { UNCHOSEN_TEXT };
    let imperial_color =
        if chosen == Some(SEG_X + SEG_HALF_W) { CHOSEN_TEXT } else { UNCHOSEN_TEXT };

    text(fb, WORD_FACE, LABEL_METRIC, METRIC_CX, SEG_TEXT_BASELINE_Y, metric_color);
    text(fb, WORD_FACE, LABEL_IMPERIAL, IMPERIAL_CX, SEG_TEXT_BASELINE_Y, imperial_color);
}

fn draw(fb: &mut FrameBuf, state: &State) {
    text(fb, WORD_FACE, TITLE, PANEL_CX, TITLE_BASELINE_Y, HEADING);

    // Nothing to show a choice about: the units themselves could not be read,
    // so drawing the control at all would be inventing one of its two answers.
    if !state.status().shows_a_readable_choice() {
        text(fb, WORD_FACE, LABEL_UNKNOWN, PANEL_CX, LABEL_BASELINE_Y, WARN_ACCENT);
        text(fb, HINT_FACE, FOOTER_UNKNOWN, PANEL_CX, FOOTER_BASELINE_Y, CHROME);
        return;
    }

    draw_choice(fb, state);

    // Unsupported still draws the control: the units came from a supported
    // message, so they are true whatever the gate decided.
    let (label, label_color) = if !state.is_known() {
        (LABEL_UNKNOWN, WARN_ACCENT)
    } else {
        match state.status() {
            Status::NotSaved => (LABEL_NOT_SAVED, WARN_ACCENT),
            Status::Unsupported => (LABEL_VIEW_ONLY, CHROME),
            _ => ("", HEADING),
        }
    };
    if !label.is_empty() {
        text(fb, WORD_FACE, label, PANEL_CX, LABEL_BASELINE_Y, label_color);
    }

    let footer = match state.status() {
        Status::Unsupported => FOOTER_UNSUPPORTED,
        Status::NotSaved | Status::LiveOnly => FOOTER_NOT_SAVED,
        _ => FOOTER,
    };
    text(fb, HINT_FACE, footer, PANEL_CX, FOOTER_BASELINE_Y, CHROME);
}

const TITLE: &str = "UNITS";
const LABEL_METRIC: &str = "METRIC";
const LABEL_IMPERIAL: &str = "IMPERIAL";
const LABEL_UNKNOWN: &str = "UNKNOWN";
const LABEL_NOT_SAVED: &str = "NOT SAVED";
const LABEL_VIEW_ONLY: &str = "VIEW ONLY";
const FOOTER: &str = "R1 SWITCH  R2 BACK";
const FOOTER_NOT_SAVED: &str = "REVERTS ON REBOOT";
const FOOTER_UNSUPPORTED: &str = "NEEDS WATCH 1.4.0";
const FOOTER_UNKNOWN: &str = "R2 BACK";

pub fn render(buf: &mut [u8], width: u32, height: u32, state: &State) {
    if width == 0 || height == 0 {
        return;
    }
    let needed = (width as usize).saturating_mul(height as usize);
    if buf.len() < needed {
        return;
    }

    let mut fb = FrameBuf { buf: &mut buf[..needed], w: width, h: height };
    fb.buf.fill(GROUND.0);
    draw(&mut fb, state);
}

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// Must walk the same values in the same order as `unit_toggle_abi::fingerprint()`
/// in unit_toggle_gui.h.
const fn abi_fingerprint() -> u32 {
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<State>());
    let h = fnv1a(h, core::mem::align_of::<State>());
    let h = fnv1a(h, core::mem::offset_of!(State, imperial));
    let h = fnv1a(h, core::mem::offset_of!(State, known));
    let h = fnv1a(h, core::mem::offset_of!(State, status));
    fnv1a(h, core::mem::offset_of!(State, _pad))
}

/// Lets the caller confirm it was linked against the archive it thinks it was.
/// The compile-time assertions below cannot do this: a stale archive and a newer
/// header each satisfy their own, having been compiled at different times.
#[no_mangle]
pub extern "C" fn unit_toggle_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

// Per field, because a size check passes when two fields are swapped.
// unit_toggle_gui.h asserts the same offsets, so a hand edit to either
// declaration breaks a build.
const _: () = assert!(core::mem::size_of::<State>() == 4);
const _: () = assert!(core::mem::align_of::<State>() == 1);
const _: () = assert!(core::mem::offset_of!(State, imperial) == 0);
const _: () = assert!(core::mem::offset_of!(State, known) == 1);
const _: () = assert!(core::mem::offset_of!(State, status) == 2);
const _: () = assert!(core::mem::offset_of!(State, _pad) == 3);

/// # Safety
/// `buf` must point to at least `buf_len` writable bytes and `state` to a valid
/// `unit_toggle_state`, both valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn unit_toggle_render(
    buf: *mut u8,
    buf_len: u32,
    width: u16,
    height: u16,
    state: *const State,
) {
    if buf.is_null() || state.is_null() || buf_len == 0 || width == 0 || height == 0 {
        return;
    }
    let slice = core::slice::from_raw_parts_mut(buf, buf_len as usize);
    render(slice, width as u32, height as u32, &*state);
}

#[cfg(test)]
mod tests {
    use super::*;

    const W: u32 = 240;
    const H: u32 = 240;
    const C_STRUCT_SIZE: usize = 4;
    const C_STRUCT_ALIGN: usize = 1;

    fn metric() -> State {
        State { imperial: 0, known: 1, status: 0, _pad: [0; 1] }
    }

    fn imperial() -> State {
        State { imperial: 1, known: 1, status: 0, _pad: [0; 1] }
    }

    fn unknown() -> State {
        State { imperial: 0, known: 0, status: 2, _pad: [0; 1] }
    }

    fn not_saved() -> State {
        State { imperial: 1, known: 1, status: 3, _pad: [0; 1] }
    }

    fn live_only() -> State {
        State { imperial: 1, known: 1, status: 4, _pad: [0; 1] }
    }

    /// Gate refused, units still `known`: reading them needs no gate.
    fn unsupported() -> State {
        State { imperial: 1, known: 1, status: 1, _pad: [0; 1] }
    }

    fn frame(state: &State) -> Vec<u8> {
        let mut buf = vec![0u8; (W * H) as usize];
        render(&mut buf, W, H, state);
        buf
    }

    fn px(buf: &[u8], x: i32, y: i32) -> u8 {
        buf[(y as u32 * W + x as u32) as usize]
    }

    /// A point well inside a half, clear of both the outline and the glyphs.
    const METRIC_PROBE: (i32, i32) = (SEG_X + 8, SEG_Y + 8);
    const IMPERIAL_PROBE: (i32, i32) = (SEG_X + SEG_W - 8, SEG_Y + 8);

    #[test]
    fn state_layout_matches_c() {
        assert_eq!(core::mem::size_of::<State>(), C_STRUCT_SIZE);
        assert_eq!(core::mem::align_of::<State>(), C_STRUCT_ALIGN);
    }

    #[test]
    fn abi_fingerprint_is_stable() {
        assert_eq!(unit_toggle_abi_fingerprint(), abi_fingerprint());
    }

    #[test]
    fn undersized_buffer_is_a_no_op() {
        let mut buf = vec![0u8; 10];
        render(&mut buf, W, H, &metric());
        assert!(buf.iter().all(|&b| b == 0));
    }

    /// The one thing this screen has to get right: the filled half is the one
    /// the setting actually says, and only that one.
    #[test]
    fn filled_half_follows_state() {
        let m = frame(&metric());
        assert_eq!(px(&m, METRIC_PROBE.0, METRIC_PROBE.1), CHOSEN_FILL.0);
        assert_eq!(px(&m, IMPERIAL_PROBE.0, IMPERIAL_PROBE.1), GROUND.0);

        let i = frame(&imperial());
        assert_eq!(px(&i, METRIC_PROBE.0, METRIC_PROBE.1), GROUND.0);
        assert_eq!(px(&i, IMPERIAL_PROBE.0, IMPERIAL_PROBE.1), CHOSEN_FILL.0);
    }

    /// A change that took effect but never reached the file must not draw the
    /// same confident white as one that saved.
    #[test]
    fn not_saved_is_visually_distinct_from_a_saved_choice() {
        let saved = frame(&imperial());
        let unsaved = frame(&not_saved());
        assert_ne!(saved, unsaved);

        // The outline is where the difference has to live: both fill the same
        // half, because both are telling the truth about the live value.
        assert_eq!(
            px(&unsaved, IMPERIAL_PROBE.0, IMPERIAL_PROBE.1),
            px(&saved, IMPERIAL_PROBE.0, IMPERIAL_PROBE.1)
        );
        let outline_y = SEG_Y + SEG_H / 2;
        assert_eq!(px(&saved, SEG_X, outline_y), HEADING.0);
        assert_eq!(px(&unsaved, SEG_X, outline_y), WARN_ACCENT.0);
    }

    /// The chosen half is a rectangle inside a rounded control, so its outer
    /// corners have to be rounded too.
    #[test]
    fn the_filled_half_stays_inside_the_rounded_outline() {
        for (state, corner_x) in
            [(metric(), SEG_X), (imperial(), SEG_X + SEG_W - 1)]
        {
            let f = frame(&state);
            for (x, y) in [(corner_x, SEG_Y), (corner_x, SEG_Y + SEG_H - 1)] {
                assert_eq!(
                    px(&f, x, y),
                    GROUND.0,
                    "fill or stroke reached the square corner ({x},{y})"
                );
            }
        }
    }

    #[test]
    fn unknown_fills_neither_half() {
        let u = frame(&unknown());
        assert_eq!(px(&u, METRIC_PROBE.0, METRIC_PROBE.1), GROUND.0);
        assert_eq!(px(&u, IMPERIAL_PROBE.0, IMPERIAL_PROBE.1), GROUND.0);
    }

    /// The gate refusing must not hide the setting; it only stops it changing.
    #[test]
    fn unsupported_still_shows_which_units_are_set() {
        let u = frame(&unsupported());
        assert_eq!(px(&u, IMPERIAL_PROBE.0, IMPERIAL_PROBE.1), CHOSEN_FILL.0);
        assert_eq!(px(&u, METRIC_PROBE.0, METRIC_PROBE.1), GROUND.0);
    }

    /// ...but it must not look like a choice R1 can move.
    #[test]
    fn unsupported_is_visually_distinct_from_a_changeable_choice() {
        let changeable = frame(&imperial());
        let locked = frame(&unsupported());
        assert_ne!(changeable, locked);

        let outline_y = SEG_Y + SEG_H / 2;
        assert_eq!(px(&changeable, SEG_X, outline_y), HEADING.0);
        assert_eq!(px(&locked, SEG_X, outline_y), CHROME.0);
    }

    /// Nothing readable to show, so nothing that looks like an answer is drawn.
    /// Nothing readable to show, so nothing that looks like an answer is drawn.
    #[test]
    fn unreadable_draws_no_control_at_all() {
        let f = frame(&unknown());
        let outline_y = SEG_Y + SEG_H / 2;
        assert_eq!(px(&f, SEG_X, outline_y), GROUND.0);
    }

    #[test]
    fn live_only_and_saved_differ_only_in_the_footer() {
        let saved = frame(&imperial());
        let live = frame(&live_only());
        assert_ne!(saved, live);

        let outline_y = SEG_Y + SEG_H / 2;
        assert_eq!(px(&live, SEG_X, outline_y), HEADING.0);
        assert_eq!(
            px(&live, IMPERIAL_PROBE.0, IMPERIAL_PROBE.1),
            px(&saved, IMPERIAL_PROBE.0, IMPERIAL_PROBE.1)
        );
    }

    /// Every state has to survive the bezel: the panel is round, and anything
    /// outside the inscribed circle is behind it.
    #[test]
    fn nothing_is_drawn_outside_the_round_mask() {
        let r = (W / 2) as i32;
        for state in [metric(), imperial(), unknown(), not_saved(), live_only(), unsupported()] {
            let f = frame(&state);
            for y in 0..H as i32 {
                for x in 0..W as i32 {
                    let (dx, dy) = (x - r, y - r);
                    if dx * dx + dy * dy > r * r && px(&f, x, y) != GROUND.0 {
                        panic!("status {} lit ({x},{y}) outside the mask", state.status);
                    }
                }
            }
        }
    }

    /// An unrecognised status must degrade to the one screen that claims
    /// nothing, not to a confident answer.
    #[test]
    fn an_unknown_status_reads_as_unreadable() {
        assert_eq!(Status::from_u8(200), Status::Unreadable);
        let odd = State { imperial: 1, known: 1, status: 200, _pad: [0; 1] };
        let f = frame(&odd);
        let outline_y = SEG_Y + SEG_H / 2;
        assert_eq!(px(&f, SEG_X, outline_y), GROUND.0);
    }
}
