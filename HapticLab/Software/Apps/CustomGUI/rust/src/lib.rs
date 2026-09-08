//! HapticLab's renderer, as a pure function of one `Frame`. Layout reasoning:
//! `HapticLab/README.md`.
//!
//! PANEL. 240x240, round, 8bpp `ABGR2222`: four levels a channel, so every
//! colour named here is one of 0/170/255, and 85 is left out because it is the
//! first level to wash out in daylight on this reflective glass. Bright ink on
//! a dark ground is the only kind proven to render; a black-on-white readout
//! came back as a blank white band (`RustGuiPoc/Docs/FINDINGS.md`). The corners
//! of the square buffer sit behind the bezel, so nothing is anchored to one.
//! Falsified by a different display; `nothing_is_drawn_outside_the_bezel`,
//! `every_string_fits_its_chord` and `no_scene_relies_on_the_disc_clip` are
//! what hold the round part.

// `cargo test --features std`: the crate is no_std, and a test binary cannot
// link one whose panics do not unwind.
#![cfg_attr(not(feature = "std"), no_std)]

use textkit::{faces, Align, Canvas, Clip, Face};

#[cfg(not(feature = "std"))]
extern "C" {
    fn haptic_lab_gui_host_panic(msg: *const u8, len: u32);
}

/// Without this a panic hangs the GUI thread silently, and a session in
/// progress is a wrist-hour that cannot be repeated.
#[cfg(not(feature = "std"))]
#[panic_handler]
fn on_panic(_info: &core::panic::PanicInfo) -> ! {
    let s = b"panic";
    unsafe { haptic_lab_gui_host_panic(s.as_ptr(), s.len() as u32) };
    loop {}
}

/// The frames worth looking at. Host-only: the watch is handed frames.
#[cfg(feature = "std")]
pub mod scenes;

// -- Colour ------------------------------------------------------------------

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Abgr2222(pub u8);

const ALPHA_OPAQUE: u8 = 0b11;
const CHANNEL_MASK: u8 = 0b11;

/// Two bits a channel, so only the top two of an eight-bit level survive.
const fn keep_high_bits(channel: u8) -> u8 {
    (channel >> 6) & CHANNEL_MASK
}

impl Abgr2222 {
    pub const fn rgb(r: u8, g: u8, b: u8) -> Self {
        Abgr2222(
            (ALPHA_OPAQUE << 6)
                | (keep_high_bits(b) << 4)
                | (keep_high_bits(g) << 2)
                | keep_high_bits(r),
        )
    }
}

const BLACK: Abgr2222 = Abgr2222::rgb(0, 0, 0);
const WHITE: Abgr2222 = Abgr2222::rgb(255, 255, 255);
/// Present but not the thing being read.
const DIM: Abgr2222 = Abgr2222::rgb(170, 170, 170);
/// Not-what-you-wanted: a wrong answer, a miss, a run that ended early.
const AMBER: Abgr2222 = Abgr2222::rgb(255, 170, 0);

// -- Instructions ------------------------------------------------------------
// Every string the wearer reads, in one place, so the wrap test and the
// round-screen test enumerate exactly what the watch draws. The frame selects
// one by id and carries no prose of its own.
//
// They say what to do rather than what is happening, and none of them names or
// narrows the pattern about to be played.

pub const MSG_NONE: u8 = 0;
pub const MSG_MENU: u8 = 1;
pub const MSG_CALIBRATE: u8 = 2;
pub const MSG_CALIBRATE_DONE: u8 = 3;
pub const MSG_WHICH_ONE: u8 = 4;
pub const MSG_SWEEP: u8 = 5;
pub const MSG_STORAGE_FULL: u8 = 6;
pub const MSG_NO_FILES: u8 = 7;
pub const MSG_COUNT: u8 = 8;

/// Indexed by the ids above.
pub const MESSAGES: [&str; MSG_COUNT as usize] = [
    "",
    "The condition comes from session.json.",
    "Press each button once. The one you press lights up.",
    "All four seen. R1 begins.",
    "Which one was it?",
    "Arm still and flat. Nothing to answer.",
    "Storage cap reached. The run stopped and saved.",
    "Cannot write the run files. Nothing will be recorded.",
];

/// The Phase 0 questions, one per probe, in the order `Probe.hpp` builds them.
/// The count is checked against the frame at draw time rather than asserted
/// across two languages.
pub const PROBE_QUESTIONS: [&str; 11] = [
    "Three clicks. Do the second and third differ from the first?",
    "Three sharp clicks. Do the second and third differ?",
    "Three bumps. Do the second and third differ?",
    "Effect 127. Does anything play at all?",
    "Effect 128. Clamped, ignored, or refused?",
    "Effect 255. Clamped, ignored, or refused?",
    "Two clicks, 1270 ms apart. Time the gap.",
    "Two clicks, 10 ms apart. One buzz or two?",
    "Two full messages, no gap. Sixteen notes, or eight?",
    "A full message cut off after 300 ms.",
    "The count says twelve, the array holds eight.",
];

/// Short labels for the probe list, drawn above the question so the operator
/// can see which row of the ledger the run answers.
pub const PROBE_LABELS: [&str; PROBE_QUESTIONS.len()] = [
    "V1 CLICK 1-2-3",
    "V1 SHARP 4-5-6",
    "V1 BUMP 7-8-9",
    "V2 EFFECT 127",
    "V2 EFFECT 128",
    "V2 EFFECT 255",
    "V3 PAUSE 1270",
    "V3 PAUSE 10",
    "V4 QUEUE",
    "V4 INTERRUPT",
    "V5 COUNT 12",
];

fn message(id: u8) -> &'static str {
    MESSAGES.get(id as usize).copied().unwrap_or("")
}

