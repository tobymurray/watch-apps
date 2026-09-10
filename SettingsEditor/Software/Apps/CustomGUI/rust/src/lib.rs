#![cfg_attr(not(feature = "std"), no_std)]

//! One watch setting per screen: the name, the value, where in the list it
//! sits, and what may be claimed about it. L1 and L2 page; R1 opens an editor
//! on the value and R1 again commits it; R2 cancels, or exits.
//!
//! The editor state machine lives here rather than in the C++ shell because it
//! is the part worth testing, and nothing in it needs a kernel.

use embedded_graphics::{prelude::*, primitives::{PrimitiveStyle, Triangle}};
use inscribed_disc::color::Abgr2222;
use inscribed_disc::surface::Surface;
use textkit::{faces, Align, Face, Style};

/// What may be claimed about the field on screen. Every one of these leaves the
/// setting somewhere different, and a value that just changes cannot tell them
/// apart.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Status {
    /// Read, and a change would also reach settings.json.
    Ok,
    /// Saving is switched off, so a change is live only. Nothing went wrong.
    LiveOnly,
    /// The value changed and took effect, but the file was not written.
    NotSaved,
    /// The kernel's own report of this field did not agree with the raw read,
    /// so nothing about it can be claimed and nothing here may write it.
    Unconfirmed,
    /// The file holds a value this app has no control for. Rewriting it from
    /// the control it does have would discard whatever set it.
    NotExpressible,
    /// The firmware gate refused. The value still comes from a supported
    /// message, so it is true; it just cannot be changed here.
    Unsupported,
    /// The field has never been set. Zero is not a height or a weight anyone
    /// has, so it is drawn as absent rather than as a number.
    Unset,
}

impl Status {
    /// Anything unrecognised reads as `Unconfirmed`, so a value this build does
    /// not know about cannot be drawn as a confident answer.
    fn from_u8(raw: u8) -> Status {
        match raw {
            0 => Status::Ok,
            1 => Status::LiveOnly,
            2 => Status::NotSaved,
            4 => Status::NotExpressible,
            5 => Status::Unsupported,
            6 => Status::Unset,
            _ => Status::Unconfirmed,
        }
    }

    /// True when a number or word may be drawn for this field at all.
    fn shows_a_value(self) -> bool {
        self != Status::Unconfirmed
    }

    /// True when R1 may open an editor on it.
    pub fn is_editable(self) -> bool {
        matches!(
            self,
            Status::Ok | Status::LiveOnly | Status::NotSaved | Status::Unset
        )
    }

    /// What the screen says about the value, above the button hints.
    ///
    /// `Unset` has none: its whole message goes where the value would be, since
    /// there is no value to caption.
    fn line(self) -> &'static str {
        match self {
            Status::Ok | Status::Unset => "",
            // MEASURED: 141 px in the caption face, the longest of these, against
            // the 222 px chord at the row their caps stand on. At the title
            // face's weight it is 185 px and all but touches the rim.
            Status::LiveOnly => "REVERTS ON REBOOT",
            Status::NotSaved => "NOT SAVED",
            Status::Unconfirmed => "CANT CONFIRM",
            Status::NotExpressible => "SET ON PHONE",
            Status::Unsupported => "VIEW ONLY",
        }
    }
}

/// Mirrors `settings_editor_state` field for field. `value` is the live value
/// the shell read, or the pending one while `editing` -- the shell writes
/// nothing until R1 commits, because a commit is a whole-file rewrite and a
/// rename pair, and one per press of a stepper is fifteen of each to move a
/// weight from 90 to 75.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct State {
    pub field: u8,
    pub status: u8,
    pub editing: u8,
    pub field_count: u8,
    pub value: u32,
    pub value_min: u32,
    pub value_max: u32,
}

impl State {
    fn status(&self) -> Status {
        Status::from_u8(self.status)
    }

    fn is_editing(&self) -> bool {
        self.editing != 0 && self.status().is_editable()
    }

    /// A field with no value yet, and not being given one right now.
    fn is_unset(&self) -> bool {
        self.status() == Status::Unset && !self.is_editing()
    }

    fn spec(&self) -> &'static FieldSpec {
        &FIELDS[(self.field as usize).min(FIELDS.len() - 1)]
    }
}

