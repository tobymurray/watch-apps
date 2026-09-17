//! The Squash recorder's renderer, as a pure function of one `Frame`.
//!
//! PANEL. 240x240, round, 8bpp `ABGR2222` — four levels a channel, so every
//! colour here is one of 0/85/170/255 and anything else is quantised on the
//! way to the glass. The corners of the square buffer sit behind the bezel.
//!
//! This app is an instrument, not an activity tracker: the screen's job is to
//! let the wearer answer "is it recording, what am I calling this, and is the
//! sensor seeing anything" without taking the watch off.

// `cargo test --features std`: the crate is no_std, and a test binary cannot
// link one whose panics do not unwind.
#![cfg_attr(not(feature = "std"), no_std)]

use inscribed_disc::color::Abgr2222;
use inscribed_disc::surface::Surface;
use textkit::{faces, Align, Face, Style};

// PanicKit owns this app's `#[panic_handler]`. A lang item is only linked if
// something references the crate, and nothing here calls it by name, so this
// import is what pulls it in.
#[cfg(feature = "device")]
use panickit as _;

/// The frames worth looking at. Host-only: the watch is handed frames.
#[cfg(feature = "std")]
pub mod scenes;

// -- Palette ----------------------------------------------------------------

const WHITE: Abgr2222 = Abgr2222::WHITE;
const BLACK: Abgr2222 = Abgr2222::BLACK;
/// The dimmest usable grey: 85 is the only other non-black level and it washes
/// out in daylight on this reflective panel.
const DIM: Abgr2222 = Abgr2222::rgb(170, 170, 170);
/// Heart rate, and only heart rate.
const RED: Abgr2222 = Abgr2222::rgb(255, 0, 0);
/// Held, or a cap that has stopped the recording.
const AMBER: Abgr2222 = Abgr2222::rgb(255, 170, 0);
const GREEN: Abgr2222 = Abgr2222::rgb(0, 255, 0);
const BLUE: Abgr2222 = Abgr2222::rgb(0, 170, 255);
const CYAN: Abgr2222 = Abgr2222::rgb(0, 255, 255);
const YELLOW: Abgr2222 = Abgr2222::rgb(255, 255, 0);

// -- Screens -----------------------------------------------------------------

pub const SCREEN_READY: u8 = 0;
pub const SCREEN_PROFILE: u8 = 1;
pub const SCREEN_LABEL: u8 = 2;
pub const SCREEN_PAUSED: u8 = 3;
pub const SCREEN_SAVED: u8 = 4;
pub const SCREEN_DISCARDED: u8 = 5;

// -- Labels ------------------------------------------------------------------
// These values ARE the `kind` column of `imu_<stamp>_events.csv`. Kind 0 stayed
// MANUAL so every recording made before on-watch labelling still reads back as
// unlabelled markers rather than as rallies.

pub const LABEL_NONE: u8 = 0;
pub const LABEL_RALLY: u8 = 1;
pub const LABEL_REST: u8 = 2;
pub const LABEL_OFF_COURT: u8 = 3;
pub const LABEL_WARMUP: u8 = 4;
pub const LABEL_DRILL: u8 = 5;
pub const LABEL_IDLE: u8 = 6;
/// A whole game. What this app marks: a press per rally is 15-25 a game.
pub const LABEL_GAME: u8 = 7;
pub const LABEL_COUNT: u8 = 8;

/// The name written on the screen, and the name `Label::parse` reads back.
pub fn label_name(label: u8) -> &'static str {
    match label {
        LABEL_RALLY => "RALLY",
        LABEL_REST => "REST",
        LABEL_OFF_COURT => "OFF COURT",
        LABEL_WARMUP => "WARMUP",
        LABEL_DRILL => "DRILL",
        LABEL_IDLE => "IDLE",
        LABEL_GAME => "GAME",
        _ => "NO LABEL",
    }
}

fn label_color(label: u8) -> Abgr2222 {
    match label {
        LABEL_RALLY => CYAN,
        LABEL_REST => AMBER,
        LABEL_OFF_COURT => BLUE,
        LABEL_WARMUP => CYAN,
        LABEL_DRILL => YELLOW,
        LABEL_IDLE => DIM,
        LABEL_GAME => GREEN,
        _ => DIM,
    }
}

// -- Recorder state ----------------------------------------------------------
// Mirrors ImuCsvRecorder::Stop, value for value.

pub const REC_NONE: u8 = 0;
pub const REC_REQUESTED: u8 = 1;
pub const REC_SIZE_LIMIT: u8 = 2;
pub const REC_DURATION_LIMIT: u8 = 3;
pub const REC_SINK_ERROR: u8 = 4;