// -- Framebuffer -------------------------------------------------------------

struct FrameBuf<'a> {
    buf: &'a mut [u8],
    w: u32,
    h: u32,
    /// The panel clips text to the disc. Every layout here is meant to fit
    /// without that, so the tests render both ways and compare: a difference
    /// means a string was inside the glass only because the clip took the end
    /// of it off.
    clip: Clip,
}

impl FrameBuf<'_> {
    fn canvas(&mut self) -> Canvas<'_> {
        match self.clip {
            Clip::Disc => Canvas::round(self.buf, self.w, self.h),
            Clip::Rect => Canvas::rect(self.buf, self.w, self.h),
        }
    }
}

/// A direct row-fill, which is the only shape here that is not a glyph.
fn fill_rect(fb: &mut FrameBuf, x: i32, y: i32, w: i32, h: i32, color: Abgr2222) {
    if w <= 0 || h <= 0 {
        return;
    }
    let (fb_w, fb_h) = (fb.w as i32, fb.h as i32);
    let x0 = x.max(0);
    let y0 = y.max(0);
    let x1 = (x + w).min(fb_w);
    let y1 = (y + h).min(fb_h);
    if x0 >= x1 || y0 >= y1 {
        return;
    }
    for row in y0..y1 {
        let start = (row * fb_w + x0) as usize;
        let end = (row * fb_w + x1) as usize;
        fb.buf[start..end].fill(color.0);
    }
}

// -- The C ABI frame ---------------------------------------------------------
// Mirrors haptic_lab_gui_frame (haptic_lab_gui.h); the fingerprint below is
// what checks it.

pub const SCREEN_MENU: u8 = 0;
pub const SCREEN_CALIBRATION: u8 = 1;
pub const SCREEN_LEARN: u8 = 2;
pub const SCREEN_WAIT: u8 = 3;
pub const SCREEN_PLAYING: u8 = 4;
pub const SCREEN_ANSWER: u8 = 5;
pub const SCREEN_FEEDBACK: u8 = 6;
pub const SCREEN_REST: u8 = 7;
pub const SCREEN_DONE: u8 = 8;
pub const SCREEN_ABORTED: u8 = 9;
pub const SCREEN_PROBE: u8 = 10;
pub const SCREEN_PROBE_SENT: u8 = 11;
pub const SCREEN_SWEEP: u8 = 12;

pub const FEEDBACK_NONE: u8 = 0;
pub const FEEDBACK_CORRECT: u8 = 1;
pub const FEEDBACK_WRONG: u8 = 2;
pub const FEEDBACK_MISS: u8 = 3;
pub const FEEDBACK_TIMEOUT: u8 = 4;

pub const NO_BUTTON: u8 = 0xFF;

pub const BLOCK_TRAINING: u8 = 0;

pub const CONTACT_WRIST: u8 = 0;
pub const MASKING_UNMASKED: u8 = 0;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Frame {
    pub modelled_duration_ms: u32,
    pub block_trials: u16,
    pub trials_total: u16,
    pub sweep_index: u16,
    pub sweep_count: u16,
    pub screen: u8,
    pub block: u8,
    pub block_count: u8,
    pub block_kind: u8,
    pub calibration_mask: u8,
    pub last_button: u8,
    pub feedback: u8,
    pub learn_index: u8,
    pub learn_count: u8,
    pub probe_index: u8,
    pub probe_count: u8,
    pub pattern_count: u8,
    pub message_id: u8,
    pub contact: u8,
    pub masking: u8,
    pub alt: [[u8; 9]; 3],
    pub name: [u8; 9],
}

impl Default for Frame {
    fn default() -> Self {
        Frame {
            modelled_duration_ms: 0,
            block_trials: 0,
            trials_total: 0,
            sweep_index: 0,
            sweep_count: 0,
            screen: SCREEN_MENU,
            block: 0,
            block_count: 0,
            block_kind: BLOCK_TRAINING,
            calibration_mask: 0,
            last_button: NO_BUTTON,
            feedback: FEEDBACK_NONE,
            learn_index: 0,
            learn_count: 0,
            probe_index: 0,
            probe_count: PROBE_QUESTIONS.len() as u8,
            pattern_count: 0,
            message_id: MSG_NONE,
            contact: CONTACT_WRIST,
            masking: MASKING_UNMASKED,
            alt: [[0; 9]; 3],
            name: [0; 9],
        }
    }
}

impl Frame {
    fn str_field(bytes: &[u8]) -> &str {
        let len = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
        core::str::from_utf8(&bytes[..len]).unwrap_or("")
    }

    pub fn alt_str(&self, slot: usize) -> &str {
        Self::str_field(&self.alt[slot])
    }