/// The name, the unit and the step for one field, in the order
/// `SettingsKit`'s `kEditable` declares them. Two tables indexed by one
/// position: `field_count` is checked against `FIELDS.len()` on every frame,
/// and `abi_fingerprint` folds in each name so a reordering on one side shows
/// up at startup rather than as the wrong label over the right number.
pub struct FieldSpec {
    pub name: &'static str,
    /// Drawn to the right of the number, or empty for a field drawn as a word.
    pub unit: &'static str,
    /// The two words a boolean or two-token enum is drawn as, low then high.
    pub words: Option<[&'static str; 2]>,
    /// What one press of L1 or L2 moves.
    ///
    /// There is no auto-repeat to hide behind: `LONG_PRESS` and `HOLD_*` are
    /// not forwarded to screens, and Spin's own long-press discard never fired
    /// once on a watch (`Spin/README.md`). So a press is a press, and the step
    /// is the field's own granularity rather than a fraction of its range --
    /// nobody crosses a range, they nudge a value. The cost of that choice is
    /// stated as an assertion in
    /// `press_counts_for_the_edits_a_wearer_actually_makes`.
    pub step: u32,
    /// Where the editor opens on a field that has never been set. Zero is not a
    /// height or a weight, and stepping up from the bottom of the range would be
    /// sixty-five presses.
    ///
    /// ASSERTED, not measured: the three daily goals use the firmware's own
    /// constructor defaults (30, 5000, 10), and height and weight have none, so
    /// those two are chosen. A seed is shown, not written -- nothing reaches the
    /// file until R1.
    pub seed: u32,
}

pub const FIELDS: [FieldSpec; 8] = [
    FieldSpec {
        name: "UNITS",
        unit: "",
        words: Some(["METRIC", "IMPERIAL"]),
        step: 1,
        seed: 0,
    },
    FieldSpec {
        name: "NOTIFICATIONS",
        unit: "",
        words: Some(["OFF", "ON"]),
        step: 1,
        seed: 0,
    },
    FieldSpec {
        name: "ACTIVE MINUTES",
        unit: "MIN",
        words: None,
        step: 5,
        seed: 30,
    },
    FieldSpec {
        name: "STEP GOAL",
        unit: "STEPS",
        words: None,
        step: 500,
        seed: 5000,
    },
    FieldSpec {
        name: "FLOOR GOAL",
        unit: "FLOORS",
        words: None,
        step: 1,
        seed: 10,
    },
    FieldSpec {
        name: "HEIGHT",
        unit: "CM",
        words: None,
        step: 1,
        seed: 170,
    },
    FieldSpec {
        name: "WEIGHT",
        unit: "KG",
        words: None,
        step: 1,
        seed: 70,
    },
    FieldSpec {
        name: "MAX HEART RATE",
        unit: "BPM",
        words: None,
        step: 1,
        seed: 180,
    },
];

// --- The editor -------------------------------------------------------------

/// The four buttons, by what they do here rather than by which switch they are.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(u8)]
pub enum Button {
    Up = 0,
    Down = 1,
    Select = 2,
    Back = 3,
}

/// What the shell has to do about a press. Everything that touches the watch is
/// out here, so the state machine above it needs no kernel to be tested.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(u8)]
pub enum Action {
    /// The frame changed and nothing else.
    Redraw = 0,
    /// A different field is on screen; read its live value and range.
    ReadField = 1,
    /// Write `state.value` to the field, live and then to settings.json.
    Commit = 2,
    /// Leave the app.
    Exit = 3,
    /// The press meant nothing here.
    Ignore = 4,
}

fn step_towards(value: u32, by: u32, min: u32, max: u32, up: bool) -> u32 {
    if up {
        // Clamped rather than wrapped: a step goal that rolls from 50,000 to
        // 500 on one press is a change nobody asked for.
        if value >= max {
            max
        } else {
            value.saturating_add(by).min(max)
        }
    } else if value <= min {
        min
    } else {
        value.saturating_sub(by).max(min)
    }
}

/// Applies a press to `state` and says what the shell must do about it.
pub fn on_button(state: &mut State, button: Button) -> Action {
    if state.is_editing() {
        return match button {
            Button::Up | Button::Down => {
                let spec = state.spec();
                let next = step_towards(
                    state.value,
                    spec.step,
                    state.value_min,
                    state.value_max,
                    button == Button::Up,
                );
                if next == state.value {
                    return Action::Ignore;
                }
                state.value = next;
                Action::Redraw
            }
            Button::Select => {
                state.editing = 0;
                Action::Commit
            }
            Button::Back => {
                // Discarded, not committed: the pending value was never written
                // anywhere, and re-reading is what puts the screen back to the
                // truth rather than to what this app remembered.
                state.editing = 0;
                Action::ReadField
            }
        };
    }

    let count = field_count(state);
    match button {
        Button::Up => {
            state.field = if state.field == 0 {
                count - 1
            } else {
                state.field - 1
            };
            Action::ReadField
        }
        Button::Down => {
            state.field = (state.field + 1) % count;
            Action::ReadField
        }
        Button::Select => {
            if !state.status().is_editable() {
                return Action::Ignore;
            }
            if state.status() == Status::Unset {
                let spec = state.spec();
                state.value = spec.seed.clamp(state.value_min, state.value_max);
            }
            state.editing = 1;
            Action::Redraw
        }
        Button::Back => Action::Exit,
    }
}

/// How many fields this build can page through, whatever the shell claimed:
/// a count past the end of `FIELDS` would draw one field's name over another's
/// value.
fn field_count(state: &State) -> u8 {
    let declared = state.field_count as usize;
    if declared == 0 || declared > FIELDS.len() {
        FIELDS.len() as u8
    } else {
        declared as u8
    }
}

/// An external change to the field being edited abandons the edit.
///
/// The phone app can set the same field while this screen is open, and a step
/// the wearer made against the old value would write a number they never saw.
/// The shell calls this when its own re-read comes back different.
pub fn external_change(state: &mut State, live_value: u32) {
    state.editing = 0;
    state.value = live_value;
}

// --- Palette ----------------------------------------------------------------

/// Bright on dark, because that is the only contrast this glass is proven to
/// render: an early black-on-white readout came back as a blank white band.
const GROUND: Abgr2222 = Abgr2222::BLACK;
const HEADING: Abgr2222 = Abgr2222::WHITE;
const CHROME: Abgr2222 = Abgr2222::GREY;
const DIM: Abgr2222 = Abgr2222::DARK_GREY;
const WARN_ACCENT: Abgr2222 = Abgr2222::AMBER;

// --- Layout -----------------------------------------------------------------

const PANEL_CX: i32 = 120;

/// MEASURED with `textkit`'s `measure` example, against the round mask's chord
/// at each row: the widest field name is `MAX HEART RATE` at 152 px in the title
/// face, whose caps then stand from row 40, where the lit chord is 180 px. The
/// 27 px title row UnitToggle uses has only 153 px of glass and would clip it.
const TITLE_BASELINE_Y: i32 = 59;

/// MEASURED: the widest value is `50000` at 90 px in the number face and
/// `IMPERIAL` at 89 px in the value-word face, both far inside the 237 px this
/// text's narrowest row has.
const VALUE_BASELINE_Y: i32 = 140;
/// The number face's ascent is 29 and the unit face's 15, so this sits the unit
/// on the number's baseline.
const UNIT_GAP_PX: i32 = 7;

/// MEASURED: the widest caption is `REVERTS ON REBOOT` at 141 px in the caption
/// face, against the 210 px chord at this baseline -- the narrow row for text
/// below the centre line, where the caps are the wide one above it.
const STATUS_BASELINE_Y: i32 = 178;

const DOTS_Y: i32 = 198;
const DOT_DIAMETER: u32 = 6;
const DOT_PITCH: i32 = 14;

/// MEASURED: the widest footer is `L1/L2 PAGE  R1 EDIT` at 112 px in the hint
/// face against the 131 px chord at this row, which UnitToggle already ships a
/// 118 px footer at.
const FOOTER_BASELINE_Y: i32 = 220;

/// Where the stepper's arrows sit, above and below the value.
const UP_ARROW_TIP_Y: i32 = 96;
const DOWN_ARROW_TIP_Y: i32 = 172;
const ARROW_HALF_W: i32 = 11;
const ARROW_HEIGHT: i32 = 12;

/// Poppins, from the atlases TextKit generates; `Docs/TEXT.md` at the repo root
/// is why these and not others. `TITLE_FACE` names the field, `VALUE_WORD_FACE`
/// and `NUMBER_FACE` are the value itself, `CAPTION_FACE` is what may be
/// claimed about it and `HINT_FACE` the buttons.
///
/// The number face carries `0123456789:` and nothing else, which is why every
/// non-numeric value goes through `VALUE_WORD_FACE`.
static TITLE_FACE: &Face = &faces::SEMIBOLD_18_ASCII;
static VALUE_WORD_FACE: &Face = &faces::SEMIBOLD_20_ASCII;
static NUMBER_FACE: &Face = &faces::SEMIBOLD_27_CLOCK;
static UNIT_FACE: &Face = &faces::REGULAR_14_ASCII;
static CAPTION_FACE: &Face = &faces::REGULAR_14_ASCII;
static HINT_FACE: &Face = &faces::REGULAR_12_ASCII;

/// The footer says what the buttons do and nothing else.
///
/// Saving is off by default, so `LiveOnly` is the state of every field on an
/// install nobody has configured -- and a footer that spent itself on
/// `REVERTS ON REBOOT` there would never once show a wearer how to page or
/// edit. That message is a caption on the value, so it goes in the status line
/// instead. The cost is that a refused firmware no longer names the version it
/// wants; `VIEW ONLY` on all eight fields is the message, and the version is
/// not something a wrist can act on.
const FOOTER_BROWSE: &str = "L1/L2 PAGE  R1 EDIT";
const FOOTER_LOOK_ONLY: &str = "L1/L2 PAGE";
const FOOTER_EDITING: &str = "R1 SAVE  R2 CANCEL";

/// A field that has never been set says so where its value would be. The
/// number face carries `0123456789:` and nothing else, so a dash there draws
/// TextKit's missing-glyph box -- `measure` reports `missing 1` for it.
const VALUE_UNSET: &str = "NOT SET";

/// Decimal into a caller-owned buffer. No `write!`, no allocation: `snprintf`
/// and `std::string` each drag libstdc++'s exception runtime into an
/// `-fno-exceptions` app, which measured 10,036 bytes on NotifyToggle.
struct Digits {
    bytes: [u8; 10],
    len: usize,
}

impl Digits {
    fn of(mut value: u32) -> Digits {
        let mut reversed = [0u8; 10];
        let mut n = 0;
        loop {
            reversed[n] = b'0' + (value % 10) as u8;
            n += 1;
            value /= 10;
            if value == 0 {
                break;
            }
        }
        let mut bytes = [0u8; 10];
        for i in 0..n {
            bytes[i] = reversed[n - 1 - i];
        }
        Digits { bytes, len: n }
    }