/// Why the recorder stopped, in the words the wearer needs rather than the
/// enum's. A cap is not a failure and a sink error is, so they do not share a
/// colour.
fn stop_text(stop: u8) -> (&'static str, Abgr2222) {
    match stop {
        REC_SIZE_LIMIT => ("SIZE CAP REACHED", AMBER),
        REC_DURATION_LIMIT => ("TIME CAP REACHED", AMBER),
        REC_SINK_ERROR => ("WRITE FAILED", RED),
        _ => ("", DIM),
    }
}

pub const HR_NONE: u8 = 0;
pub const HR_OPTICAL: u8 = 1;
pub const HR_EXTERNAL: u8 = 2;

// -- The C ABI frame ---------------------------------------------------------
// Mirrors squash_gui_frame (squash_gui.h); the fingerprint below is what checks
// it.

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Frame {
    /// Active session seconds; the clock the wearer started.
    pub elapsed_s: u32,
    /// Seconds of IMU actually written. Diverges from `elapsed_s` the moment a
    /// cap trips, which is the divergence this screen exists to show.
    pub rec_s: u32,
    /// The duration cap in force, from `input.json`.
    pub rec_cap_s: u32,
    /// KiB written so far.
    pub rec_kb: u32,
    /// The size cap in force, KiB.
    pub rec_cap_kb: u32,
    /// Mean gyroscope vector magnitude over the last epoch, raw LSB.
    pub gyro_mag: u32,
    /// Variance of accelerometer magnitude over the last epoch, LSB^2 / 1000.
    pub accel_var_k: u32,
    /// Seconds the wearer said they were in a rally. Pressed, not inferred.
    pub rally_s: u32,
    /// Seconds inside a game, which is what this app's one button marks.
    pub game_s: u32,
    /// Seconds resting on court, which in threes includes sitting a rally out.
    pub rest_s: u32,
    /// Seconds off court entirely.
    pub off_court_s: u32,
    /// Markers written to the sidecar, label changes included.
    pub markers: u16,
    /// Current bpm; 0 = nothing believable right now.
    pub hr_bpm: u16,
    /// Seconds held in the current label.
    pub label_s: u16,
    /// Percent of last epoch's samples with any accelerometer axis railed.
    pub sat_accel_pct: u8,
    /// Percent of last epoch's samples with any gyroscope axis railed.
    pub sat_gyro_pct: u8,
    pub screen: u8,
    /// One of the `LABEL_*` values; what the sidecar is being told right now.
    pub label: u8,
    /// LABEL screen only: which entry the wearer is sitting on.
    pub label_pick: u8,
    /// The kernel's 0..3 confidence in `hr_bpm`.
    pub hr_trust: u8,
    pub hr_source: u8,
    /// 1 = samples are being written this second.
    pub recording: u8,
    /// One of the `REC_*` values.
    pub rec_stop: u8,
    /// 1 = `recordImu` was true in `input.json`, so starting will record.
    pub armed: u8,
    /// SAVED only: 1 the files are on disk, 0 they are not.
    pub saved_ok: u8,
}

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// Must walk the same values in the same order as `squash_gui_abi_fingerprint`
/// is walked in `squash_gui.h`.
const fn abi_fingerprint() -> u32 {
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<Frame>());
    let h = fnv1a(h, core::mem::align_of::<Frame>());
    let h = fnv1a(h, core::mem::offset_of!(Frame, elapsed_s));
    let h = fnv1a(h, core::mem::offset_of!(Frame, rec_s));
    let h = fnv1a(h, core::mem::offset_of!(Frame, rec_cap_s));
    let h = fnv1a(h, core::mem::offset_of!(Frame, rec_kb));
    let h = fnv1a(h, core::mem::offset_of!(Frame, rec_cap_kb));
    let h = fnv1a(h, core::mem::offset_of!(Frame, gyro_mag));
    let h = fnv1a(h, core::mem::offset_of!(Frame, accel_var_k));
    let h = fnv1a(h, core::mem::offset_of!(Frame, rally_s));
    let h = fnv1a(h, core::mem::offset_of!(Frame, game_s));
    let h = fnv1a(h, core::mem::offset_of!(Frame, rest_s));
    let h = fnv1a(h, core::mem::offset_of!(Frame, off_court_s));
    let h = fnv1a(h, core::mem::offset_of!(Frame, markers));
    let h = fnv1a(h, core::mem::offset_of!(Frame, hr_bpm));
    let h = fnv1a(h, core::mem::offset_of!(Frame, label_s));
    let h = fnv1a(h, core::mem::offset_of!(Frame, sat_accel_pct));
    let h = fnv1a(h, core::mem::offset_of!(Frame, sat_gyro_pct));
    let h = fnv1a(h, core::mem::offset_of!(Frame, screen));
    let h = fnv1a(h, core::mem::offset_of!(Frame, label));
    let h = fnv1a(h, core::mem::offset_of!(Frame, label_pick));
    let h = fnv1a(h, core::mem::offset_of!(Frame, hr_trust));
    let h = fnv1a(h, core::mem::offset_of!(Frame, hr_source));
    let h = fnv1a(h, core::mem::offset_of!(Frame, recording));
    let h = fnv1a(h, core::mem::offset_of!(Frame, rec_stop));
    let h = fnv1a(h, core::mem::offset_of!(Frame, armed));
    fnv1a(h, core::mem::offset_of!(Frame, saved_ok))
}