    pub fn name_str(&self) -> &str {
        Self::str_field(&self.name)
    }
}

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// Must walk the same values in the same order as
/// `haptic_lab_gui_abi::fingerprint()`.
const fn abi_fingerprint() -> u32 {
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<Frame>());
    let h = fnv1a(h, core::mem::align_of::<Frame>());
    let h = fnv1a(h, core::mem::offset_of!(Frame, modelled_duration_ms));
    let h = fnv1a(h, core::mem::offset_of!(Frame, block_trials));
    let h = fnv1a(h, core::mem::offset_of!(Frame, trials_total));
    let h = fnv1a(h, core::mem::offset_of!(Frame, sweep_index));
    let h = fnv1a(h, core::mem::offset_of!(Frame, sweep_count));
    let h = fnv1a(h, core::mem::offset_of!(Frame, screen));
    let h = fnv1a(h, core::mem::offset_of!(Frame, block));
    let h = fnv1a(h, core::mem::offset_of!(Frame, block_count));
    let h = fnv1a(h, core::mem::offset_of!(Frame, block_kind));
    let h = fnv1a(h, core::mem::offset_of!(Frame, calibration_mask));
    let h = fnv1a(h, core::mem::offset_of!(Frame, last_button));
    let h = fnv1a(h, core::mem::offset_of!(Frame, feedback));
    let h = fnv1a(h, core::mem::offset_of!(Frame, learn_index));
    let h = fnv1a(h, core::mem::offset_of!(Frame, learn_count));
    let h = fnv1a(h, core::mem::offset_of!(Frame, probe_index));
    let h = fnv1a(h, core::mem::offset_of!(Frame, probe_count));
    let h = fnv1a(h, core::mem::offset_of!(Frame, pattern_count));
    let h = fnv1a(h, core::mem::offset_of!(Frame, message_id));
    let h = fnv1a(h, core::mem::offset_of!(Frame, contact));
    let h = fnv1a(h, core::mem::offset_of!(Frame, masking));
    let h = fnv1a(h, core::mem::offset_of!(Frame, alt));
    fnv1a(h, core::mem::offset_of!(Frame, name))
}

#[no_mangle]
pub extern "C" fn haptic_lab_gui_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

const _: () = assert!(core::mem::size_of::<Frame>() == 64);
const _: () = assert!(core::mem::align_of::<Frame>() == 4);
const _: () = assert!(core::mem::offset_of!(Frame, alt) == 27);
const _: () = assert!(core::mem::offset_of!(Frame, name) == 54);

// -- Geometry ----------------------------------------------------------------

const PANEL_W: i32 = 240;
const CENTER_X: i32 = PANEL_W / 2;

/// The disc's radius in pixels, by `BarcodeLayout::pixelIsLit`'s rule: a pixel
/// is glass only if its centre is within 119.5 dot pitches of the centre. Every
/// anchor below was worked out from the chord this gives at that anchor's own
/// row, and `every_string_fits_its_chord` is what re-checks the arithmetic.
pub const DISC_RADIUS: f32 = 119.5;



// -- Button affordances ------------------------------------------------------
// HARDWARE: the four buttons are at the corners of the bezel. There are no
// corners on the glass, so a hint cannot be anchored to one; it is anchored to
// the disc's own edge on its button's side, at a row in its button's half.
//
// Drawn rather than blitted: the bezel indicator bitmaps blit corrupt on
// device (Barcode/README.md), and a solid fill straight into the framebuffer
// is the path that renders. On a three-alternative forced choice the mapping
// *is* the measurement, so this is the most load-bearing drawing in the app.
//
// The labels run inboard from the rim rather than outboard from a mark on the
// diagonal, which is the arrangement Spin uses. Spin's works because its hint
// words are short; two eight-character pattern names on one row are 256 px
// against a 191 px chord, so the inboard anchor put them on top of each other.

/// Which of the four this hint belongs to, in `EventButton::Id` order.
pub const HINT_SW1: usize = 0;
pub const HINT_SW2: usize = 1;
pub const HINT_SW3: usize = 2;
pub const HINT_SW4: usize = 3;

/// The two rows short hints sit on, near the rim in each button's own half.
const HINT_ROW_TOP: i32 = 48;
const HINT_ROW_BOTTOM: i32 = 176;

/// The mark: a solid block at the rim on the label's side, so the side is
/// legible without reading anything.
const MARK_W: i32 = 6;
const MARK_H: i32 = 12;
const MARK_GAP: i32 = 7;

/// Where a short hint's text starts, measured in from the chord at its own row
/// and then rounded in. Both rows use the same pair, so the four hints line up.
const HINT_X_LEFT: i32 = 37;
const HINT_X_RIGHT: i32 = 202;

/// The widest a short hint may be, so the two that share a row cannot meet.
/// Held by `no_hint_can_meet_the_one_across_from_it`.
pub const HINT_MAX_W: i32 = 70;

fn hint_row(button: usize) -> i32 {
    if button == HINT_SW1 || button == HINT_SW2 {
        HINT_ROW_TOP
    } else {
        HINT_ROW_BOTTOM
    }
}

fn hint_is_left(button: usize) -> bool {
    button == HINT_SW1 || button == HINT_SW3
}

/// A block at the rim on `left`'s side, on the row `top` starts.
fn draw_mark(fb: &mut FrameBuf, top: i32, left: bool, anchor: i32, color: Abgr2222) {
    let x = if left { anchor - MARK_GAP - MARK_W } else { anchor + MARK_GAP };
    fill_rect(fb, x, top + 1, MARK_W, MARK_H, color);
}

/// One of the four short hints: the mark, then the word running inboard.
fn draw_hint(fb: &mut FrameBuf, face: &Face, button: usize, text: &str, color: Abgr2222) {
    let top = hint_row(button);
    let left = hint_is_left(button);
    let (x, align) = if left {
        (HINT_X_LEFT, Align::Left)
    } else {
        (HINT_X_RIGHT, Align::Right)
    };
    draw_mark(fb, top, left, x, color);
    draw_text(fb, face, text, x, top, align, color);
}

// -- The response rows -------------------------------------------------------
// The trial screen's four labels get a row each rather than sharing two, so a
// pattern name at its eight-character cap fits without meeting anything. Read
// downwards they are L1, R1, L2, R2, which is the button order, and each sits
// on its
// button's own side with its button's name beside it. Side, vertical order and
// name all say the same thing, because a wearer who is wrong about which button
// they pressed contaminates every confusion number with response error.

const ALT_ROW_TOPS: [i32; 4] = [74, 112, 150, 188];
const ALT_X_LEFT: i32 = 24;
const ALT_X_RIGHT: [i32; 4] = [216, 216, 216, 192];
/// Between a button's name and the alternative it stands for.
const ALT_NAME_GAP: i32 = 26;