    fn as_str(&self) -> &str {
        core::str::from_utf8(&self.bytes[..self.len]).unwrap_or("")
    }
}

fn text_at(fb: &mut Surface<Abgr2222>, face: &Face, s: &str, x: i32, baseline: i32, align: Align, color: Abgr2222) {
    face.draw(fb, s, x, baseline, Style::new(color, GROUND, align)).ok();
}

fn text(fb: &mut Surface<Abgr2222>, face: &Face, s: &str, x: i32, baseline: i32, color: Abgr2222) {
    text_at(fb, face, s, x, baseline, Align::Center, color);
}

fn text_left(fb: &mut Surface<Abgr2222>, face: &Face, s: &str, x: i32, baseline: i32, color: Abgr2222) {
    text_at(fb, face, s, x, baseline, Align::Left, color);
}

/// The number and its unit as one centred group, so a three-digit value and a
/// five-digit one are both centred on the panel rather than on the number.
/// Returns where the number itself is centred, which is where the stepper's
/// arrows go -- over what changes rather than over the group.
fn draw_number_with_unit(fb: &mut Surface<Abgr2222>, digits: &str, unit: &str, color: Abgr2222) -> i32 {
    let number_w = NUMBER_FACE.measure(digits).advance;
    let unit_w = if unit.is_empty() {
        0
    } else {
        UNIT_FACE.measure(unit).advance
    };
    let gap = if unit.is_empty() { 0 } else { UNIT_GAP_PX };
    let total = number_w + gap + unit_w;
    let left = PANEL_CX - total / 2;

    text_left(fb, NUMBER_FACE, digits, left, VALUE_BASELINE_Y, color);
    if !unit.is_empty() {
        text_left(
            fb,
            UNIT_FACE,
            unit,
            left + number_w + gap,
            VALUE_BASELINE_Y,
            CHROME,
        );
    }
    left + number_w / 2
}

/// A solid triangle pointing away from the value, dim when the value is already
/// at that end of its range -- which is where the clamp shows up on screen,
/// rather than as a press that silently does nothing.
fn draw_arrow(fb: &mut Surface<Abgr2222>, cx: i32, tip_y: i32, upward: bool, live: bool) {
    let base_y = if upward {
        tip_y + ARROW_HEIGHT
    } else {
        tip_y - ARROW_HEIGHT
    };
    Triangle::new(
        Point::new(cx, tip_y),
        Point::new(cx - ARROW_HALF_W, base_y),
        Point::new(cx + ARROW_HALF_W, base_y),
    )
    .into_styled(PrimitiveStyle::with_fill(if live { HEADING } else { DIM }))
    .draw(fb)
    .ok();
}

/// One dot a field, the current one bright. A twelve-item list will not fit
/// this panel at a legible size, so the list is a position rather than a view.
///
/// `a_row_of_dots_is_centred_at_every_count` holds the centring.
fn draw_position(fb: &mut Surface<Abgr2222>, index: u8, count: u8) {
    if count < 2 {
        return;
    }
    let width = DOT_DIAMETER as i32;
    let left = fb.width() / 2 - ((count as i32 - 1) * DOT_PITCH) / 2 - width / 2;
    for i in 0..count as i32 {
        let colour = if i == index as i32 { HEADING } else { DIM };
        fb.fill_rect(left + i * DOT_PITCH, DOTS_Y, width, width, colour);
    }
}

fn footer_for(state: &State) -> &'static str {
    if state.is_editing() {
        FOOTER_EDITING
    } else if state.status().is_editable() {
        FOOTER_BROWSE
    } else {
        FOOTER_LOOK_ONLY
    }
}