#[no_mangle]
pub extern "C" fn squash_gui_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

const _: () = assert!(core::mem::size_of::<Frame>() == 64);
const _: () = assert!(core::mem::align_of::<Frame>() == 4);
const _: () = assert!(core::mem::offset_of!(Frame, elapsed_s) == 0);
const _: () = assert!(core::mem::offset_of!(Frame, rec_s) == 4);
const _: () = assert!(core::mem::offset_of!(Frame, rec_cap_s) == 8);
const _: () = assert!(core::mem::offset_of!(Frame, rec_kb) == 12);
const _: () = assert!(core::mem::offset_of!(Frame, rec_cap_kb) == 16);
const _: () = assert!(core::mem::offset_of!(Frame, gyro_mag) == 20);
const _: () = assert!(core::mem::offset_of!(Frame, accel_var_k) == 24);
const _: () = assert!(core::mem::offset_of!(Frame, rally_s) == 28);
const _: () = assert!(core::mem::offset_of!(Frame, game_s) == 32);
const _: () = assert!(core::mem::offset_of!(Frame, rest_s) == 36);
const _: () = assert!(core::mem::offset_of!(Frame, off_court_s) == 40);
const _: () = assert!(core::mem::offset_of!(Frame, markers) == 44);
const _: () = assert!(core::mem::offset_of!(Frame, hr_bpm) == 46);
const _: () = assert!(core::mem::offset_of!(Frame, label_s) == 48);
const _: () = assert!(core::mem::offset_of!(Frame, sat_accel_pct) == 50);
const _: () = assert!(core::mem::offset_of!(Frame, sat_gyro_pct) == 51);
const _: () = assert!(core::mem::offset_of!(Frame, screen) == 52);
const _: () = assert!(core::mem::offset_of!(Frame, label) == 53);
const _: () = assert!(core::mem::offset_of!(Frame, label_pick) == 54);
const _: () = assert!(core::mem::offset_of!(Frame, hr_trust) == 55);
const _: () = assert!(core::mem::offset_of!(Frame, hr_source) == 56);
const _: () = assert!(core::mem::offset_of!(Frame, recording) == 57);
const _: () = assert!(core::mem::offset_of!(Frame, rec_stop) == 58);
const _: () = assert!(core::mem::offset_of!(Frame, armed) == 59);
const _: () = assert!(core::mem::offset_of!(Frame, saved_ok) == 60);

// -- Framebuffer -------------------------------------------------------------

type FrameBuf<'a> = Surface<'a, Abgr2222>;

// -- Geometry ----------------------------------------------------------------

const CENTER_X: i32 = 120;

static SMALL: &Face = &faces::REGULAR_12_ASCII;
static LABEL_F: &Face = &faces::REGULAR_14_ASCII;
static HEADING: &Face = &faces::SEMIBOLD_18_ASCII;
static BIG: &Face = &faces::SEMIBOLD_20_ASCII;
/// Digits and a colon, nothing else; the recorded clock is all it draws.
static CLOCK: &Face = &faces::SEMIBOLD_27_CLOCK;
/// Cut for exactly the state names in `label_name`; it can draw nothing else.
static STATE: &Face = &faces::SEMIBOLD_28_STATES;

/// Poppins' capital height is 0.7 em, so a screen that names a top gets its
/// capitals starting there.
fn cap_height(face: &Face) -> i32 {
    (face.px as i32 * 7 + 5) / 10
}

fn draw_text(fb: &mut FrameBuf, face: &Face, s: &str, x: i32, top: i32, align: Align, color: Abgr2222) {
    face.draw(fb, s, x, top + cap_height(face), Style::new(color, BLACK, align)).ok();
}