/// The button names, in `EventButton::Id` order, as the bezel has them.
pub const BUTTON_NAMES: [&str; 4] = ["L1", "R1", "L2", "R2"];

/// One response row: the mark, the button's name, and the alternative.
fn draw_response_row(fb: &mut FrameBuf, face: &Face, button: usize, text: &str, color: Abgr2222) {
    let top = ALT_ROW_TOPS[button];
    let left = hint_is_left(button);
    let anchor = if left { ALT_X_LEFT } else { ALT_X_RIGHT[button] };

    draw_mark(fb, top, left, anchor, color);
    if left {
        draw_text(fb, SMALL, BUTTON_NAMES[button], anchor, top + 2, Align::Left, DIM);
        draw_text(fb, face, text, anchor + ALT_NAME_GAP, top, Align::Left, color);
    } else {
        draw_text(fb, SMALL, BUTTON_NAMES[button], anchor, top + 2, Align::Right, DIM);
        draw_text(fb, face, text, anchor - ALT_NAME_GAP, top, Align::Right, color);
    }
}

// -- Fonts -------------------------------------------------------------------
// TextKit's pre-rendered Poppins atlases. 22 to 26 px em is what was confirmed
// readable on this glass and 11 to 12 px is detection only, and light
// autohinting snaps vertically only, so SemiBold is preferred for anything
// small or anything that must survive daylight (`TextKit/README.md`).

/// The state word, and any single word the wearer must read at a glance.
static STATE: &Face = &faces::SEMIBOLD_20_ASCII;
/// The three alternatives. SemiBold because a misread here is a contaminated
/// trial rather than a cosmetic problem.
static ALT: &Face = &faces::SEMIBOLD_18_ASCII;
/// Prose, in the box Barcode measured.
static PROSE: &Face = &faces::REGULAR_18_LATIN;
/// What a button does, and the second line of a two-line readout.
static LABEL: &Face = &faces::REGULAR_16_LATIN;
/// Counters, which are read while the wearer is resting rather than answering.
static SMALL: &Face = &faces::REGULAR_14_ASCII;

/// The prose box, measured rather than chosen: `Docs/TEXT.md` records
/// Barcode's prompt layout as x 20, width 200, 24 px line pitch, centred, up
/// to four wrapped lines. The width and the pitch are what was measured; the
/// top varies by screen, because some screens carry a word above it.
pub const PROSE_X: i32 = 20;
pub const PROSE_W: i32 = 200;
const PROSE_LINE_H: i32 = 24;
pub const PROSE_MAX_LINES: usize = 4;

/// Cap top to baseline: the atlases place by baseline, and every layout
/// constant here is the top of the capitals.
fn cap_height(face: &Face) -> i32 {
    (face.px as i32 * 7 + 5) / 10
}

/// Advance width, which is what alignment and wrapping use. Only the layout
/// tests ask for it; the screens go through `draw_text`.
#[cfg(test)]
fn text_width(face: &Face, s: &str) -> i32 {
    face.measure(s).advance
}

fn draw_text(fb: &mut FrameBuf, face: &Face, s: &str, x: i32, top: i32, align: Align, color: Abgr2222) {
    let baseline = top + cap_height(face);
    let mut canvas = fb.canvas();
    face.draw(&mut canvas, s, x, baseline, align, color.0);
}

fn draw_centered(fb: &mut FrameBuf, face: &Face, s: &str, top: i32, color: Abgr2222) {
    draw_text(fb, face, s, CENTER_X, top, Align::Center, color);
}

/// How many lines `text` needs in the prose box, which exceeds
/// `PROSE_MAX_LINES` when it does not fit. A count the caller can see rather
/// than a line that silently vanishes.
pub fn prose_lines(text: &str) -> usize {
    let mut lines = [""; PROSE_MAX_LINES];
    PROSE.wrap(text, PROSE_W, &mut lines)
}

fn draw_prose(fb: &mut FrameBuf, text: &str, top: i32, color: Abgr2222) {
    let mut lines = [""; PROSE_MAX_LINES];
    let n = PROSE.wrap(text, PROSE_W, &mut lines).min(PROSE_MAX_LINES);
    for (i, line) in lines[..n].iter().enumerate() {
        draw_text(
            fb,
            PROSE,
            line,
            PROSE_X + PROSE_W / 2,
            top + i as i32 * PROSE_LINE_H,
            Align::Center,
            color,
        );
    }
}

// -- Number formatting -------------------------------------------------------
// No allocator, so every string is built into a caller-owned buffer.

fn u32_to_str(mut value: u32, buf: &mut [u8; 12]) -> &str {
    let mut i = buf.len();
    loop {
        i -= 1;
        buf[i] = b'0' + (value % 10) as u8;
        value /= 10;
        if value == 0 || i == 0 {
            break;
        }
    }
    core::str::from_utf8(&buf[i..]).unwrap_or("0")
}

/// A fixed-capacity string, for the readouts that join a word to a number.
struct Line {
    buf: [u8; 40],
    len: usize,
}

impl Line {
    fn new() -> Self {
        Line { buf: [0; 40], len: 0 }
    }

    fn push(&mut self, s: &str) -> &mut Self {
        for &b in s.as_bytes() {
            if self.len < self.buf.len() {
                self.buf[self.len] = b;
                self.len += 1;
            }
        }
        self
    }

    fn push_u32(&mut self, value: u32) -> &mut Self {
        let mut digits = [0u8; 12];
        let n = u32_to_str(value, &mut digits).len();
        for i in digits.len() - n..digits.len() {
            if self.len < self.buf.len() {
                self.buf[self.len] = digits[i];
                self.len += 1;
            }
        }
        self
    }