fn draw(fb: &mut Surface<Abgr2222>, state: &State) {
    let spec = state.spec();
    let count = field_count(state);
    let status = state.status();
    let editing = state.is_editing();

    text(
        fb,
        TITLE_FACE,
        spec.name,
        PANEL_CX,
        TITLE_BASELINE_Y,
        HEADING,
    );

    let mut stepper_cx = PANEL_CX;
    if status.shows_a_value() {
        let value_color = if editing { WARN_ACCENT } else { HEADING };
        if state.is_unset() {
            text(
                fb,
                VALUE_WORD_FACE,
                VALUE_UNSET,
                PANEL_CX,
                VALUE_BASELINE_Y,
                CHROME,
            );
        } else if let Some(words) = spec.words {
            let word = words[(state.value != 0) as usize];
            text(
                fb,
                VALUE_WORD_FACE,
                word,
                PANEL_CX,
                VALUE_BASELINE_Y,
                value_color,
            );
        } else {
            stepper_cx =
                draw_number_with_unit(fb, Digits::of(state.value).as_str(), spec.unit, value_color);
        }
    }

    if editing {
        draw_arrow(
            fb,
            stepper_cx,
            UP_ARROW_TIP_Y,
            true,
            state.value < state.value_max,
        );
        draw_arrow(
            fb,
            stepper_cx,
            DOWN_ARROW_TIP_Y,
            false,
            state.value > state.value_min,
        );
    } else {
        let line = status.line();
        if !line.is_empty() {
            let color = match status {
                Status::NotSaved | Status::Unconfirmed => WARN_ACCENT,
                _ => CHROME,
            };
            text(fb, CAPTION_FACE, line, PANEL_CX, STATUS_BASELINE_Y, color);
        }
    }

    draw_position(fb, state.field.min(count - 1), count);
    text(
        fb,
        HINT_FACE,
        footer_for(state),
        PANEL_CX,
        FOOTER_BASELINE_Y,
        CHROME,
    );
}