fn draw_centered(fb: &mut FrameBuf, face: &Face, s: &str, y: i32, color: Abgr2222) {
    draw_text(fb, face, s, CENTER_X, y, Align::Center, color);
}

// -- Number formatting -------------------------------------------------------
// No allocator, so every string is built into a caller-owned buffer.

/// Decimal digits of `v`, right-aligned in `buf`, returned as a slice of it.
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

/// `M:SS` under an hour, `H:MM:SS` at or over it — the shortest unambiguous
/// form, so the digits stay as large as they can be.
fn format_duration(total: u32, buf: &mut [u8; 12]) -> &str {
    let h = total / 3600;
    let m = (total % 3600) / 60;
    let s = total % 60;
    let mut i = 0;
    let mut put = |b: u8, i: &mut usize| {
        if *i < 12 {
            buf[*i] = b;
            *i += 1;
        }
    };
    if h > 0 {
        let mut hb = [0u8; 10];
        for &c in fmt_u32(h, &mut hb).as_bytes() {
            put(c, &mut i);
        }
        put(b':', &mut i);
        put(b'0' + (m / 10) as u8, &mut i);
        put(b'0' + (m % 10) as u8, &mut i);
    } else {
        let mut mb = [0u8; 10];
        for &c in fmt_u32(m, &mut mb).as_bytes() {
            put(c, &mut i);
        }
    }
    put(b':', &mut i);
    put(b'0' + (s / 10) as u8, &mut i);
    put(b'0' + (s % 10) as u8, &mut i);
    core::str::from_utf8(&buf[..i]).unwrap_or("0:00")
}

/// KiB as megabytes with one decimal, which is the resolution the caps are set
/// at; `1536` becomes `1.5`.
fn format_mb(kb: u32, buf: &mut [u8; 12]) -> &str {
    let tenths = (kb * 10 + 512) / 1024;
    let whole = tenths / 10;
    let frac = tenths % 10;
    let mut i = 0;
    let mut wb = [0u8; 10];
    for &c in fmt_u32(whole, &mut wb).as_bytes() {
        if i < 12 {
            buf[i] = c;
            i += 1;
        }
    }
    if i < 12 {
        buf[i] = b'.';
        i += 1;
    }
    if i < 12 {
        buf[i] = b'0' + frac as u8;
        i += 1;
    }
    core::str::from_utf8(&buf[..i]).unwrap_or("0.0")
}

// -- The magnitude ladder ----------------------------------------------------

/// Segment thresholds for the live bars, in the feature's own raw LSB.
///
/// A table rather than a logarithm: the panel has twelve segments to give away,
/// the values span four orders of magnitude, and a ladder someone can read off
/// the source is worth more here than a curve. Falsified by a recording whose
/// interesting range sits outside it.
const GYRO_LADDER: [u32; 12] =
    [100, 200, 400, 700, 1_200, 2_000, 3_200, 5_000, 8_000, 12_000, 18_000, 26_000];

/// Accelerometer magnitude variance, in thousands of LSB^2.
const ACCEL_LADDER: [u32; 12] =
    [1, 5, 20, 70, 200, 600, 1_500, 4_000, 10_000, 30_000, 80_000, 200_000];

fn segments_lit(v: u32, ladder: &[u32; 12]) -> usize {
    ladder.iter().filter(|&&t| v >= t).count()
}

/// A twelve-segment bar, lit left to right, with unlit segments left as an
/// outline so the scale stays readable when the signal is small.
fn draw_bar(fb: &mut FrameBuf, x: i32, y: i32, w: i32, h: i32, lit: usize, color: Abgr2222) {
    let seg_w = w / 12;
    for i in 0..12 {
        let sx = x + i as i32 * seg_w;
        if i < lit {
            fb.fill_rect(sx, y, seg_w - 1, h, color);
        } else {
            fb.fill_rect(sx, y + h - 1, seg_w - 1, 1, Abgr2222::rgb(85, 85, 85));
        }
    }
}

// -- Screens -----------------------------------------------------------------

fn draw_ready(fb: &mut FrameBuf, frame: &Frame) {
    draw_centered(fb, HEADING, "SQUASH", 30, WHITE);

    if frame.armed == 1 {
        draw_centered(fb, BIG, "ARMED", 58, GREEN);
        let mut mb = [0u8; 12];
        let mut mins = [0u8; 10];
        let cap_mb = format_mb(frame.rec_cap_kb, &mut mb);
        let cap_min = fmt_u32(frame.rec_cap_s / 60, &mut mins);
        // Both caps, because either can be the one that stops a session and the
        // wearer cannot tell which from the countdown alone.
        let mut line = [0u8; 32];
        let n = join(&mut line, &[cap_min, " MIN  ", cap_mb, " MB"]);
        draw_centered(fb, SMALL, core::str::from_utf8(&line[..n]).unwrap_or(""), 92, DIM);
    } else {
        draw_centered(fb, BIG, "NOT ARMED", 58, AMBER);
        draw_centered(fb, SMALL, "recordImu is false", 92, DIM);
    }

    draw_hr_row(fb, frame, 140);
    draw_centered(fb, LABEL_F, "R1  START", 186, WHITE);
}