    fn as_str(&self) -> &str {
        core::str::from_utf8(&self.buf[..self.len]).unwrap_or("")
    }
}

// -- Screens -----------------------------------------------------------------

/// Above the short-hint row, so a title and a hint never share pixels. The
/// chord at this row is 149 px, which every title here fits inside.
const TITLE_TOP: i32 = 26;
/// The one word a screen is about, on the widest part of the disc.
const STATE_TOP: i32 = 96;
const SECOND_TOP: i32 = 130;
/// The prose box on a screen with no title above it. Four lines at the 24 px
/// pitch reach row 155, clear of the bottom hint row.
const PROSE_TOP_BARE: i32 = 72;
/// The trial prompt, which is one line at the label face rather than the prose
/// box: the box is 200 px wide at row 72 and there are 178 px of glass here.
const ANSWER_PROMPT_TOP: i32 = 40;
/// The prose box under a title. Also four lines clear of the bottom hints.
const PROSE_TOP_UNDER_TITLE: i32 = 70;
/// The menu carries three readouts above its prose, so two lines is all the
/// room there is before the bottom hint row.
const MENU_PROSE_TOP: i32 = 118;
pub const MENU_PROSE_MAX_LINES: usize = 2;

/// Which condition the run is configured for, drawn where the operator sees it
/// before starting: a no-contact control mislabelled as a wrist run is a
/// ruined session that looks like a real one.
fn condition_line(frame: &Frame) -> Line {
    let mut line = Line::new();
    line.push(if frame.contact == CONTACT_WRIST { "WRIST" } else { "TABLE" });
    line.push(if frame.masking == MASKING_UNMASKED { " UNMASKED" } else { " MASKED" });
    line
}

fn draw_menu(fb: &mut FrameBuf, frame: &Frame) {
    draw_centered(fb, STATE, "HAPTIC LAB", TITLE_TOP, WHITE);
    // Amber, and above everything else the menu says: a no-contact control run
    // mislabelled as a wrist run is a ruined session that looks like a real one.
    draw_centered(fb, LABEL, condition_line(frame).as_str(), 70, AMBER);

    let mut counts = Line::new();
    counts.push_u32(frame.pattern_count as u32).push(" PATTERNS  ");
    counts.push_u32(frame.trials_total as u32).push(" TRIALS");
    draw_centered(fb, SMALL, counts.as_str(), 94, DIM);

    draw_prose(fb, message(frame.message_id), MENU_PROSE_TOP, DIM);

    draw_hint(fb, LABEL, HINT_SW1, "PROBE", WHITE);
    draw_hint(fb, LABEL, HINT_SW2, "RUN", WHITE);
    draw_hint(fb, LABEL, HINT_SW3, "SWEEP", WHITE);
    draw_hint(fb, LABEL, HINT_SW4, "EXIT", DIM);
}

/// Every button lights its own label and nothing else, so a wearer who cannot
/// say which button they pressed is found here rather than in the data.
fn draw_calibration(fb: &mut FrameBuf, frame: &Frame) {
    const NAMES: [&str; 4] = ["L1", "R1", "L2", "R2"];
    draw_prose(fb, message(frame.message_id), PROSE_TOP_BARE, WHITE);
    for (i, name) in NAMES.iter().enumerate() {
        let seen = frame.calibration_mask & (1 << i) != 0;
        let color = if frame.last_button == i as u8 {
            AMBER
        } else if seen {
            WHITE
        } else {
            DIM
        };
        draw_hint(fb, LABEL, i, name, color);
    }
}

/// Naming the pattern and replaying it on demand is what the training stage is
/// for; a trial screen cannot reach this code path.
fn draw_learn(fb: &mut FrameBuf, frame: &Frame) {
    draw_centered(fb, STATE, frame.name_str(), STATE_TOP, WHITE);

    let mut counter = Line::new();
    counter.push_u32(frame.learn_index as u32 + 1).push(" OF ");
    counter.push_u32(frame.learn_count as u32);
    draw_centered(fb, SMALL, counter.as_str(), SECOND_TOP, DIM);

    let mut duration = Line::new();
    duration.push_u32(frame.modelled_duration_ms).push(" MS");
    draw_centered(fb, SMALL, duration.as_str(), SECOND_TOP + 22, DIM);

    draw_hint(fb, LABEL, HINT_SW1, "PREV", WHITE);
    draw_hint(fb, LABEL, HINT_SW2, "PLAY", WHITE);
    draw_hint(fb, LABEL, HINT_SW3, "NEXT", WHITE);
    draw_hint(fb, LABEL, HINT_SW4, "BEGIN", WHITE);
}

/// Nothing to read: a screen with the alternatives already on it invites an
/// answer to a pattern that has not been played.
fn draw_wait(fb: &mut FrameBuf) {
    draw_centered(fb, STATE, "WAIT", STATE_TOP, DIM);
}

/// The window this screen covers is the same length for every pattern in the
/// set, so how long it is up says nothing about which one played.
fn draw_playing(fb: &mut FrameBuf) {
    draw_centered(fb, STATE, "PLAYING", STATE_TOP, WHITE);
}

/// The three, in the order the frame gives them, which does not depend on
/// which was played.
fn draw_answer(fb: &mut FrameBuf, frame: &Frame) {
    // The prompt sits above the rows in the narrow top of the disc, so it is
    // one line at the label face rather than the four-line prose box: the box
    // is 200 px wide at row 72 and there are only 178 px of glass at row 40.
    draw_centered(fb, LABEL, MESSAGES[MSG_WHICH_ONE as usize], ANSWER_PROMPT_TOP, WHITE);
    draw_response_row(fb, ALT, HINT_SW1, frame.alt_str(0), WHITE);
    draw_response_row(fb, ALT, HINT_SW2, frame.alt_str(1), WHITE);
    draw_response_row(fb, ALT, HINT_SW3, frame.alt_str(2), WHITE);
    draw_response_row(fb, LABEL, HINT_SW4, "NOT FELT", DIM);
}