pub fn render(buf: &mut [u8], width: u32, height: u32, state: &State) {
    // The geometry arrives from a kernel message, so a surface that refuses is
    // better than a renderer that writes past the end.
    let Some(mut fb) = Surface::<Abgr2222>::round(buf, width, height) else {
        return;
    };
    fb.clear(GROUND);
    draw(&mut fb, state);
}

// --- C ABI ------------------------------------------------------------------

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// The struct layout, and the field table's own shape.
///
/// The layout half is the usual check that a stale archive is not linked
/// against a newer header. The table half is why this fingerprint is not just
/// `size_of`: `SettingsKit`'s `kEditable` and `FIELDS` are two tables indexed by
/// one position, so a row inserted in one and not the other would draw the
/// wrong name over the right number. Folding in each name's length and first
/// character catches an insertion, a deletion and every reordering that does
/// not preserve both -- which is what the two same-length names `HEIGHT` and
/// `WEIGHT` need the first character for.
const fn abi_fingerprint() -> u32 {
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<State>());
    let h = fnv1a(h, core::mem::align_of::<State>());
    let h = fnv1a(h, core::mem::offset_of!(State, field));
    let h = fnv1a(h, core::mem::offset_of!(State, status));
    let h = fnv1a(h, core::mem::offset_of!(State, editing));
    let h = fnv1a(h, core::mem::offset_of!(State, field_count));
    let h = fnv1a(h, core::mem::offset_of!(State, value));
    let h = fnv1a(h, core::mem::offset_of!(State, value_min));
    let mut h = fnv1a(h, core::mem::offset_of!(State, value_max));

    h = fnv1a(h, FIELDS.len());
    let mut i = 0;
    while i < FIELDS.len() {
        h = fnv1a(h, FIELDS[i].name.len());
        h = fnv1a(h, FIELDS[i].name.as_bytes()[0] as usize);
        i += 1;
    }
    h
}

/// Lets the caller confirm it was linked against the archive it thinks it was.
/// The compile-time assertions below cannot do this: a stale archive and a newer
/// header each satisfy their own, having been compiled at different times.
#[no_mangle]
pub extern "C" fn settings_editor_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

#[no_mangle]
pub extern "C" fn settings_editor_field_count() -> u32 {
    FIELDS.len() as u32
}

// Per field, because a size check passes when two fields are swapped.
const _: () = assert!(core::mem::size_of::<State>() == 16);
const _: () = assert!(core::mem::align_of::<State>() == 4);
const _: () = assert!(core::mem::offset_of!(State, field) == 0);
const _: () = assert!(core::mem::offset_of!(State, status) == 1);
const _: () = assert!(core::mem::offset_of!(State, editing) == 2);
const _: () = assert!(core::mem::offset_of!(State, field_count) == 3);
const _: () = assert!(core::mem::offset_of!(State, value) == 4);
const _: () = assert!(core::mem::offset_of!(State, value_min) == 8);
const _: () = assert!(core::mem::offset_of!(State, value_max) == 12);

/// # Safety
/// `buf` must point to at least `buf_len` writable bytes and `state` to a valid
/// `settings_editor_state`, both valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn settings_editor_render(
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

/// # Safety
/// `state` must point to a valid, writable `settings_editor_state`.
///
/// Returns an `Action`. An unrecognised `button` is `Action::Ignore`.
#[no_mangle]
pub unsafe extern "C" fn settings_editor_on_button(state: *mut State, button: u8) -> u8 {
    if state.is_null() {
        return Action::Ignore as u8;
    }
    let pressed = match button {
        0 => Button::Up,
        1 => Button::Down,
        2 => Button::Select,
        3 => Button::Back,
        _ => return Action::Ignore as u8,
    };
    on_button(&mut *state, pressed) as u8
}

#[cfg(test)]
mod tests {
    use super::*;