fn draw_profile(fb: &mut FrameBuf, frame: &Frame) {
    let paused = frame.screen == SCREEN_PAUSED;

    // Row 1: the recording's own state, which is not the session's. A session
    // runs on after a cap stops the samples, and that is the case this row is
    // here for.
    if paused {
        draw_centered(fb, SMALL, "PAUSED", 14, AMBER);
    } else if frame.recording == 1 {
        fb.fill_rect(CENTER_X - 34, 17, 7, 7, RED);
        draw_text(fb, SMALL, "REC", CENTER_X - 22, 14, Align::Left, RED);
    } else {
        let (text, color) = stop_text(frame.rec_stop);
        if text.is_empty() {
            draw_centered(fb, SMALL, "NOT RECORDING", 14, DIM);
        } else {
            draw_centered(fb, SMALL, text, 14, color);
        }
    }

    // Row 2: the label, the largest thing on the screen. It is the one fact the
    // wearer is asserting and the only one no later analysis can recover.
    let lc = if frame.label == LABEL_NONE { DIM } else { label_color(frame.label) };
    draw_centered(fb, STATE, label_name(frame.label), 32, lc);

    // How long it has been held, named rather than bare: two clocks sitting
    // above each other with nothing to tell them apart read as one wrong number.
    if frame.label != LABEL_NONE {
        let mut b = [0u8; 12];
        let mut line = [0u8; 24];
        let n = join(&mut line, &["HELD ", format_duration(frame.label_s as u32, &mut b)]);
        draw_centered(fb, SMALL, core::str::from_utf8(&line[..n]).unwrap_or(""), 66, DIM);
    }

    // Row 3: seconds RECORDED, not elapsed. They are the same number until a
    // cap trips, and the 2026-09-13 match is what happens when only one of them
    // is on the screen.
    let near_cap = frame.rec_cap_s > 0 && frame.rec_s * 10 >= frame.rec_cap_s * 9;
    let mut rb = [0u8; 12];
    draw_centered(
        fb,
        CLOCK,
        format_duration(frame.rec_s, &mut rb),
        82,
        if near_cap { AMBER } else { WHITE },
    );

    // The cap as a bar plus what is left of it, rather than as a second clock:
    // the number the wearer acts on is the remainder, not the ceiling.
    let frac = (frame.rec_s.min(frame.rec_cap_s) * 12)
        .checked_div(frame.rec_cap_s)
        .unwrap_or(0) as usize;
    draw_bar(fb, 66, 116, 108, 5, frac, if near_cap { AMBER } else { WHITE });

    if frame.rec_cap_s > 0 {
        let left_s = frame.rec_cap_s.saturating_sub(frame.rec_s);
        let mut lb = [0u8; 10];
        let mut line = [0u8; 24];
        // Minutes, because a second-resolution countdown invites watching it.
        let n = join(&mut line, &[fmt_u32(left_s.div_ceil(60), &mut lb), " MIN LEFT"]);
        draw_centered(
            fb,
            SMALL,
            core::str::from_utf8(&line[..n]).unwrap_or(""),
            126,
            if near_cap { AMBER } else { DIM },
        );
    }

    // Row 4: the two live features, each on its own ladder. These are what tell
    // the wearer the sensor is seeing a stroke rather than a dead subscription.
    draw_text(fb, SMALL, "GYRO", 40, 146, Align::Left, DIM);
    draw_bar(fb, 82, 148, 96, 6, segments_lit(frame.gyro_mag, &GYRO_LADDER), GREEN);

    draw_text(fb, SMALL, "ACCL", 40, 160, Align::Left, DIM);
    draw_bar(fb, 82, 162, 96, 6, segments_lit(frame.accel_var_k, &ACCEL_LADDER), CYAN);

    // Row 5: saturation, which is signal on this hardware rather than an error
    // — both ranges rail during real strokes — so it is reported, not hidden.
    let sat = frame.sat_accel_pct.max(frame.sat_gyro_pct);
    if sat > 0 {
        let mut sb = [0u8; 10];
        let mut line = [0u8; 24];
        let n = join(&mut line, &["SAT ", fmt_u32(sat as u32, &mut sb), "%"]);
        draw_centered(fb, SMALL, core::str::from_utf8(&line[..n]).unwrap_or(""), 172, YELLOW);
    }

    draw_hr_row(fb, frame, 186);

    // Row 6: the counters, smallest, because they are checked between games
    // rather than during one.
    let mut mk = [0u8; 10];
    let mut mb = [0u8; 12];
    let mut line = [0u8; 32];
    let n = join(
        &mut line,
        &["M ", fmt_u32(frame.markers as u32, &mut mk), "   ", format_mb(frame.rec_kb, &mut mb), " MB"],
    );
    draw_centered(fb, SMALL, core::str::from_utf8(&line[..n]).unwrap_or(""), 202, DIM);
}

