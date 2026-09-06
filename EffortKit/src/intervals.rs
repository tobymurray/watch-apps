//! An interval session, as one line of text a wearer typed on their phone.
//!
//! ```text
//! session  = item , { "," , item } ;                 (* at most 41 items *)
//! item     = block | step ;
//! block    = repeats , "x" , "(" , step , "," , step ,
//!            [ "," , step ] , [ "," , step ] , ")" ;
//! repeats  = digit , [ digit ] ;                     (* 1..99 *)
//! step     = duration , unit , [ "@" , effort ] ;
//! duration = digit , [ digit ] , [ digit ] ;         (* 1..999 *)
//! unit     = "s" | "m" ;
//! effort   = "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" ;
//! digit    = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;
//! ```
//!
//! PLATFORM: the regex the phone validates this text against never reaches the
//! watch, and the values file it writes is plaintext on a FAT volume readable
//! over USB and BLE. So [`Session::parse`] is handed arbitrary bytes and is
//! total over them: every input yields a session or names a reason it did not,
//! with no panic, no unbounded loop and no allocation. Falsified by a values
//! file the watch cannot be handed directly.
//!
//! Where this grammar and that regex deliberately disagree, and why, is in
//! `Spin/Docs/INTERVAL-DSL-EVALUATION.md`.
//!
//! `@n` IS AN INSTRUCTION AND NEVER A COMPLIANCE CHECK: nothing here or built
//! on it may compare it against a measured heart rate. Measured on one real
//! interval ride at a maximum of 184: in nine of eleven correctly ridden
//! efforts the recovery averaged a *higher* heart rate than the work it was
//! recovering from. Falsified by riding one that does not.
//!
//! MEASURED: the most steps a 128-byte value can name is **2871**, in 126 bytes
//! and 8 items — `99x(1s,1s,1s,1s)` six times, then `99x(1s,1s,1s)` and
//! `99x(1s,1s)`. That is why a session is walked by [`Cursor`] rather than
//! expanded: a flat array bounded by it costs 8.6 KB at three bytes a step
//! where the cursor costs three. Re-derive by searching item shapes against the
//! 128-byte cap.

/// Items a session may hold.
pub const MAX_ITEMS: usize = 41;

/// Steps one repeated block may hold.
pub const MAX_BLOCK: usize = 4;

/// The declared `maxLength` of the `intervals` field, in bytes.
pub const MAX_TEXT: usize = 128;

/// The value that means no session, and the field's declared default.
pub const OFF: &[u8] = b"0s";

/// One prescribed step: how long, and how hard it says to go.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct Step {
    /// Length of the step in seconds, 1..=59_940.
    pub seconds: u16,
    /// The `@n` the wearer wrote, 1..=8; 0 when they wrote none.
    pub effort: u8,
}

/// One item of a session: a step, or a block of steps repeated.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
struct Item {
    repeats: u8,
    len: u8,
    steps: [Step; MAX_BLOCK],
}

impl Item {
    const EMPTY: Self = Item {
        repeats: 0,
        len: 0,
        steps: [Step { seconds: 0, effort: 0 }; MAX_BLOCK],
    };
}

impl Default for Item {
    fn default() -> Self {
        Item::EMPTY
    }
}

/// Why a value named no session.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Error {
    /// No bytes at all.
    Empty,
    /// Longer than [`MAX_TEXT`].
    TooLong,
    /// More than [`MAX_ITEMS`] items.
    TooManyItems,
    /// A byte the grammar has no use for, including whitespace and upper case.
    Unexpected,
    /// A duration with no digits, or more than three of them.
    BadDuration,
    /// A step of no seconds, anywhere but as the whole value `0s`.
    ZeroStep,
    /// `0x(...)`, or a repeat count of more than two digits.
    BadRepeats,
    /// A block holding fewer than two or more than [`MAX_BLOCK`] steps.
    BadBlock,
    /// The value ended mid-item.
    Truncated,
}

/// A parsed session: the items as written, never expanded.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Session {
    items: [Item; MAX_ITEMS],
    len: u8,
}

impl Default for Session {
    fn default() -> Self {
        Session::EMPTY
    }
}

/// Where a step sits in the session, which a [`Step`] alone cannot say.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Placed {
    /// The step itself.
    pub step: Step,
    /// Which repetition of its block this is, 1-based; 0 outside a block.
    pub rep: u8,
    /// How many repetitions the block has; 0 outside a block.
    pub reps: u8,
}

impl Session {
    /// No session, usable in a `const`. What `0s` parses to.
    pub const EMPTY: Self = Session { items: [Item::EMPTY; MAX_ITEMS], len: 0 };

