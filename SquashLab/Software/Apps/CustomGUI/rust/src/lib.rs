//! SquashLab's renderer, as a pure function of one `Frame`.
//!
//! PANEL. 240x240, round, 8bpp `ABGR2222` — four levels a channel, so every
//! colour here is one of 0/85/170/255 and anything else is quantised on the
//! way to the glass. The corners of the square buffer sit behind the bezel.
//!
//! The screen's job is to tell a player standing on a court, mid-drill, which
//! drill they are on. Everything else is smaller than that.

// `cargo test --features std`: the crate is no_std, and a test binary cannot
// link one whose panics do not unwind.
#![cfg_attr(not(feature = "std"), no_std)]

use inscribed_disc::color::Abgr2222;
use inscribed_disc::surface::Surface;
use textkit::{faces, Align, Face, Style};

#[cfg(feature = "device")]
use panickit as _;

/// The frames worth looking at. Host-only: the watch is handed frames.
#[cfg(feature = "std")]
pub mod scenes;

// -- Palette ----------------------------------------------------------------

const WHITE: Abgr2222 = Abgr2222::WHITE;
const BLACK: Abgr2222 = Abgr2222::BLACK;
const DIM: Abgr2222 = Abgr2222::rgb(170, 170, 170);
const RED: Abgr2222 = Abgr2222::rgb(255, 0, 0);
const AMBER: Abgr2222 = Abgr2222::rgb(255, 170, 0);
const GREEN: Abgr2222 = Abgr2222::rgb(0, 255, 0);
const CYAN: Abgr2222 = Abgr2222::rgb(0, 255, 255);

// -- Screens -----------------------------------------------------------------

/// Choosing which protocol to run. The first screen, because a session is
/// several protocols and the wearer picks each one as they reach it.
pub const SCREEN_PICK: u8 = 0;
/// A drill is running.
pub const SCREEN_DRILL: u8 = 1;
/// Between drills: what the last one counted, and what is next.
pub const SCREEN_BETWEEN: u8 = 2;
/// The protocol is finished.
pub const SCREEN_DONE: u8 = 3;

/// Longest a protocol or drill name may be, including the terminator.
///
/// Names come from a file the wearer edits, so the renderer takes bytes and
/// never a symbol: a drill this build has never heard of still draws.
pub const NAME_LEN: usize = 16;

// -- The C ABI frame ---------------------------------------------------------
// Mirrors squashlab_gui_frame (squashlab_gui.h); the fingerprint checks it.

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Frame {
    /// Seconds in the current drill.
    pub elapsed_s: u32,
    /// Seconds of IMU written this session.
    pub rec_s: u32,
    /// KiB written this session.
    pub rec_kb: u32,
    /// Shots the detector has emitted in this drill. What the wearer checks
    /// their own count against, which is the whole reason it is on the screen.
    pub detected: u32,
    /// Shots the protocol asks for; 0 means the drill is timed, not counted.
    pub target: u32,
    /// The drill's first line, NUL-terminated ASCII — "FOREHAND".
    pub line1: [u8; NAME_LEN],
    /// The drill's second line — "DRIVE". Empty for a one-word drill.
    pub line2: [u8; NAME_LEN],
    /// The protocol highlighted on the pick screen, or the one running.
    pub protocol: [u8; NAME_LEN],
    /// 1-based index of the current drill.
    pub step: u16,
    /// Drills in the protocol.
    pub step_count: u16,
    /// Current bpm; 0 = nothing believable right now.
    pub hr_bpm: u16,
    pub screen: u8,
    /// 1 = samples are being written.
    pub recording: u8,
    /// 1 = this drill is a rest, drawn as a negative control rather than work.
    pub is_rest: u8,
    /// PICK only: how many protocols the file offered.
    pub protocol_count: u8,
    /// PICK only: 1-based index of the highlighted one.
    pub protocol_pick: u8,
}

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// Must walk the same values in the same order as `squashlab_gui_abi::fingerprint()`.
const fn abi_fingerprint() -> u32 {
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<Frame>());
    let h = fnv1a(h, core::mem::align_of::<Frame>());
    let h = fnv1a(h, core::mem::offset_of!(Frame, elapsed_s));
    let h = fnv1a(h, core::mem::offset_of!(Frame, rec_s));
    let h = fnv1a(h, core::mem::offset_of!(Frame, rec_kb));
    let h = fnv1a(h, core::mem::offset_of!(Frame, detected));
    let h = fnv1a(h, core::mem::offset_of!(Frame, target));
    let h = fnv1a(h, core::mem::offset_of!(Frame, line1));
    let h = fnv1a(h, core::mem::offset_of!(Frame, line2));
    let h = fnv1a(h, core::mem::offset_of!(Frame, protocol));
    let h = fnv1a(h, core::mem::offset_of!(Frame, step));
    let h = fnv1a(h, core::mem::offset_of!(Frame, step_count));
    let h = fnv1a(h, core::mem::offset_of!(Frame, hr_bpm));
    let h = fnv1a(h, core::mem::offset_of!(Frame, screen));
    let h = fnv1a(h, core::mem::offset_of!(Frame, recording));
    let h = fnv1a(h, core::mem::offset_of!(Frame, is_rest));
    let h = fnv1a(h, core::mem::offset_of!(Frame, protocol_count));
    fnv1a(h, core::mem::offset_of!(Frame, protocol_pick))
}