fn draw_hr_row(fb: &mut FrameBuf, frame: &Frame, y: i32) {
    if frame.hr_bpm == 0 {
        draw_centered(fb, LABEL_F, "-- BPM", y, DIM);
        return;
    }
    let mut hb = [0u8; 10];
    let bpm = fmt_u32(frame.hr_bpm as u32, &mut hb);
    let src = match frame.hr_source {
        HR_EXTERNAL => " STRAP",
        HR_OPTICAL => " WRIST",
        _ => "",
    };
    let mut line = [0u8; 24];
    let n = join(&mut line, &[bpm, src]);
    // Untrusted readings are drawn dim rather than withheld: the recorder keeps
    // them either way, and a dim number says which.
    let color = if frame.hr_trust == 0 { DIM } else { RED };
    draw_centered(fb, LABEL_F, core::str::from_utf8(&line[..n]).unwrap_or(""), y, color);
}

fn draw_label_picker(fb: &mut FrameBuf, frame: &Frame) {
    draw_centered(fb, SMALL, "LABEL", 16, DIM);

    // Every state on one screen, so picking one is a press count the wearer can
    // learn rather than a scroll they have to watch.
    let row_h = 24;
    let top = 40;
    for i in 1..LABEL_COUNT {
        let y = top + (i as i32 - 1) * row_h;
        let selected = i == frame.label_pick;
        // A caret rather than a filled band: a band has to be grey to sit under
        // text, and grey under a blue or amber state name is the one pairing
        // this panel's four levels a channel cannot hold apart.
        if selected {
            fb.fill_rect(34, y, 4, row_h - 6, WHITE);
        }
        // The state in force keeps its own colour wherever it sits in the list,
        // so "what am I recording" and "what am I about to pick" never collapse
        // into one highlight.
        let color = if i == frame.label {
            label_color(i)
        } else if selected {
            WHITE
        } else {
            DIM
        };
        draw_text(fb, LABEL_F, label_name(i), 48, y, Align::Left, color);
    }

    draw_centered(fb, SMALL, "L2 NEXT   R1 SET", 196, WHITE);
}

fn draw_paused(fb: &mut FrameBuf, frame: &Frame) {
    draw_profile(fb, frame);
    // Over the counters, which are the least useful line while a decision is
    // waiting.
    fb.fill_rect(20, 196, 200, 26, BLACK);
    draw_text(fb, SMALL, "L1 SAVE", 46, 200, Align::Left, WHITE);
    draw_text(fb, SMALL, "L2 DISCARD", 120, 200, Align::Left, AMBER);
}

/// One `LABEL   m:ss` row of the session breakdown.
fn draw_tally(fb: &mut FrameBuf, name: &str, seconds: u32, y: i32, color: Abgr2222) {
    let mut b = [0u8; 12];
    draw_text(fb, LABEL_F, name, 52, y, Align::Left, color);
    draw_text(fb, LABEL_F, format_duration(seconds, &mut b), 188, y, Align::Right, WHITE);
}

/// Rest against play as `1 : N.N`, or nothing when either side is empty.
///
/// The ratio squash is usually discussed in, and the direction chosen so the
/// number grows as the session gets easier: a wearer reads "how much rest did I
/// get per unit of play", which is the question, rather than its reciprocal.
fn draw_work_rest(fb: &mut FrameBuf, rally_s: u32, rest_s: u32, y: i32) {
    if rally_s == 0 || rest_s == 0 {
        return;
    }
    // Integer tenths: there is no float formatter here and the ratio is read to
    // one decimal at most.
    let tenths = (rest_s as u64 * 10 / rally_s as u64).min(999) as u32;
    let mut whole = [0u8; 10];
    let mut line = [0u8; 24];
    let n = join(
        &mut line,
        &["1 : ", fmt_u32(tenths / 10, &mut whole), ".", DIGITS[(tenths % 10) as usize]],
    );
    draw_centered(fb, SMALL, core::str::from_utf8(&line[..n]).unwrap_or(""), y, DIM);
    draw_centered(fb, SMALL, "GAME : REST", y + 14, DIM);
}