    const W: u32 = 240;
    const H: u32 = 240;

    fn browsing(field: u8, value: u32) -> State {
        State {
            field,
            status: Status::Ok as u8,
            editing: 0,
            field_count: FIELDS.len() as u8,
            value,
            value_min: 25,
            value_max: 250,
        }
    }

    fn frame(state: &State) -> Vec<u8> {
        let mut buf = vec![0u8; (W * H) as usize];
        render(&mut buf, W, H, state);
        buf
    }

    fn px(buf: &[u8], x: i32, y: i32) -> u8 {
        buf[(y as u32 * W + x as u32) as usize]
    }

    /// Where `draw_number_with_unit` puts the number's centre.
    fn number_centre(digits: &str, unit: &str) -> i32 {
        let number_w = NUMBER_FACE.measure(digits).advance;
        let unit_w = UNIT_FACE.measure(unit).advance;
        let left = PANEL_CX - (number_w + UNIT_GAP_PX + unit_w) / 2;
        left + number_w / 2
    }

    fn any_lit(buf: &[u8], rows: core::ops::Range<i32>) -> bool {
        rows.flat_map(|y| (0..W as i32).map(move |x| (x, y)))
            .any(|(x, y)| px(buf, x, y) != GROUND.0)
    }

    /// The ink is centred on the panel at every count, odd and even.
    ///
    /// Falsified by a pitch or diameter that makes an exactly centred row
    /// impossible: at 14 and 6 every term of the centring is even, so no
    /// division truncates and the row lands exactly on 120.0.
    #[test]
    fn a_row_of_dots_is_centred_at_every_count() {
        for count in 2..=FIELDS.len() as u8 {
            let mut buf = vec![0u8; (W * H) as usize];
            {
                let mut fb = Surface::<Abgr2222>::round(&mut buf, W, H).unwrap();
                fb.clear(GROUND);
                draw_position(&mut fb, 0, count);
            }
            let row = DOTS_Y + DOT_DIAMETER as i32 / 2;
            let lit: Vec<i32> =
                (0..W as i32).filter(|&x| px(&buf, x, row) != GROUND.0).collect();
            let (first, last) = (lit[0], lit[lit.len() - 1] + 1);
            let centre = (first + last) as f32 / 2.0;
            assert!(
                (centre - W as f32 / 2.0).abs() < 0.01,
                "count {count}: ink spans [{first},{last}), centre {centre}"
            );
        }
    }

    #[test]
    fn abi_fingerprint_is_stable() {
        assert_eq!(settings_editor_abi_fingerprint(), abi_fingerprint());
        assert_eq!(settings_editor_field_count() as usize, FIELDS.len());
    }

    #[test]
    fn undersized_buffer_is_a_no_op() {
        let mut buf = vec![0u8; 10];
        render(&mut buf, W, H, &browsing(6, 90));
        assert!(buf.iter().all(|&b| b == 0));
    }

    /// Every screen has to survive the bezel: the panel is round, and anything
    /// outside the inscribed circle is behind it. This is what the measured
    /// title row exists for -- the widest field name does not fit where
    /// UnitToggle puts its own.
    #[test]
    fn nothing_is_drawn_outside_the_round_mask() {
        let r = (W / 2) as i32;
        for field in 0..FIELDS.len() as u8 {
            for status in 0..8u8 {
                for editing in [0u8, 1] {
                    let mut s = browsing(field, 50000);
                    s.status = status;
                    s.editing = editing;
                    s.value_min = 0;
                    s.value_max = 50000;
                    let f = frame(&s);
                    for y in 0..H as i32 {
                        for x in 0..W as i32 {
                            let (dx, dy) = (x - r, y - r);
                            if dx * dx + dy * dy > r * r && px(&f, x, y) != GROUND.0 {
                                panic!(
                                    "field {field} status {status} editing {editing} lit \
                                     ({x},{y}) outside the mask"
                                );
                            }
                        }
                    }
                }
            }
        }
    }

    /// A pending value must not draw the same confident white as a committed
    /// one: it is not the setting yet, and nothing has been written.
    #[test]
    fn a_pending_value_is_visually_distinct_from_a_committed_one() {
        let committed = frame(&browsing(6, 90));
        let mut pending = browsing(6, 90);
        pending.editing = 1;
        let pending = frame(&pending);
        assert_ne!(committed, pending);

        let value_rows = VALUE_BASELINE_Y - NUMBER_FACE.ascent as i32..VALUE_BASELINE_Y;
        let has = |buf: &Vec<u8>, want: u8| {
            value_rows
                .clone()
                .flat_map(|y| (0..W as i32).map(move |x| (x, y)))
                .any(|(x, y)| px(buf, x, y) == want)
        };
        assert!(
            has(&committed, HEADING.0),
            "a committed value is drawn white"
        );
        assert!(
            has(&pending, WARN_ACCENT.0),
            "a pending value is drawn amber"
        );
        assert!(!has(&committed, WARN_ACCENT.0));
    }