#[no_mangle]
pub extern "C" fn squashlab_gui_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

const _: () = assert!(core::mem::size_of::<Frame>() == 80);
const _: () = assert!(core::mem::align_of::<Frame>() == 4);

impl Default for Frame {
    fn default() -> Frame {
        Frame {
            elapsed_s: 0,
            rec_s: 0,
            rec_kb: 0,
            detected: 0,
            target: 0,
            line1: [0; NAME_LEN],
            line2: [0; NAME_LEN],
            protocol: [0; NAME_LEN],
            step: 0,
            step_count: 0,
            hr_bpm: 0,
            screen: SCREEN_PICK,
            recording: 0,
            is_rest: 0,
            protocol_count: 0,
            protocol_pick: 0,
        }
    }
}

// -- Framebuffer and text ----------------------------------------------------

type FrameBuf<'a> = Surface<'a, Abgr2222>;

const CENTER_X: i32 = 120;

static SMALL: &Face = &faces::REGULAR_12_ASCII;
static LABEL_F: &Face = &faces::REGULAR_14_ASCII;
static MED: &Face = &faces::SEMIBOLD_20_ASCII;
/// Full ASCII, not a subset: drill names come from a file the wearer edits, so
/// a name this build has never seen still has to draw rather than becoming a
/// row of empty boxes.
static BIG: &Face = &faces::SEMIBOLD_28_ASCII;

fn cap_height(face: &Face) -> i32 {
    (face.px as i32 * 7 + 5) / 10
}

fn draw_text(fb: &mut FrameBuf, face: &Face, s: &str, x: i32, top: i32, align: Align, color: Abgr2222) {
    face.draw(fb, s, x, top + cap_height(face), Style::new(color, BLACK, align)).ok();
}

fn draw_centered(fb: &mut FrameBuf, face: &Face, s: &str, y: i32, color: Abgr2222) {
    draw_text(fb, face, s, CENTER_X, y, Align::Center, color);
}

fn text_width(face: &Face, s: &str) -> i32 {
    face.measure(s).advance.max(0)
}

/// Half the chord of the disc at height `y`, which is how much room a line has.
///
/// The panel is round, so a line near the top or bottom has far less width than
/// one across the middle: at y=16 the chord is 120 px where at y=120 it is 240.
/// A name long enough to be clipped there is not a rendering detail, because
/// the names come from a file and this build cannot know them.
fn half_chord(y: i32) -> i32 {
    let dy = (y - 120).abs() as f32;
    let r = 120.0f32;
    if dy >= r {
        return 0;
    }
    #[cfg(feature = "std")]
    let h = (r * r - dy * dy).sqrt();
    #[cfg(not(feature = "std"))]
    let h = {
        // No std sqrt in no_std; Newton on the integer square is plenty here.
        let v = (r * r - dy * dy) as u32;
        let mut x = v.max(1);
        let mut last = 0u32;
        while x != last {
            last = x;
            x = (x + v / x) / 2;
        }
        x as f32
    };
    h as i32
}

