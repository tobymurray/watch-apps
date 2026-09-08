//! Navigation for four buttons, a screen stack, and a clock for animation.
//!
//! # Why focus and not a pointer
//!
//! Every immediate-mode toolkit worth borrowing from — egui, and
//! `kolibri-embedded-gui` after it — takes a pointer position and hit-tests
//! widgets against it. There is no pointer here and there never will be: the
//! devices this crate is for have four buttons at the bezel corners, and in
//! every app it was distilled from two of those four were already spoken for as
//! Back and Select. So the navigable state is a **focus index**, the two
//! remaining buttons move it, and a widget reports what happened to it rather
//! than being asked whether a point is inside it.
//!
//! # Why a stack and not a screen number
//!
//! Six of the seven renderers dispatched on a `screen: u8` held in a flat
//! struct the C++ half owned. That cannot express *where you came from*, so
//! list → item → confirm → back → back has nowhere to record the list position
//! to return to. A stack can, it costs one array, and it is the difference
//! between a kit that reaches a notification list and one that has to be
//! rewritten for the first person who wants one.

/// A press, as the four buttons of a round bezel deliver it.
///
/// Named for position rather than for meaning, because which corner is Back is
/// a product decision and not this crate's.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Hash)]
#[repr(u8)]
pub enum Button {
    /// Top left.
    L1 = 0,
    /// Top right.
    R1 = 1,
    /// Bottom left.
    L2 = 2,
    /// Bottom right.
    R2 = 3,
}

impl Button {
    /// From the wire value, refusing anything else rather than guessing.
    pub const fn from_u8(v: u8) -> Option<Button> {
        match v {
            0 => Some(Button::L1),
            1 => Some(Button::R1),
            2 => Some(Button::L2),
            3 => Some(Button::R2),
            _ => None,
        }
    }
}

/// What happened to a press.
///
/// A hold is deliberately absent from the vocabulary a widget can require: on
/// the hardware this was distilled from, enabling the system's music control
/// makes it swallow `HOLD_1S` before the app sees it, so a design that needs a
/// hold stops working when an unrelated capability is switched on.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Hash)]
pub enum Press {
    /// Pressed and released.
    Click,
    /// Held past the platform's threshold, where the platform delivers it.
    Hold,
}

/// Which way a press moves the focus ring.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Role {
    /// Move focus to the previous item.
    Prev,
    /// Move focus to the next item.
    Next,
    /// Act on the focused item.
    Select,
    /// Leave this screen.
    Back,
    /// This button means nothing here.
    None,
}

/// Which corner does what.
///
/// The default is the arrangement every app in this crate's home repository
/// converged on independently: the right-hand buttons act and leave, the
/// left-hand ones move.
#[derive(Clone, Copy, Debug)]
pub struct Bindings {
    /// What the top-left button does.
    pub l1: Role,
    /// What the top-right button does.
    pub r1: Role,
    /// What the bottom-left button does.
    pub l2: Role,
    /// What the bottom-right button does.
    pub r2: Role,
}

impl Default for Bindings {
    fn default() -> Self {
        Bindings { l1: Role::Prev, r1: Role::Select, l2: Role::Next, r2: Role::Back }
    }
}

impl Bindings {
    /// What this press means.
    pub const fn role(&self, b: Button) -> Role {
        match b {
            Button::L1 => self.l1,
            Button::R1 => self.r1,
            Button::L2 => self.l2,
            Button::R2 => self.r2,
        }
    }
}

/// A focus ring over `len` items.
///
/// Wraps, because with two buttons and no pointer a wearer who overshoots the
/// last item should not have to press back through the whole list.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct Focus {
    index: usize,
}

impl Focus {
    /// A ring focused on the first item.
    pub const fn new() -> Self {
        Focus { index: 0 }
    }

    /// Which item has focus.
    pub const fn index(&self) -> usize {
        self.index
    }

    /// Clamps rather than wraps, for a caller whose list shrank underneath it.
    pub fn clamp(&mut self, len: usize) {
        if len == 0 {
            self.index = 0;
        } else if self.index >= len {
            self.index = len - 1;
        }
    }

    /// Moves focus on by one, wrapping at the end.
    pub fn next(&mut self, len: usize) {
        if len > 0 {
            self.index = (self.index + 1) % len;
        }
    }

    /// Moves focus back by one, wrapping at the start.
    pub fn prev(&mut self, len: usize) {
        if len > 0 {
            self.index = if self.index == 0 { len - 1 } else { self.index - 1 };
        }
    }

    /// Applies a press through `bindings`, returning the role it played so the
    /// caller can act on `Select` and `Back` in the same expression.
    pub fn apply(&mut self, bindings: &Bindings, button: Button, len: usize) -> Role {
        let role = bindings.role(button);
        match role {
            Role::Next => self.next(len),
            Role::Prev => self.prev(len),
            _ => {}
        }
        role
    }
}