fn draw_feedback(fb: &mut FrameBuf, frame: &Frame) {
    let (word, color) = match frame.feedback {
        FEEDBACK_CORRECT => ("CORRECT", WHITE),
        FEEDBACK_MISS => ("NOT FELT", AMBER),
        FEEDBACK_TIMEOUT => ("NO ANSWER", AMBER),
        _ => ("WRONG", AMBER),
    };
    draw_centered(fb, STATE, word, STATE_TOP, color);

    let mut answer = Line::new();
    answer.push("IT WAS ").push(frame.name_str());
    draw_centered(fb, LABEL, answer.as_str(), SECOND_TOP, DIM);
}

/// Progress belongs here, where the wearer is resting, and not on the screen
/// they are answering on.
fn draw_rest(fb: &mut FrameBuf, frame: &Frame) {
    draw_centered(fb, STATE, "REST", TITLE_TOP, WHITE);

    let mut done = Line::new();
    done.push("BLOCK ").push_u32(frame.block as u32).push(" OF ");
    done.push_u32(frame.block_count as u32);
    draw_centered(fb, LABEL, done.as_str(), PROSE_TOP_UNDER_TITLE, DIM);

    let mut next = Line::new();
    next.push_u32(frame.block_trials as u32);
    next.push(if frame.block_kind == BLOCK_TRAINING { " TRAINING NEXT" } else { " TEST NEXT" });
    draw_centered(fb, LABEL, next.as_str(), PROSE_TOP_UNDER_TITLE + 26, DIM);

    draw_hint(fb, LABEL, HINT_SW2, "GO", WHITE);
    draw_hint(fb, LABEL, HINT_SW4, "END", DIM);
}

fn draw_closed(fb: &mut FrameBuf, frame: &Frame, word: &str, color: Abgr2222) {
    draw_centered(fb, STATE, word, STATE_TOP, color);

    let mut saved = Line::new();
    saved.push_u32(frame.trials_total as u32).push(" TRIALS SAVED");
    draw_centered(fb, LABEL, saved.as_str(), SECOND_TOP, DIM);

    let mut over = Line::new();
    over.push_u32(frame.pattern_count as u32).push(" PATTERNS");
    draw_centered(fb, SMALL, over.as_str(), SECOND_TOP + 24, DIM);

    draw_hint(fb, LABEL, HINT_SW4, "EXIT", WHITE);
}