const DIGITS: [&str; 10] = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9"];

fn draw_saved(fb: &mut FrameBuf, frame: &Frame) {
    draw_centered(fb, BIG, if frame.saved_ok == 1 { "SAVED" } else { "NOT SAVED" }, 12,
                  if frame.saved_ok == 1 { WHITE } else { AMBER });

    // What the wearer said they were doing, which is the only account of this
    // session that exists: no segmenter runs on this watch.
    draw_tally(fb, "GAME", frame.game_s, 44, GREEN);
    draw_tally(fb, "REST", frame.rest_s, 66, AMBER);
    draw_tally(fb, "OFF", frame.off_court_s, 88, BLUE);

    draw_work_rest(fb, frame.game_s, frame.rest_s, 114);

    // The recorder's own account, smaller, because it is checked once.
    let mut rb = [0u8; 12];
    let mut mk = [0u8; 10];
    let mut mb = [0u8; 12];
    let mut line = [0u8; 40];
    let n = join(
        &mut line,
        &[
            format_duration(frame.rec_s, &mut rb),
            " REC   ",
            fmt_u32(frame.markers as u32, &mut mk),
            " M   ",
            format_mb(frame.rec_kb, &mut mb),
            " MB",
        ],
    );
    draw_centered(fb, SMALL, core::str::from_utf8(&line[..n]).unwrap_or(""), 150, DIM);

    // A session stopped by a cap is saved and incomplete, which is exactly the
    // state that went unnoticed for a 70-minute recording.
    let (text, color) = stop_text(frame.rec_stop);
    if !text.is_empty() {
        draw_centered(fb, SMALL, text, 168, color);
    }

    draw_centered(fb, LABEL_F, "R1  DONE", 190, WHITE);
}

fn draw_discarded(fb: &mut FrameBuf, frame: &Frame) {
    draw_centered(fb, BIG, "DISCARDED", 60, AMBER);
    // The recording is closed out and kept on discard, so saying nothing here
    // would misreport what is on the disk.
    let mut mb = [0u8; 12];
    let mut line = [0u8; 32];
    let n = join(&mut line, &["RECORDING KEPT: ", format_mb(frame.rec_kb, &mut mb), " MB"]);
    draw_centered(fb, SMALL, core::str::from_utf8(&line[..n]).unwrap_or(""), 104, DIM);
    draw_centered(fb, LABEL_F, "R1  DONE", 186, WHITE);
}

/// Concatenate into `out`, returning the byte count written.
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

pub fn render(buf: &mut [u8], width: u32, height: u32, frame: &Frame) {
    // The geometry arrives from a kernel message, so refuse rather than write
    // past the end.
    let Some(mut fb) = Surface::<Abgr2222>::round(buf, width, height) else {
        return;
    };
    fb.clear(BLACK);

    match frame.screen {
        SCREEN_PROFILE => draw_profile(&mut fb, frame),
        SCREEN_LABEL => draw_label_picker(&mut fb, frame),
        SCREEN_PAUSED => draw_paused(&mut fb, frame),
        SCREEN_SAVED => draw_saved(&mut fb, frame),
        SCREEN_DISCARDED => draw_discarded(&mut fb, frame),
        // Default rather than an arm of its own: an out-of-range screen byte is
        // a bug upstream, and READY loses the wearer the least.
        _ => draw_ready(&mut fb, frame),
    }
}

/// # Safety
/// `buf` must point to at least `buf_len` writable bytes and `frame` to a valid
/// `squash_gui_frame`, both valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn squash_gui_render(
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

/// Advance the picker, wrapping past the last real label. `LABEL_NONE` is not
/// offered: it is what a recording starts as, never something to choose.
#[no_mangle]
pub extern "C" fn squash_gui_label_next(pick: u8) -> u8 {
    if pick >= LABEL_COUNT - 1 {
        LABEL_RALLY
    } else {
        pick + 1
    }
}

/// Where the picker opens when nothing has been chosen yet.
pub const LABEL_DEFAULT: u8 = LABEL_GAME;