    /// The stepper's arrows only exist while editing, and only then do L1 and
    /// L2 change a value rather than the field.
    #[test]
    fn the_arrows_appear_only_while_editing() {
        let arrow_rows = UP_ARROW_TIP_Y..UP_ARROW_TIP_Y + ARROW_HEIGHT;

        assert!(!any_lit(&frame(&browsing(6, 90)), arrow_rows.clone()));

        let mut editing = browsing(6, 90);
        editing.editing = 1;
        assert!(any_lit(&frame(&editing), arrow_rows));
    }

    /// Where the clamp shows up: the arrow the wearer cannot move is dim, so a
    /// press that does nothing is explained before it is pressed.
    #[test]
    fn an_arrow_at_the_end_of_the_range_is_dim() {
        let mut at_top = browsing(7, 220);
        at_top.editing = 1;
        at_top.value_min = 90;
        at_top.value_max = 220;
        let f = frame(&at_top);
        // The arrows sit over the number, which is left of centre once the unit
        // is beside it, so the probe follows the number rather than the panel.
        let cx = number_centre("220", "BPM");
        assert_eq!(px(&f, cx, UP_ARROW_TIP_Y + ARROW_HEIGHT - 1), DIM.0);
        assert_eq!(px(&f, cx, DOWN_ARROW_TIP_Y - ARROW_HEIGHT + 1), HEADING.0);
    }

    /// A field whose value could not be confirmed draws no value at all:
    /// inventing one of a boolean's two answers is worse than admitting
    /// neither, and a number is worse still.
    #[test]
    fn an_unconfirmed_field_draws_no_value() {
        let mut s = browsing(3, 5000);
        s.status = Status::Unconfirmed as u8;
        let f = frame(&s);
        let value_rows = VALUE_BASELINE_Y - NUMBER_FACE.ascent as i32..VALUE_BASELINE_Y;
        assert!(!any_lit(&f, value_rows));
    }

    /// Zero is not a height or a weight, so an unset field says so rather than
    /// offering `0` as the current value.
    #[test]
    fn an_unset_field_draws_no_number() {
        let mut s = browsing(5, 0);
        s.status = Status::Unset as u8;
        let f = frame(&s);
        let value_rows = VALUE_BASELINE_Y - NUMBER_FACE.ascent as i32..VALUE_BASELINE_Y;
        assert!(any_lit(&f, value_rows), "the placeholder is drawn");
        assert_ne!(f, frame(&browsing(5, 0)));
    }

    #[test]
    fn an_unknown_status_reads_as_unconfirmed() {
        assert_eq!(Status::from_u8(200), Status::Unconfirmed);
        assert!(!Status::from_u8(200).is_editable());
    }

    /// A field count the shell got wrong must not index past the table.
    #[test]
    fn a_field_index_or_count_past_the_table_still_draws_a_field() {
        let mut s = browsing(200, 90);
        s.field_count = 200;
        let f = frame(&s);
        assert!(any_lit(&f, TITLE_BASELINE_Y - 19..TITLE_BASELINE_Y));
    }

    #[test]
    fn paging_wraps_in_both_directions_and_asks_for_a_read() {
        let mut s = browsing(0, 90);
        assert_eq!(on_button(&mut s, Button::Up), Action::ReadField);
        assert_eq!(s.field, FIELDS.len() as u8 - 1);
        assert_eq!(on_button(&mut s, Button::Down), Action::ReadField);
        assert_eq!(s.field, 0);
    }

    #[test]
    fn select_opens_an_editor_only_on_a_field_that_can_be_written() {
        let mut s = browsing(6, 90);
        assert_eq!(on_button(&mut s, Button::Select), Action::Redraw);
        assert_eq!(s.editing, 1);

        let mut locked = browsing(6, 90);
        locked.status = Status::Unsupported as u8;
        assert_eq!(on_button(&mut locked, Button::Select), Action::Ignore);
        assert_eq!(locked.editing, 0);
    }

    /// One commit, not one a press. Fifteen presses from 90 kg to 75 would be
    /// fifteen whole-file rewrites, fifteen rename pairs and fifteen windows in
    /// which power loss strands the file.
    #[test]
    fn stepping_commits_nothing_until_select() {
        let mut s = browsing(6, 90);
        s.value_min = 25;
        s.value_max = 250;
        on_button(&mut s, Button::Select);

        for _ in 0..15 {
            assert_eq!(on_button(&mut s, Button::Down), Action::Redraw);
        }
        assert_eq!(s.value, 75);
        assert_eq!(s.editing, 1);

        assert_eq!(on_button(&mut s, Button::Select), Action::Commit);
        assert_eq!(s.editing, 0);
        assert_eq!(s.value, 75);
    }

    #[test]
    fn stepping_clamps_rather_than_wrapping() {
        let mut s = browsing(7, 219);
        s.value_min = 90;
        s.value_max = 220;
        on_button(&mut s, Button::Select);

        assert_eq!(on_button(&mut s, Button::Up), Action::Redraw);
        assert_eq!(s.value, 220);
        assert_eq!(on_button(&mut s, Button::Up), Action::Ignore);
        assert_eq!(s.value, 220);
    }