/// Draw a name at the largest face that fits the disc at that height.
///
/// Tried largest first and never clipped: a drill name the wearer cannot read
/// is the one failure this screen must not have, and shrinking is always better
/// than losing the ends of the word.
fn draw_fitted(fb: &mut FrameBuf, s: &str, y: i32, color: Abgr2222) -> i32 {
    if s.is_empty() {
        return 0;
    }
    // Both ends of the line sit at the tighter of its top and bottom.
    let room = half_chord(y).min(half_chord(y + 28)) * 2 - 8;
    for face in [BIG, MED, LABEL_F] {
        if text_width(face, s) <= room {
            draw_centered(fb, face, s, y, color);
            return face.px as i32;
        }
    }
    draw_centered(fb, SMALL, s, y, color);
    SMALL.px as i32
}

/// The NUL-terminated ASCII in a name field, as a `&str`.
///
/// Anything that is not printable ASCII ends the name. A file the wearer edits
/// can carry anything, and a renderer is the wrong place to discover that.
fn name(bytes: &[u8; NAME_LEN]) -> &str {
    let mut n = 0;
    while n < NAME_LEN {
        let c = bytes[n];
        if !(0x20..0x7F).contains(&c) {
            break;
        }
        n += 1;
    }
    core::str::from_utf8(&bytes[..n]).unwrap_or("")
}

fn fmt_u32(v: u32, buf: &mut [u8]) -> &str {
    let mut i = buf.len();
    let mut n = v;
    loop {
        i -= 1;
        buf[i] = b'0' + (n % 10) as u8;
        n /= 10;
        if n == 0 || i == 0 {
            break;
        }
    }
    core::str::from_utf8(&buf[i..]).unwrap_or("0")
}

fn format_duration(total: u32, buf: &mut [u8; 12]) -> &str {
    let m = total / 60;
    let s = total % 60;
    let mut i = 0;
    let mut mb = [0u8; 10];
    for &c in fmt_u32(m, &mut mb).as_bytes() {
        if i < 12 {
            buf[i] = c;
            i += 1;
        }
    }
    for c in [b':', b'0' + (s / 10) as u8, b'0' + (s % 10) as u8] {
        if i < 12 {
            buf[i] = c;
            i += 1;
        }
    }
    core::str::from_utf8(&buf[..i]).unwrap_or("0:00")
}

fn join(out: &mut [u8], parts: &[&str]) -> usize {
    let mut i = 0;
    for p in parts {
        for &b in p.as_bytes() {
            if i < out.len() {
                out[i] = b;
                i += 1;
            }
        }
    }
    i
}

// -- Screens -----------------------------------------------------------------

fn draw_step_of(fb: &mut FrameBuf, frame: &Frame, y: i32) {
    if frame.step_count == 0 {
        return;
    }
    let mut a = [0u8; 8];
    let mut b = [0u8; 8];
    let mut line = [0u8; 24];
    let n = join(
        &mut line,
        &[
            fmt_u32(frame.step as u32, &mut a),
            " OF ",
            fmt_u32(frame.step_count as u32, &mut b),
        ],
    );
    draw_centered(fb, SMALL, core::str::from_utf8(&line[..n]).unwrap_or(""), y, DIM);
}

fn draw_pick(fb: &mut FrameBuf, frame: &Frame) {
    draw_centered(fb, SMALL, "PROTOCOL", 26, DIM);
    draw_fitted(fb, name(&frame.protocol), 60, WHITE);

    if frame.protocol_count > 0 {
        let mut a = [0u8; 8];
        let mut b = [0u8; 8];
        let mut line = [0u8; 24];
        let n = join(
            &mut line,
            &[
                fmt_u32(frame.protocol_pick as u32, &mut a),
                " OF ",
                fmt_u32(frame.protocol_count as u32, &mut b),
            ],
        );
        draw_centered(fb, SMALL, core::str::from_utf8(&line[..n]).unwrap_or(""), 110, DIM);
    }

    if frame.step_count > 0 {
        let mut c = [0u8; 8];
        let mut line = [0u8; 24];
        let n = join(&mut line, &[fmt_u32(frame.step_count as u32, &mut c), " DRILLS"]);
        draw_centered(fb, LABEL_F, core::str::from_utf8(&line[..n]).unwrap_or(""), 140, DIM);
    }

    draw_centered(fb, LABEL_F, "L2 NEXT   R1 START", 190, WHITE);
}