/// A screen stack with real back behaviour.
///
/// `S` is the caller's own screen enum; the stack holds it and the focus that
/// screen had, so returning to a list lands on the item that was selected
/// rather than at the top.
#[derive(Clone, Debug)]
pub struct Stack<S, const N: usize> {
    entries: [Option<(S, Focus)>; N],
    depth: usize,
}

impl<S: Copy, const N: usize> Stack<S, N> {
    /// A stack showing `root`, which can never be popped.
    pub fn new(root: S) -> Self {
        let mut entries = [None; N];
        entries[0] = Some((root, Focus::new()));
        Stack { entries, depth: 1 }
    }

    /// The screen on top.
    pub fn top(&self) -> S {
        self.entries[self.depth - 1].expect("the root is never popped").0
    }

    /// The focus of the screen on top.
    pub fn focus(&self) -> Focus {
        self.entries[self.depth - 1].expect("the root is never popped").1
    }

    /// The focus of the screen on top, to move.
    pub fn focus_mut(&mut self) -> &mut Focus {
        &mut self.entries[self.depth - 1].as_mut().expect("the root is never popped").1
    }

    /// How many screens deep, root included.
    pub const fn depth(&self) -> usize {
        self.depth
    }

    /// Pushes a screen. Returns `false` when the stack is full, rather than
    /// dropping the bottom: a navigation that silently forgets where it came
    /// from is worse than one that refuses to go deeper.
    pub fn push(&mut self, screen: S) -> bool {
        if self.depth >= N {
            return false;
        }
        self.entries[self.depth] = Some((screen, Focus::new()));
        self.depth += 1;
        true
    }

    /// Pops back one screen, restoring the focus that screen had. Returns
    /// `false` at the root, which is a caller's cue to leave the app.
    pub fn pop(&mut self) -> bool {
        if self.depth <= 1 {
            return false;
        }
        self.entries[self.depth - 1] = None;
        self.depth -= 1;
        true
    }
}

/// A monotonic millisecond clock for animation.
///
/// # What the clock is, and what suspend does to it
///
/// The tick that drives a GUI stops arriving while the app is suspended, so an
/// animation driven by counting ticks freezes and then resumes mid-stride,
/// while one driven by wall-clock time jumps to where it should be. This holds
/// the host's own millisecond count and therefore does the second — and
/// [`Anim::resumed`] exists for the case where a caller wants the first,
/// because an animation the wearer never saw start should not be shown
/// half-finished.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Anim {
    start_ms: u32,
    duration_ms: u32,
}

impl Anim {
    /// An animation starting now and running for `duration_ms`.
    pub const fn starting(now_ms: u32, duration_ms: u32) -> Self {
        Anim { start_ms: now_ms, duration_ms }
    }

    /// Progress from 0 to 255. Saturates at the end rather than wrapping, and
    /// a zero-length animation is instantly complete.
    pub const fn progress(&self, now_ms: u32) -> u8 {
        if self.duration_ms == 0 {
            return 255;
        }
        let elapsed = now_ms.wrapping_sub(self.start_ms);
        if elapsed >= self.duration_ms {
            return 255;
        }
        ((elapsed as u64 * 255) / self.duration_ms as u64) as u8
    }

    /// Whether the animation has reached its end.
    pub const fn is_done(&self, now_ms: u32) -> bool {
        self.progress(now_ms) == 255
    }

    /// Restarts from `now_ms`, for a caller that suspended and came back and
    /// would rather replay than resume mid-stride.
    pub const fn resumed(&self, now_ms: u32) -> Self {
        Anim { start_ms: now_ms, duration_ms: self.duration_ms }
    }
}

