//! Just enough JSON to read and write the shared log, in `no_std` with no
//! allocator.
//!
//! Deliberately not a general parser. It walks values to find their extent so
//! that a brace inside a string cannot confuse it, and it reads the handful of
//! shapes the schema in `history.rs` uses: an object's members, an array's
//! items, and unsigned integers. Anything else it declines to understand, which
//! makes an unexpected file a refusal rather than a wrong number.

/// The end of the JSON value starting at `at`, exclusive; `None` if it is
/// malformed or runs off the end.
pub fn value_end(b: &[u8], at: usize) -> Option<usize> {
    let mut i = at;
    if i >= b.len() {
        return None;
    }
    match b[i] {
        b'"' => string_end(b, i),
        b'{' | b'[' => {
            let mut depth = 0usize;
            while i < b.len() {
                match b[i] {
                    b'"' => i = string_end(b, i)?,
                    b'{' | b'[' => {
                        depth += 1;
                        i += 1;
                    }
                    b'}' | b']' => {
                        depth -= 1;
                        i += 1;
                        if depth == 0 {
                            return Some(i);
                        }
                    }
                    _ => i += 1,
                }
            }
            None
        }
        _ => {
            // A bare token: number, true, false or null. It ends where the
            // enclosing object or array says it does.
            while i < b.len() && !matches!(b[i], b',' | b'}' | b']' | b' ' | b'\t' | b'\n' | b'\r')
            {
                i += 1;
            }
            if i == at {
                None
            } else {
                Some(i)
            }
        }
    }
}

fn string_end(b: &[u8], at: usize) -> Option<usize> {
    let mut i = at + 1;
    while i < b.len() {
        match b[i] {
            b'\\' => i += 2,
            b'"' => return Some(i + 1),
            _ => i += 1,
        }
    }
    None
}

fn skip_ws(b: &[u8], mut i: usize) -> usize {
    while i < b.len() && matches!(b[i], b' ' | b'\t' | b'\n' | b'\r') {
        i += 1;
    }
    i
}

/// The value of `key` in the object whose text is `obj`, at its top level only.
pub fn member<'a>(obj: &'a [u8], key: &str) -> Option<&'a [u8]> {
    let mut i = skip_ws(obj, 0);
    if i >= obj.len() || obj[i] != b'{' {
        return None;
    }
    i += 1;
    loop {
        i = skip_ws(obj, i);
        if i >= obj.len() || obj[i] == b'}' {
            return None;
        }
        if obj[i] != b'"' {
            return None;
        }
        let key_start = i + 1;
        let key_end = string_end(obj, i)? - 1;
        i = skip_ws(obj, key_end + 1);
        if i >= obj.len() || obj[i] != b':' {
            return None;
        }
        i = skip_ws(obj, i + 1);
        let val_start = i;
        let val_end = value_end(obj, i)?;
        if &obj[key_start..key_end] == key.as_bytes() {
            return Some(&obj[val_start..val_end]);
        }
        i = skip_ws(obj, val_end);
        if i < obj.len() && obj[i] == b',' {
            i += 1;
        } else {
            return None;
        }
    }
}

/// Walks the items of a JSON array's text.
pub struct Items<'a> {
    b: &'a [u8],
    i: usize,
    done: bool,
}

/// Items of the array whose text is `arr`; empty for anything that is not one.
pub fn items(arr: &[u8]) -> Items<'_> {
    let i = skip_ws(arr, 0);
    if i < arr.len() && arr[i] == b'[' {
        Items {
            b: arr,
            i: i + 1,
            done: false,
        }
    } else {
        Items {
            b: arr,
            i: 0,
            done: true,
        }
    }
}

impl<'a> Iterator for Items<'a> {
    type Item = &'a [u8];