    /// Read a session out of arbitrary bytes.
    pub fn parse(text: &[u8]) -> Result<Session, Error> {
        if text.is_empty() {
            return Err(Error::Empty);
        }
        if text.len() > MAX_TEXT {
            return Err(Error::TooLong);
        }
        if text == OFF {
            return Ok(Session::EMPTY);
        }

        let mut out = Session::EMPTY;
        let mut at = 0usize;
        loop {
            if out.len as usize == MAX_ITEMS {
                return Err(Error::TooManyItems);
            }
            let (item, next) = parse_item(text, at)?;
            out.items[out.len as usize] = item;
            out.len += 1;
            at = next;
            match text.get(at) {
                None => return Ok(out),
                Some(b',') => at += 1,
                Some(_) => return Err(Error::Unexpected),
            }
        }
    }

    /// True when there are no steps, which is what [`OFF`] parses to.
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Items as written, blocks counting as one.
    pub fn item_count(&self) -> usize {
        self.len as usize
    }

    /// Steps the session runs through, blocks counted out.
    pub fn step_count(&self) -> u16 {
        let mut n = 0u16;
        for item in &self.items[..self.len as usize] {
            n = n.saturating_add(item.repeats as u16 * item.len as u16);
        }
        n
    }

    /// Seconds the whole session prescribes, saturating.
    pub fn total_seconds(&self) -> u32 {
        let mut total = 0u32;
        for item in &self.items[..self.len as usize] {
            let mut block = 0u32;
            for step in &item.steps[..item.len as usize] {
                block += step.seconds as u32;
            }
            total = total.saturating_add(block * item.repeats as u32);
        }
        total
    }

    /// A cursor on the first step, or `None` for a session with no steps.
    pub fn start(&self) -> Option<Cursor> {
        if self.is_empty() {
            None
        } else {
            Some(Cursor { item: 0, rep: 0, step: 0 })
        }
    }

    /// Write the session back out as text this grammar accepts, canonically
    /// rather than byte-identically: `120s` comes back as `2m` and `007s` as
    /// `7s`.
    ///
    /// Returns the bytes written, or `None` when `out` is too small;
    /// [`MAX_TEXT`] bytes is always enough, because a session came from at most
    /// that many.
    pub fn render(&self, out: &mut [u8]) -> Option<usize> {
        let mut w = Writer { out, at: 0 };
        if self.is_empty() {
            w.bytes(OFF);
            return w.finish();
        }
        for (i, item) in self.items[..self.len as usize].iter().enumerate() {
            if i > 0 {
                w.byte(b',');
            }
            if item.repeats == 1 && item.len == 1 {
                w.step(&item.steps[0]);
                continue;
            }
            w.number(item.repeats as u16);
            w.bytes(b"x(");
            for (j, step) in item.steps[..item.len as usize].iter().enumerate() {
                if j > 0 {
                    w.byte(b',');
                }
                w.step(step);
            }
            w.byte(b')');
        }
        w.finish()
    }
}

/// A position in a session: which item, which repetition of it, which step of
/// that repetition — three bytes whatever the repeat counts, which is why the
/// module's measurement does not need an array of steps.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Cursor {
    item: u8,
    rep: u8,
    step: u8,
}

impl Cursor {
    /// The step the cursor is on, with its place in its block; `None` once the
    /// session has run out.
    pub fn current(&self, session: &Session) -> Option<Placed> {
        let item = session.items.get(self.item as usize)?;
        if self.item >= session.len || self.step >= item.len {
            return None;
        }
        Some(Placed {
            step: item.steps[self.step as usize],
            rep: if item.repeats > 1 { self.rep + 1 } else { 0 },
            reps: if item.repeats > 1 { item.repeats } else { 0 },
        })
    }

    /// Move to the next step, returning false once there is none.
    pub fn advance(&mut self, session: &Session) -> bool {
        let Some(item) = session.items.get(self.item as usize) else {
            return false;
        };
        if self.item >= session.len {
            return false;
        }
        self.step += 1;
        if self.step < item.len {
            return true;
        }
        self.step = 0;
        self.rep += 1;
        if self.rep < item.repeats {
            return true;
        }
        self.rep = 0;
        self.item += 1;
        self.item < session.len
    }
}

// -- The grammar, one function a production ----------------------------------