/// Ease-in-out over 0..=255, in integers.
///
/// `matrix-gui` chose integer easing deliberately for targets with no FPU.
/// This target has hard single-precision float and would not need to, but the
/// integer form is exact, identical on host and device, and therefore golden-
/// testable — which the float form is not.
pub const fn ease_in_out(t: u8) -> u8 {
    let t = t as u32;
    // 3t² − 2t³ scaled to 0..=255, in one division so truncation cannot make it
    // go backwards: dividing at each step loses enough to un-order it, which
    // `easing_is_monotonic_and_hits_both_ends` catches. The largest
    // intermediate is 3 · 255 · 255² = 49,744,125, well inside u32.
    let v = (3 * 255 * t * t - 2 * t * t * t) / (255 * 255);
    if v > 255 {
        255
    } else {
        v as u8
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Clone, Copy, PartialEq, Eq, Debug)]
    enum Screen {
        List,
        Item,
        Confirm,
    }

    #[test]
    fn focus_wraps_both_ways() {
        let mut f = Focus::new();
        f.prev(3);
        assert_eq!(f.index(), 2);
        f.next(3);
        assert_eq!(f.index(), 0);
    }

    #[test]
    fn focus_on_an_empty_list_stays_put() {
        let mut f = Focus::new();
        f.next(0);
        f.prev(0);
        assert_eq!(f.index(), 0);
    }

    #[test]
    fn focus_clamps_when_the_list_shrinks() {
        let mut f = Focus::new();
        f.next(10);
        f.next(10);
        assert_eq!(f.index(), 2);
        f.clamp(2);
        assert_eq!(f.index(), 1);
        f.clamp(0);
        assert_eq!(f.index(), 0);
    }

    /// §4's item 3, as a test: list → item → confirm → back → back lands where
    /// it started, with the list's position intact.
    #[test]
    fn back_returns_to_where_you_were() {
        let mut stack: Stack<Screen, 4> = Stack::new(Screen::List);
        stack.focus_mut().next(50);
        stack.focus_mut().next(50);
        assert_eq!(stack.focus().index(), 2);

        assert!(stack.push(Screen::Item));
        assert_eq!(stack.focus().index(), 0, "a new screen starts at the top");
        assert!(stack.push(Screen::Confirm));
        assert_eq!(stack.depth(), 3);

        assert!(stack.pop());
        assert_eq!(stack.top(), Screen::Item);
        assert!(stack.pop());
        assert_eq!(stack.top(), Screen::List);
        assert_eq!(stack.focus().index(), 2, "the list position came back");
    }

    #[test]
    fn the_root_cannot_be_popped() {
        let mut stack: Stack<Screen, 4> = Stack::new(Screen::List);
        assert!(!stack.pop());
        assert_eq!(stack.top(), Screen::List);
    }

    #[test]
    fn a_full_stack_refuses_rather_than_forgetting_the_bottom() {
        let mut stack: Stack<Screen, 2> = Stack::new(Screen::List);
        assert!(stack.push(Screen::Item));
        assert!(!stack.push(Screen::Confirm));
        assert_eq!(stack.top(), Screen::Item);
        assert!(stack.pop());
        assert_eq!(stack.top(), Screen::List);
    }

    #[test]
    fn the_default_bindings_move_on_the_left_and_act_on_the_right() {
        let b = Bindings::default();
        assert_eq!(b.role(Button::L1), Role::Prev);
        assert_eq!(b.role(Button::L2), Role::Next);
        assert_eq!(b.role(Button::R1), Role::Select);
        assert_eq!(b.role(Button::R2), Role::Back);
    }

    #[test]
    fn apply_moves_the_focus_and_reports_the_role() {
        let mut f = Focus::new();
        let b = Bindings::default();
        assert_eq!(f.apply(&b, Button::L2, 4), Role::Next);
        assert_eq!(f.index(), 1);
        assert_eq!(f.apply(&b, Button::R1, 4), Role::Select);
        assert_eq!(f.index(), 1, "select does not move the ring");
    }

    #[test]
    fn a_button_outside_the_four_is_refused() {
        assert_eq!(Button::from_u8(0), Some(Button::L1));
        assert_eq!(Button::from_u8(3), Some(Button::R2));
        assert_eq!(Button::from_u8(4), None);
        assert_eq!(Button::from_u8(255), None);
    }

    #[test]
    fn progress_runs_from_zero_to_full_and_saturates() {
        let a = Anim::starting(1_000, 500);
        assert_eq!(a.progress(1_000), 0);
        assert_eq!(a.progress(1_250), 127);
        assert_eq!(a.progress(1_500), 255);
        assert_eq!(a.progress(9_999), 255);
        assert!(a.is_done(1_500));
    }

    #[test]
    fn a_zero_length_animation_is_already_done() {
        assert!(Anim::starting(0, 0).is_done(0));
    }

    /// The clock is milliseconds since the host started, so it wraps after
    /// about 49 days. An animation running across the wrap must not jump.
    #[test]
    fn progress_survives_the_clock_wrapping() {
        let a = Anim::starting(u32::MAX - 100, 200);
        assert_eq!(a.progress(u32::MAX - 100), 0);
        assert_eq!(a.progress(u32::MAX), 127);
        assert_eq!(a.progress(99), 255);
    }

    #[test]
    fn easing_is_monotonic_and_hits_both_ends() {
        assert_eq!(ease_in_out(0), 0);
        assert_eq!(ease_in_out(255), 255);
        let mut last = 0;
        for t in 0..=255u8 {
            let v = ease_in_out(t);
            assert!(v >= last, "eased backwards at {t}: {last} -> {v}");
            last = v;
        }
    }
}