    fn next(&mut self) -> Option<&'a [u8]> {
        if self.done {
            return None;
        }
        let start = skip_ws(self.b, self.i);
        if start >= self.b.len() || self.b[start] == b']' {
            self.done = true;
            return None;
        }
        let end = match value_end(self.b, start) {
            Some(e) => e,
            None => {
                self.done = true;
                return None;
            }
        };
        let next = skip_ws(self.b, end);
        if next < self.b.len() && self.b[next] == b',' {
            self.i = next + 1;
        } else {
            // A closing bracket, or something that is not one: either way this
            // was the last item worth handing back.
            self.done = true;
        }
        Some(&self.b[start..end])
    }
}

/// A non-negative integer, or `None` for anything else including a negative or
/// a fraction.
pub fn as_u64(v: &[u8]) -> Option<u64> {
    if v.is_empty() {
        return None;
    }
    let mut out: u64 = 0;
    for &c in v {
        if !c.is_ascii_digit() {
            return None;
        }
        out = out.checked_mul(10)?.checked_add((c - b'0') as u64)?;
    }
    Some(out)
}

/// `as_u64` clamped into a `u32`, or 0.
pub fn as_u32_or(v: Option<&[u8]>, fallback: u32) -> u32 {
    match v.and_then(as_u64) {
        Some(n) => n.min(u32::MAX as u64) as u32,
        None => fallback,
    }
}

/// `as_u64` clamped into a `u16`, or 0.
pub fn as_u16_or(v: Option<&[u8]>, fallback: u16) -> u16 {
    match v.and_then(as_u64) {
        Some(n) => n.min(u16::MAX as u64) as u16,
        None => fallback,
    }
}

/// `as_u64` clamped into a `u8`, or 0.
pub fn as_u8_or(v: Option<&[u8]>, fallback: u8) -> u8 {
    match v.and_then(as_u64) {
        Some(n) => n.min(u8::MAX as u64) as u8,
        None => fallback,
    }
}

/// Appends JSON to a caller's buffer, refusing to write past its end.
///
/// The overflow flag is sticky, so a caller checks once at the end rather than
/// after every field; a truncated file is never handed on as a complete one.
pub struct Writer<'a> {
    buf: &'a mut [u8],
    pos: usize,
    overflow: bool,
}

impl<'a> Writer<'a> {
    pub fn new(buf: &'a mut [u8]) -> Self {
        Writer {
            buf,
            pos: 0,
            overflow: false,
        }
    }

    pub fn overflowed(&self) -> bool {
        self.overflow
    }

    pub fn len(&self) -> usize {
        self.pos
    }

    pub fn is_empty(&self) -> bool {
        self.pos == 0
    }

    pub fn raw(&mut self, s: &str) -> &mut Self {
        for &c in s.as_bytes() {
            self.byte(c);
        }
        self
    }

    pub fn byte(&mut self, c: u8) -> &mut Self {
        if self.pos < self.buf.len() {
            self.buf[self.pos] = c;
            self.pos += 1;
        } else {
            self.overflow = true;
        }
        self
    }

    pub fn u64(&mut self, mut v: u64) -> &mut Self {
        let mut digits = [0u8; 20];
        let mut n = 0;
        loop {
            digits[n] = b'0' + (v % 10) as u8;
            n += 1;
            v /= 10;
            if v == 0 {
                break;
            }
        }
        while n > 0 {
            n -= 1;
            self.byte(digits[n]);
        }
        self
    }

    /// `"key":` -- the caller writes the value.
    pub fn key(&mut self, k: &str) -> &mut Self {
        self.byte(b'"').raw(k).raw("\":")
    }

    /// `"key":<number>`.
    pub fn num(&mut self, k: &str, v: u64) -> &mut Self {
        self.key(k).u64(v)
    }

    /// `"key":"value"` -- the value must contain no character JSON escapes.
    pub fn text(&mut self, k: &str, v: &[u8]) -> &mut Self {
        self.key(k).byte(b'"');
        for &c in v {
            self.byte(c);
        }
        self.byte(b'"')
    }
}