fn draw_drill(fb: &mut FrameBuf, frame: &Frame) {
    // The one thing a player standing on a court needs, and it gets the top of
    // the screen and the largest face this app carries.
    let colour = if frame.is_rest == 1 { AMBER } else { GREEN };
    draw_fitted(fb, name(&frame.line1), 28, colour);
    draw_fitted(fb, name(&frame.line2), 62, colour);

    draw_step_of(fb, frame, 96);

    // The count the wearer is here to check theirs against. Drawn as
    // detected-of-target when the protocol asked for a number, so the two are
    // read together and neither can be mistaken for the other.
    let mut d = [0u8; 10];
    if frame.target > 0 {
        let mut t = [0u8; 10];
        let mut line = [0u8; 24];
        let n = join(
            &mut line,
            &[fmt_u32(frame.detected, &mut d), " / ", fmt_u32(frame.target, &mut t)],
        );
        let reached = frame.detected >= frame.target;
        draw_centered(
            fb,
            BIG,
            core::str::from_utf8(&line[..n]).unwrap_or(""),
            114,
            if reached { CYAN } else { WHITE },
        );
    } else {
        draw_centered(fb, BIG, fmt_u32(frame.detected, &mut d), 114, WHITE);
    }
    draw_centered(fb, SMALL, "SHOTS SEEN", 152, DIM);

    let mut e = [0u8; 12];
    draw_centered(fb, LABEL_F, format_duration(frame.elapsed_s, &mut e), 170, DIM);

    if frame.recording == 1 {
        fb.fill_rect(CENTER_X - 30, 191, 6, 6, RED);
        draw_text(fb, SMALL, "REC", CENTER_X - 20, 188, Align::Left, RED);
    } else {
        draw_centered(fb, SMALL, "NOT RECORDING", 188, AMBER);
    }
}

fn draw_between(fb: &mut FrameBuf, frame: &Frame) {
    draw_centered(fb, SMALL, "COUNTED", 26, DIM);
    let mut d = [0u8; 10];
    draw_centered(fb, BIG, fmt_u32(frame.detected, &mut d), 48, CYAN);

    draw_centered(fb, SMALL, "NEXT", 96, DIM);
    draw_fitted(fb, name(&frame.line1), 112, GREEN);
    draw_fitted(fb, name(&frame.line2), 140, GREEN);

    draw_step_of(fb, frame, 168);
    draw_centered(fb, LABEL_F, "R1  GO", 190, WHITE);
}

fn draw_done(fb: &mut FrameBuf, frame: &Frame) {
    draw_centered(fb, BIG, "DONE", 30, WHITE);
    draw_centered(fb, LABEL_F, name(&frame.protocol), 72, DIM);

    let mut rb = [0u8; 12];
    let mut kb = [0u8; 10];
    let mut line = [0u8; 32];
    let n = join(
        &mut line,
        &[
            format_duration(frame.rec_s, &mut rb),
            "   ",
            fmt_u32(frame.rec_kb / 1024, &mut kb),
            " MB",
        ],
    );
    draw_centered(fb, LABEL_F, core::str::from_utf8(&line[..n]).unwrap_or(""), 104, DIM);

    let mut d = [0u8; 10];
    let mut line = [0u8; 24];
    let n = join(&mut line, &[fmt_u32(frame.detected, &mut d), " SHOTS SEEN"]);
    draw_centered(fb, LABEL_F, core::str::from_utf8(&line[..n]).unwrap_or(""), 128, DIM);

    draw_centered(fb, LABEL_F, "R1  SAVE", 176, WHITE);
}

pub fn render(buf: &mut [u8], width: u32, height: u32, frame: &Frame) {
    let Some(mut fb) = Surface::<Abgr2222>::round(buf, width, height) else {
        return;
    };
    fb.clear(BLACK);

    match frame.screen {
        SCREEN_DRILL => draw_drill(&mut fb, frame),
        SCREEN_BETWEEN => draw_between(&mut fb, frame),
        SCREEN_DONE => draw_done(&mut fb, frame),
        // Default rather than an arm of its own: an out-of-range screen byte is
        // a bug upstream, and the picker loses the wearer the least.
        _ => draw_pick(&mut fb, frame),
    }
}