fn probe_text(frame: &Frame, table: &[&'static str]) -> &'static str {
    // The frame says how many probes the service half built. A disagreement
    // means one list changed without the other, and saying so on the glass is
    // better than drawing the wrong question beside the right label.
    if frame.probe_count as usize != PROBE_QUESTIONS.len() {
        return "Probe list mismatch. Rebuild the app.";
    }
    table.get(frame.probe_index as usize).copied().unwrap_or("")
}

fn draw_probe(fb: &mut FrameBuf, frame: &Frame) {
    draw_centered(fb, ALT, probe_text(frame, &PROBE_LABELS), TITLE_TOP, WHITE);
    draw_prose(fb, probe_text(frame, &PROBE_QUESTIONS), PROSE_TOP_UNDER_TITLE, DIM);
    draw_hint(fb, LABEL, HINT_SW1, "PREV", WHITE);
    draw_hint(fb, LABEL, HINT_SW2, "RUN", WHITE);
    draw_hint(fb, LABEL, HINT_SW3, "NEXT", WHITE);
    draw_hint(fb, LABEL, HINT_SW4, "BACK", DIM);
}

fn draw_probe_sent(fb: &mut FrameBuf, frame: &Frame) {
    draw_centered(fb, STATE, "SENT", TITLE_TOP, WHITE);
    draw_centered(fb, ALT, probe_text(frame, &PROBE_LABELS), STATE_TOP, DIM);

    let mut counter = Line::new();
    counter.push_u32(frame.probe_index as u32 + 1).push(" OF ");
    counter.push_u32(frame.probe_count as u32);
    draw_centered(fb, SMALL, counter.as_str(), SECOND_TOP + 12, DIM);
}

fn draw_sweep(fb: &mut FrameBuf, frame: &Frame) {
    draw_centered(fb, STATE, "SWEEP", TITLE_TOP, WHITE);

    let mut counter = Line::new();
    counter.push("EFFECT ").push_u32(frame.sweep_index as u32);
    counter.push(" OF ").push_u32(frame.sweep_count as u32);
    draw_centered(fb, LABEL, counter.as_str(), 72, WHITE);

    draw_prose(fb, message(frame.message_id), 100, DIM);
    draw_hint(fb, LABEL, HINT_SW4, "STOP", DIM);
}

// -- Entry points ------------------------------------------------------------

pub fn render(buf: &mut [u8], width: u32, height: u32, frame: &Frame) {
    render_with_clip(buf, width, height, frame, Clip::Disc);
}

fn render_with_clip(buf: &mut [u8], width: u32, height: u32, frame: &Frame, clip: Clip) {
    if width == 0 || height == 0 {
        return;
    }
    let needed = (width as usize).saturating_mul(height as usize);
    if buf.len() < needed {
        return;
    }

    let mut fb = FrameBuf { buf: &mut buf[..needed], w: width, h: height, clip };
    fb.buf.fill(BLACK.0);

    match frame.screen {
        SCREEN_CALIBRATION => draw_calibration(&mut fb, frame),
        SCREEN_LEARN => draw_learn(&mut fb, frame),
        SCREEN_WAIT => draw_wait(&mut fb),
        SCREEN_PLAYING => draw_playing(&mut fb),
        SCREEN_ANSWER => draw_answer(&mut fb, frame),
        SCREEN_FEEDBACK => draw_feedback(&mut fb, frame),
        SCREEN_REST => draw_rest(&mut fb, frame),
        SCREEN_DONE => draw_closed(&mut fb, frame, "DONE", WHITE),
        SCREEN_ABORTED => draw_closed(&mut fb, frame, "ENDED", AMBER),
        SCREEN_PROBE => draw_probe(&mut fb, frame),
        SCREEN_PROBE_SENT => draw_probe_sent(&mut fb, frame),
        SCREEN_SWEEP => draw_sweep(&mut fb, frame),
        // Default rather than an arm of its own: an out-of-range screen byte is
        // a bug upstream, and the menu is where it costs the operator least.
        _ => draw_menu(&mut fb, frame),
    }
}

/// # Safety
/// `buf` must point to at least `buf_len` writable bytes and `frame` to a
/// valid `haptic_lab_gui_frame`, both valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn haptic_lab_gui_render(
    buf: *mut u8,
    buf_len: u32,
    width: u16,
    height: u16,
    frame: *const Frame,
) {
    if buf.is_null() || frame.is_null() || buf_len == 0 || width == 0 || height == 0 {
        return;
    }
    let slice = core::slice::from_raw_parts_mut(buf, buf_len as usize);
    render(slice, width as u32, height as u32, &*frame);
}

#[cfg(test)]
mod tests {
    use super::*;

    const W: u32 = 240;
    const H: u32 = 240;

    /// The disc's centre in the square buffer, which is between pixels.
    const RING_CX: f32 = (W as f32 - 1.0) / 2.0;
    const RING_CY: f32 = (H as f32 - 1.0) / 2.0;

    fn draw(frame: &Frame) -> Vec<u8> {
        let mut buf = vec![0u8; (W * H) as usize];
        render(&mut buf, W, H, frame);
        buf
    }

    fn lit_pixels(buf: &[u8]) -> usize {
        buf.iter().filter(|&&b| b != BLACK.0).count()
    }

    /// The draw rule itself, not a looser one: `BarcodeLayout::pixelIsLit` and
    /// `textkit::Canvas::round` both count a pixel as glass only if its centre
    /// is within 119.5 pitches, so the test uses that radius rather than the
    /// datasheet's 120.
    fn inside_bezel(x: u32, y: u32) -> bool {
        let dx = 2 * x as i32 - (W as i32 - 1);
        let dy = 2 * y as i32 - (H as i32 - 1);
        let d = W.min(H) as i32 - 1;
        dx * dx + dy * dy <= d * d
    }

    /// Half the chord of the disc at row `y`: 240 px of width only at the
    /// middle, and about 97 either side of the centre line at the button rows.
    fn half_chord(y: i32) -> f32 {
        let dy = y as f32 - RING_CY;
        let inside = DISC_RADIUS * DISC_RADIUS - dy * dy;
        if inside <= 0.0 {
            0.0
        } else {
            inside.sqrt()
        }
    }

    #[test]
    fn nothing_is_drawn_outside_the_bezel() {
        for (name, frame) in scenes::scenes() {
            let buf = draw(&frame);
            for y in 0..H {
                for x in 0..W {
                    if !inside_bezel(x, y) {
                        assert_eq!(
                            buf[(y * W + x) as usize],
                            BLACK.0,
                            "{name} draws at ({x},{y}), outside the round panel"
                        );
                    }
                }
            }
        }
    }

    /// The clip is a safety net, not the layout. This is the test that would
    /// have caught the hint whose last glyph the glass ate: rendered against a
    /// square canvas every scene has to come out identical, which means nothing
    /// was inside the disc only because the clip removed the end of it.
    #[test]
    fn no_scene_relies_on_the_disc_clip() {
        for (name, frame) in scenes::scenes() {
            let mut clipped = vec![0u8; (W * H) as usize];
            render_with_clip(&mut clipped, W, H, &frame, Clip::Disc);
            let mut square = vec![0u8; (W * H) as usize];
            render_with_clip(&mut square, W, H, &frame, Clip::Rect);
            let differing = clipped.iter().zip(square.iter()).filter(|(a, b)| a != b).count();
            assert_eq!(differing, 0, "{name} has ink the disc clip removed");
        }
    }

    #[test]
    fn every_string_fits_its_chord() {
        for (name, frame) in scenes::scenes() {
            let buf = draw(&frame);
            for y in 0..H {
                let limit = half_chord(y as i32);
                for x in 0..W {
                    if buf[(y * W + x) as usize] == BLACK.0 {
                        continue;
                    }
                    let dx = (x as f32 - RING_CX).abs();
                    assert!(
                        dx <= limit,
                        "{name}: ink at ({x},{y}) is {dx} px from the centre line, past the {limit} px chord"
                    );
                }
            }
        }
    }

    /// The menu has three readouts above its prose, so its own instruction has
    /// to be shorter than the rest.
    #[test]
    fn the_menu_instruction_fits_the_room_the_menu_leaves() {
        let text = MESSAGES[MSG_MENU as usize];
        let mut lines = [""; PROSE_MAX_LINES];
        let n = PROSE.wrap(text, PROSE_W, &mut lines);
        assert!(
            n <= MENU_PROSE_MAX_LINES,
            "{text:?} needs {n} lines and the menu has room for {MENU_PROSE_MAX_LINES}"
        );
    }

    /// Every instruction and every probe question, wrapped into the box and
    /// checked line by line. `wrap` reports the count it needed, so an overflow
    /// is a number here rather than a line that vanished on a wrist.
    #[test]
    fn every_instruction_fits_four_lines_without_splitting_or_losing_a_word() {
        for text in MESSAGES.iter().chain(PROBE_QUESTIONS.iter()) {
            if text.is_empty() {
                continue;
            }
            let mut lines = [""; PROSE_MAX_LINES];
            let n = PROSE.wrap(text, PROSE_W, &mut lines);
            assert!(n <= PROSE_MAX_LINES, "{text:?} needs {n} lines");
            let mut words = Vec::new();
            for line in &lines[..n] {
                assert!(
                    text_width(PROSE, line) <= PROSE_W,
                    "line {line:?} exceeds {PROSE_W} px"
                );
                words.extend(line.split(' '));
            }
            assert_eq!(
                words,
                text.split(' ').collect::<Vec<_>>(),
                "{text:?} lost or split a word"
            );
        }
    }

    /// The whole point of the trial screen: the same triple looks identical
    /// whichever of its three was played, so nothing about the answer can reach
    /// the wearer through the glass.
    #[test]
    fn the_answer_screen_carries_nothing_about_which_was_played() {
        let mut frame = Frame { screen: SCREEN_ANSWER, ..Frame::default() };
        frame.alt[0][..3].copy_from_slice(b"TAP");
        frame.alt[1][..4].copy_from_slice(b"TICK");
        frame.alt[2][..4].copy_from_slice(b"BUZZ");
        let reference = draw(&frame);

        for name in [b"TAP\0\0\0\0\0\0", b"TICK\0\0\0\0\0", b"BUZZ\0\0\0\0\0"] {
            let mut leaked = frame;
            leaked.name.copy_from_slice(name);
            leaked.modelled_duration_ms = 3400;
            leaked.feedback = FEEDBACK_CORRECT;
            leaked.block_kind = 1;
            assert_eq!(draw(&leaked), reference, "the answer screen read a field it must not");
        }
    }

    /// Nor does the screen before it: WAIT and PLAYING are the same picture
    /// whatever pattern is about to be or is being played.
    #[test]
    fn the_playing_screen_carries_nothing_about_which_is_playing() {
        for screen in [SCREEN_WAIT, SCREEN_PLAYING] {
            let bare = Frame { screen, ..Frame::default() };
            let mut loaded = bare;
            loaded.name.copy_from_slice(b"PULSE\0\0\0\0");
            loaded.modelled_duration_ms = 3400;
            loaded.alt[0][..3].copy_from_slice(b"TAP");
            assert_eq!(draw(&loaded), draw(&bare), "screen {screen} read a field it must not");
        }
    }

    #[test]
    fn every_screen_draws_something() {
        for (name, frame) in scenes::scenes() {
            assert!(lit_pixels(&draw(&frame)) > 200, "{name} drew almost nothing");
        }
    }

    #[test]
    fn render_is_a_pure_function_of_the_frame() {
        for (_, frame) in scenes::scenes() {
            assert_eq!(draw(&frame), draw(&frame));
        }
    }

    /// A pattern name at its eight-character cap has to fit its response row,
    /// which is 128 px against a chord that is 240 only at the middle. This is
    /// the arithmetic behind one alternative a row rather than two.
    #[test]
    fn the_longest_pattern_name_fits_its_response_row() {
        let longest = "MMMMMMMM";
        assert_eq!(longest.len(), 8, "kMaxPatternNameLength is eight");
        let width = text_width(ALT, longest);
        for button in [HINT_SW1, HINT_SW2, HINT_SW3] {
            let top = ALT_ROW_TOPS[button];
            let left = hint_is_left(button);
            let (start, end) = if left {
                let x = ALT_X_LEFT + ALT_NAME_GAP;
                (x, x + width)
            } else {
                let x = ALT_X_RIGHT[button] - ALT_NAME_GAP;
                (x - width, x)
            };
            let limit = half_chord(top).min(half_chord(top + cap_height(ALT)));
            let furthest =
                (start as f32 - RING_CX).abs().max((end as f32 - RING_CX).abs());
            assert!(
                furthest <= limit,
                "{longest} on row {button} spans {start}..{end}, {furthest} px from the centre line, past the {limit} px chord"
            );
        }
    }

    /// The two short hints that share a row cannot reach each other, which is
    /// what `HINT_MAX_W` is for.
    #[test]
    fn no_hint_can_meet_the_one_across_from_it() {
        assert!(
            HINT_X_LEFT + HINT_MAX_W < HINT_X_RIGHT - HINT_MAX_W,
            "two {HINT_MAX_W} px hints from {HINT_X_LEFT} and {HINT_X_RIGHT} overlap"
        );
        for text in EVERY_HINT {
            let width = text_width(LABEL, text);
            assert!(width <= HINT_MAX_W, "the hint {text:?} is {width} px, over {HINT_MAX_W}");
        }
    }

    /// Every word this renderer puts in a hint slot, so the cap above is
    /// checked against the list rather than against a sample.
    const EVERY_HINT: [&str; 14] = [
        "PROBE", "RUN", "SWEEP", "EXIT", "PREV", "PLAY", "NEXT", "BEGIN", "GO", "END", "BACK",
        "STOP", "NOT FELT", "MMMMM",
    ];

    /// A frame whose probe list disagrees with this crate's says so rather than
    /// pairing the right label with the wrong question.
    #[test]
    fn a_probe_list_mismatch_is_drawn_rather_than_guessed() {
        let frame = Frame { screen: SCREEN_PROBE, probe_count: 3, ..Frame::default() };
        assert_eq!(probe_text(&frame, &PROBE_QUESTIONS), "Probe list mismatch. Rebuild the app.");
        assert!(lit_pixels(&draw(&frame)) > 200);
    }
}