    /// A step that would overshoot lands on the bound, so a 500-step goal can
    /// still reach its own minimum.
    #[test]
    fn a_step_that_would_overshoot_lands_on_the_bound() {
        let mut s = browsing(3, 700);
        s.value_min = 500;
        s.value_max = 50000;
        on_button(&mut s, Button::Select);
        assert_eq!(on_button(&mut s, Button::Down), Action::Redraw);
        assert_eq!(s.value, 500);
    }

    /// Back in the editor discards and re-reads rather than committing: the
    /// pending value was never written anywhere, and the truth is what the live
    /// struct says, not what this remembered.
    #[test]
    fn back_in_the_editor_discards_and_re_reads() {
        let mut s = browsing(6, 90);
        on_button(&mut s, Button::Select);
        on_button(&mut s, Button::Down);
        assert_eq!(s.value, 89);

        assert_eq!(on_button(&mut s, Button::Back), Action::ReadField);
        assert_eq!(s.editing, 0);
    }

    #[test]
    fn back_while_browsing_leaves_the_app() {
        let mut s = browsing(6, 90);
        assert_eq!(on_button(&mut s, Button::Back), Action::Exit);
    }

    /// The phone app can set the same field while this screen is open, and a
    /// step made against the old value would write a number the wearer never
    /// saw.
    #[test]
    fn an_external_change_abandons_a_pending_edit() {
        let mut s = browsing(6, 90);
        on_button(&mut s, Button::Select);
        on_button(&mut s, Button::Down);

        external_change(&mut s, 82);
        assert_eq!(s.editing, 0);
        assert_eq!(s.value, 82);
    }

    /// The cost of one step per press, stated rather than asserted. There is no
    /// auto-repeat on this hardware, so these are the real numbers a wrist pays.
    ///
    /// The worst of them is the one the design was challenged on: 90 kg to 75 is
    /// fifteen presses. It is also **one** commit -- one whole-file rewrite, one
    /// rename pair, one window in which power loss strands the file -- which is
    /// the part that costs a wearer anything. Falsified by a scheme that reaches
    /// the same values in fewer presses without adding a mode to track; Spin
    /// measured four such schemes over 200,000 simulated entries and every one
    /// that needed a mode paid the click it spent changing it.
    #[test]
    fn press_counts_for_the_edits_a_wearer_actually_makes() {
        let cases: [(usize, u32, u32, u32); 6] = [
            // field index, from, to, presses
            (2, 30, 60, 6),     // active minutes, +30 at a step of 5
            (3, 5000, 8000, 6), // step goal, +3000 at a step of 500
            (4, 10, 15, 5),     // floor goal, +5 at a step of 1
            (5, 190, 188, 2),   // height, -2 cm
            (6, 90, 75, 15),    // weight, -15 kg -- the worst of them
            (7, 184, 180, 4),   // max heart rate, -4 bpm
        ];

        for (index, from, to, expected) in cases {
            let mut s = browsing(index as u8, from);
            s.value_min = 0;
            s.value_max = 60000;
            on_button(&mut s, Button::Select);

            let direction = if to > from { Button::Up } else { Button::Down };
            let mut presses = 0;
            while s.value != to {
                assert_eq!(on_button(&mut s, direction), Action::Redraw);
                presses += 1;
                assert!(presses <= 1000, "{} never reached {to}", FIELDS[index].name);
            }
            assert_eq!(presses, expected, "{}", FIELDS[index].name);
        }
    }

    /// An unset field opens at its seed rather than at zero or at the bottom of
    /// its range, and the seed is shown before anything is written.
    #[test]
    fn an_unset_field_opens_at_its_seed() {
        let mut s = browsing(6, 0);
        s.status = Status::Unset as u8;
        s.value_min = 25;
        s.value_max = 250;

        assert_eq!(on_button(&mut s, Button::Select), Action::Redraw);
        assert_eq!(s.value, FIELDS[6].seed);
        assert_eq!(s.editing, 1);
    }

    /// A seed outside the field's own range would be a value the wearer could
    /// save and the splice would then have to refuse.
    #[test]
    fn every_seed_sits_inside_the_range_the_kit_declares() {
        let ranges: [(u32, u32); 8] = [
            (0, 1),
            (0, 1),
            (5, 240),
            (500, 50000),
            (1, 200),
            (100, 250),
            (25, 250),
            (90, 220),
        ];
        for (i, spec) in FIELDS.iter().enumerate() {
            let (min, max) = ranges[i];
            assert!(
                spec.seed >= min && spec.seed <= max,
                "{} seeds {} outside {min}..{max}",
                spec.name,
                spec.seed
            );
            assert!(spec.step > 0 && spec.step <= max - min + 1, "{}", spec.name);
        }
    }
}