/// The hot path: a session alternates between playing a game and not, so one
/// button toggles that pair and the picker is needed for everything else.
#[no_mangle]
pub extern "C" fn squash_gui_label_toggle(label: u8) -> u8 {
    if label == LABEL_GAME {
        LABEL_REST
    } else {
        LABEL_GAME
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const W: u32 = 240;
    const H: u32 = 240;

    fn draw(frame: &Frame) -> Vec<u8> {
        let mut buf = vec![0u8; (W * H) as usize];
        render(&mut buf, W, H, frame);
        buf
    }

    fn lit_pixels(buf: &[u8]) -> usize {
        buf.iter().filter(|&&b| b != BLACK.0).count()
    }

    #[test]
    fn duration_uses_the_shortest_unambiguous_form() {
        let mut b = [0u8; 12];
        assert_eq!(format_duration(0, &mut b), "0:00");
        assert_eq!(format_duration(59, &mut b), "0:59");
        assert_eq!(format_duration(600, &mut b), "10:00");
        assert_eq!(format_duration(3600, &mut b), "1:00:00");
        assert_eq!(format_duration(5400, &mut b), "1:30:00");
    }

    #[test]
    fn megabytes_round_to_one_decimal() {
        let mut b = [0u8; 12];
        assert_eq!(format_mb(0, &mut b), "0.0");
        assert_eq!(format_mb(1024, &mut b), "1.0");
        assert_eq!(format_mb(1536, &mut b), "1.5");
        assert_eq!(format_mb(32 * 1024, &mut b), "32.0");
    }

    #[test]
    fn every_screen_draws_something() {
        for screen in [
            SCREEN_READY,
            SCREEN_PROFILE,
            SCREEN_LABEL,
            SCREEN_PAUSED,
            SCREEN_SAVED,
            SCREEN_DISCARDED,
        ] {
            let f = Frame { screen, label: LABEL_RALLY, label_pick: LABEL_RALLY, ..Frame::default() };
            assert!(lit_pixels(&draw(&f)) > 0, "screen {screen} drew nothing");
        }
    }

    #[test]
    fn the_ladder_is_monotonic_so_a_bigger_reading_never_lights_fewer_segments() {
        for ladder in [&GYRO_LADDER, &ACCEL_LADDER] {
            for w in ladder.windows(2) {
                assert!(w[0] < w[1], "ladder is not strictly increasing");
            }
            assert_eq!(segments_lit(0, ladder), 0);
            assert_eq!(segments_lit(u32::MAX, ladder), 12);
        }
    }

    #[test]
    fn the_cap_bar_fills_rather_than_overflowing_when_the_cap_is_passed() {
        // rec_s past the cap is reachable: the recorder stops on the sample that
        // would cross it, and the Service keeps sending the session's clock.
        let f = Frame { screen: SCREEN_PROFILE, rec_s: 9_999, rec_cap_s: 1_800, ..Frame::default() };
        assert!(lit_pixels(&draw(&f)) > 0);
    }

    #[test]
    fn toggle_returns_to_game_from_anything_that_is_not_game() {
        assert_eq!(squash_gui_label_toggle(LABEL_GAME), LABEL_REST);
        assert_eq!(squash_gui_label_toggle(LABEL_REST), LABEL_GAME);
        assert_eq!(squash_gui_label_toggle(LABEL_DRILL), LABEL_GAME);
        assert_eq!(squash_gui_label_toggle(LABEL_NONE), LABEL_GAME);
    }

    #[test]
    fn the_picker_cycles_the_real_labels_and_never_offers_none() {
        let mut seen = Vec::new();
        let mut p = LABEL_RALLY;
        for _ in 0..LABEL_COUNT - 1 {
            seen.push(p);
            p = squash_gui_label_next(p);
        }
        assert_eq!(p, LABEL_RALLY, "the cycle did not close");
        assert!(!seen.contains(&LABEL_NONE));
        assert_eq!(seen.len(), (LABEL_COUNT - 1) as usize);
    }

    #[test]
    fn a_label_name_exists_for_every_value_the_picker_can_reach() {
        for i in 1..LABEL_COUNT {
            assert_ne!(label_name(i), "NO LABEL", "label {i} has no name");
        }
        assert_eq!(label_name(LABEL_NONE), "NO LABEL");
    }

    #[test]
    fn nothing_is_drawn_outside_the_bezel() {
        // The disc mask is the surface's, so this holds for every screen at once.
        let f = Frame { screen: SCREEN_PROFILE, label: LABEL_RALLY, ..Frame::default() };
        let buf = draw(&f);
        let (cx, cy, r) = (119.5f32, 119.5f32, 120.0f32);
        for y in 0..H {
            for x in 0..W {
                let dx = x as f32 - cx;
                let dy = y as f32 - cy;
                if dx * dx + dy * dy > r * r {
                    assert_eq!(buf[(y * W + x) as usize], BLACK.0, "lit pixel outside the disc at {x},{y}");
                }
            }
        }
    }
}