/// # Safety
/// `buf` must point to at least `buf_len` writable bytes and `frame` to a valid
/// `squashlab_gui_frame`, both valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn squashlab_gui_render(
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

    fn named(s: &str) -> [u8; NAME_LEN] {
        let mut out = [0u8; NAME_LEN];
        for (i, b) in s.bytes().take(NAME_LEN - 1).enumerate() {
            out[i] = b;
        }
        out
    }

    fn draw(frame: &Frame) -> Vec<u8> {
        let mut buf = vec![0u8; (W * H) as usize];
        render(&mut buf, W, H, frame);
        buf
    }

    fn lit(buf: &[u8]) -> usize {
        buf.iter().filter(|&&b| b != BLACK.0).count()
    }

    #[test]
    fn every_screen_draws_something() {
        for screen in [SCREEN_PICK, SCREEN_DRILL, SCREEN_BETWEEN, SCREEN_DONE] {
            let f = Frame {
                screen,
                line1: named("FOREHAND"),
                line2: named("DRIVE"),
                protocol: named("DRIVE DROP"),
                step: 1,
                step_count: 4,
                ..Frame::default()
            };
            assert!(lit(&draw(&f)) > 0, "screen {screen} drew nothing");
        }
    }

    #[test]
    fn a_name_stops_at_its_terminator() {
        assert_eq!(name(&named("FOREHAND")), "FOREHAND");
        assert_eq!(name(&named("")), "");
        // Full field with no room for a terminator still reads as a name.
        let full = [b'A'; NAME_LEN];
        assert_eq!(name(&full).len(), NAME_LEN);
    }

    #[test]
    fn a_name_the_file_mangled_does_not_reach_the_glass_as_control_codes() {
        let mut bytes = named("DRIVE");
        bytes[2] = 0x07;
        assert_eq!(name(&bytes), "DR");
        let mut high = [0u8; NAME_LEN];
        high[0] = 0xFF;
        assert_eq!(name(&high), "");
    }

    #[test]
    fn a_drill_this_build_has_never_heard_of_still_draws() {
        // The whole reason the big face is full ASCII: the protocol is a file.
        let f = Frame {
            screen: SCREEN_DRILL,
            line1: named("KZQJX"),
            line2: named("VWXYZ"),
            step: 1,
            step_count: 1,
            ..Frame::default()
        };
        assert!(lit(&draw(&f)) > 0);
    }

    #[test]
    fn a_counted_drill_and_a_timed_one_draw_different_things() {
        let base = Frame {
            screen: SCREEN_DRILL,
            line1: named("FOREHAND"),
            detected: 12,
            step: 1,
            step_count: 4,
            ..Frame::default()
        };
        let counted = Frame { target: 30, ..base };
        assert_ne!(draw(&counted), draw(&base), "a target must be visible when set");
    }

    #[test]
    fn duration_is_minutes_and_seconds() {
        let mut b = [0u8; 12];
        assert_eq!(format_duration(0, &mut b), "0:00");
        assert_eq!(format_duration(65, &mut b), "1:05");
        assert_eq!(format_duration(3599, &mut b), "59:59");
    }

    #[test]
    fn nothing_is_drawn_outside_the_bezel() {
        let f = Frame {
            screen: SCREEN_DRILL,
            line1: named("FOREHAND"),
            line2: named("DRIVE"),
            detected: 28,
            target: 30,
            step: 1,
            step_count: 4,
            ..Frame::default()
        };
        let buf = draw(&f);
        let (cx, cy, r) = (119.5f32, 119.5f32, 120.0f32);
        for y in 0..H {
            for x in 0..W {
                let dx = x as f32 - cx;
                let dy = y as f32 - cy;
                if dx * dx + dy * dy > r * r {
                    assert_eq!(buf[(y * W + x) as usize], BLACK.0, "lit outside the disc at {x},{y}");
                }
            }
        }
    }
}