fn parse_item(text: &[u8], at: usize) -> Result<(Item, usize), Error> {
    if let Some(x) = find_block_x(text, at) {
        return parse_block(text, at, x);
    }
    let (step, next) = parse_step(text, at)?;
    if step.seconds == 0 {
        return Err(Error::ZeroStep);
    }
    let mut item = Item { repeats: 1, len: 1, steps: [Step::default(); MAX_BLOCK] };
    item.steps[0] = step;
    Ok((item, next))
}

/// The offset of a block's `x`: an item starts with digits either way, so only
/// the byte after them says which production this is.
fn find_block_x(text: &[u8], at: usize) -> Option<usize> {
    let mut i = at;
    while i < text.len() && text[i].is_ascii_digit() {
        i += 1;
    }
    if i > at && text.get(i) == Some(&b'x') {
        Some(i)
    } else {
        None
    }
}

fn parse_block(text: &[u8], at: usize, x: usize) -> Result<(Item, usize), Error> {
    let digits = x - at;
    if digits > 2 {
        return Err(Error::BadRepeats);
    }
    let repeats = to_number(&text[at..x]);
    if repeats == 0 {
        return Err(Error::BadRepeats);
    }

    if text.get(x + 1) != Some(&b'(') {
        return Err(Error::Unexpected);
    }
    let mut item = Item { repeats: repeats as u8, len: 0, steps: [Step::default(); MAX_BLOCK] };
    let mut i = x + 2;
    loop {
        if item.len as usize == MAX_BLOCK {
            return Err(Error::BadBlock);
        }
        let (step, next) = parse_step(text, i)?;
        if step.seconds == 0 {
            return Err(Error::ZeroStep);
        }
        item.steps[item.len as usize] = step;
        item.len += 1;
        i = next;
        match text.get(i) {
            Some(b',') => i += 1,
            Some(b')') => {
                if item.len < 2 {
                    return Err(Error::BadBlock);
                }
                return Ok((item, i + 1));
            }
            Some(_) => return Err(Error::Unexpected),
            None => return Err(Error::Truncated),
        }
    }
}

fn parse_step(text: &[u8], at: usize) -> Result<(Step, usize), Error> {
    let mut i = at;
    while i < text.len() && text[i].is_ascii_digit() {
        i += 1;
    }
    let digits = i - at;
    if digits == 0 || digits > 3 {
        return Err(Error::BadDuration);
    }
    let value = to_number(&text[at..i]);

    let seconds = match text.get(i) {
        Some(b's') => value,
        Some(b'm') => value * 60,
        Some(_) => return Err(Error::Unexpected),
        None => return Err(Error::Truncated),
    };
    i += 1;

    let mut effort = 0u8;
    if text.get(i) == Some(&b'@') {
        match text.get(i + 1) {
            Some(d) if (b'1'..=b'8').contains(d) => effort = d - b'0',
            Some(_) => return Err(Error::Unexpected),
            None => return Err(Error::Truncated),
        }
        i += 2;
    }

    Ok((Step { seconds: seconds as u16, effort }, i))
}

/// At most three digits, so the widest value is 999 and nothing overflows.
fn to_number(digits: &[u8]) -> u32 {
    let mut n = 0u32;
    for d in digits {
        n = n * 10 + (d - b'0') as u32;
    }
    n
}

// -- Rendering ----------------------------------------------------------------

struct Writer<'a> {
    out: &'a mut [u8],
    at: usize,
}

impl Writer<'_> {
    fn byte(&mut self, b: u8) {
        if self.at < self.out.len() {
            self.out[self.at] = b;
        }
        self.at += 1;
    }

    fn bytes(&mut self, bs: &[u8]) {
        for b in bs {
            self.byte(*b);
        }
    }

    fn number(&mut self, mut n: u16) {
        let mut digits = [0u8; 5];
        let mut len = 0;
        loop {
            digits[len] = b'0' + (n % 10) as u8;
            len += 1;
            n /= 10;
            if n == 0 {
                break;
            }
        }
        while len > 0 {
            len -= 1;
            self.byte(digits[len]);
        }
    }

    /// Minutes when the length divides by 60, seconds otherwise — which is
    /// always expressible, because a duration that does not divide by 60 came
    /// from an `s` and is therefore at most 999.
    fn step(&mut self, step: &Step) {
        if step.seconds % 60 == 0 {
            self.number(step.seconds / 60);
            self.byte(b'm');
        } else {
            self.number(step.seconds);
            self.byte(b's');
        }
        if step.effort > 0 {
            self.byte(b'@');
            self.byte(b'0' + step.effort);
        }
    }

    fn finish(self) -> Option<usize> {
        if self.at <= self.out.len() {
            Some(self.at)
        } else {
            None
        }
    }
}
