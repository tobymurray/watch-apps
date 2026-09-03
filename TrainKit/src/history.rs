//! The record that outlives the ride: a bounded, versioned session log in
//! `../SharedData/`, for whatever wants to read a series later.
//!
//! WHY IT IS BOUNDED. This is flash with no allocator, and a log that appends
//! forever eventually fails to parse, fails to write, or fills the card. Two
//! caps hold it: `MAX_SESSIONS` entries, and `MAX_STORE_BYTES` of text. The
//! byte cap is the real one -- `save` drops the oldest entry and re-serialises
//! until the result fits -- so the newest session always lands and it is the
//! oldest that goes. `"dropped"` in the file counts everything ever evicted, so
//! a reader can tell a truncated series from a complete one.
//!
//! WHAT A READER DOES WITH A VERSION IT DOES NOT KNOW. `load` reports
//! `Load::Newer` for a `"version"` above `SCHEMA_VERSION` and reads nothing:
//! a writer that knows more than this one does must not be clobbered by it, and
//! the ride's own `.fit` is unaffected either way. A file that is not JSON, or
//! carries no version, reports `Load::Unreadable` so the caller can keep it as
//! evidence and start a fresh one.
//!
//! WHAT IS NOT IN HERE. Nothing that belongs in the `.fit`. The `.fit` is the
//! record of one ride; this is the record of the series, and the two do not
//! duplicate each other.

use crate::json::{self, Writer};
use crate::load::edwards_trimp;
use crate::record::*;

/// The schema this code writes and is willing to read.
pub const SCHEMA_VERSION: u64 = 1;

/// Entries kept: four or five weeks at four rides a week.
///
/// MEASURED, by `the_widest_possible_log_fits_the_buffer`: 20 sessions with
/// every field at its widest serialise to 14,220 bytes, so the entry cap is
/// always reachable inside the byte cap with room to spare. 24 came to 16,084
/// of the 16,384 available, which is a margin too thin to add a field to.
pub const MAX_SESSIONS: usize = 20;

/// The largest file this will write or read.
///
/// The same cap the SDK's own shared store uses (`StrideLut::kMaxStoreBytes`),
/// and for the same reason: the read is buffered, so the bound is what keeps a
/// corrupt or foreign file from asking for a transient allocation this device
/// does not have.
pub const MAX_STORE_BYTES: usize = 16 * 1024;

/// Longest app or sport name the header carries.
pub const MAX_NAME_LEN: usize = 24;

/// What `load` made of the bytes it was given.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Load {
    /// Parsed, or there was nothing there to parse.
    Ok,
    /// Not this schema, and not to be overwritten.
    Newer,
    /// Not JSON this understands.
    Unreadable,
}

/// The log in memory: what was read, plus whatever is being added.
pub struct History {
    sessions: [Session; MAX_SESSIONS],
    len: usize,
    dropped: u32,
    app: [u8; MAX_NAME_LEN],
    app_len: usize,
    sport: [u8; MAX_NAME_LEN],
    sport_len: usize,
}

impl History {
    pub fn new() -> Self {
        History {
            sessions: [Session::default(); MAX_SESSIONS],
            len: 0,
            dropped: 0,
            app: [0; MAX_NAME_LEN],
            app_len: 0,
            sport: [0; MAX_NAME_LEN],
            sport_len: 0,
        }
    }

    /// Name this log's writer.
    ///
    /// Bytes rather than `&str`, so nothing on this path needs `from_utf8`:
    /// only `[A-Za-z0-9_-]` survives `copy_name`, which is both valid UTF-8 and
    /// free of JSON escapes by construction.
    pub fn name(&mut self, app: &[u8], sport: &[u8]) {
        self.app_len = copy_name(&mut self.app, app);
        self.sport_len = copy_name(&mut self.sport, sport);
    }

    pub fn sessions(&self) -> &[Session] {
        &self.sessions[..self.len]
    }

    pub fn dropped(&self) -> u32 {
        self.dropped
    }

    /// Read a store file. Clears whatever was held first, so a failed load
    /// leaves an empty log rather than a half-read one.
    pub fn load(&mut self, buf: &[u8]) -> Load {
        let app_len = self.app_len;
        let sport_len = self.sport_len;
        let app = self.app;
        let sport = self.sport;
        *self = History::new();
        self.app = app;
        self.app_len = app_len;
        self.sport = sport;
        self.sport_len = sport_len;

        if buf.is_empty() {
            return Load::Ok;
        }
        if buf.len() > MAX_STORE_BYTES {
            return Load::Unreadable;
        }

        let version = match json::member(buf, "version").and_then(json::as_u64) {
            Some(v) => v,
            None => return Load::Unreadable,
        };
        if version > SCHEMA_VERSION {
            return Load::Newer;
        }

        let list = match json::member(buf, "sessions") {
            Some(l) => l,
            None => return Load::Unreadable,
        };
        self.dropped = json::as_u32_or(json::member(buf, "dropped"), 0);

        for item in json::items(list) {
            let s = parse_session(item);
            self.push(s);
        }
        Load::Ok
    }

    /// Add a session, replacing any entry that already claims the same start.
    ///
    /// A re-recorded ride is the same ride: `start_utc` is the entry's
    /// identity, so a retry cannot make one ride into two.
    pub fn add(&mut self, s: &Session) {
        if let Some(i) = self.sessions[..self.len]
            .iter()
            .position(|e| e.start_utc == s.start_utc && s.start_utc != 0)
        {
            self.sessions[i] = *s;
            return;
        }
        self.push(*s);
    }

    /// Serialise into `buf`, dropping the oldest entries until it fits.
    ///
    /// @return the bytes written, or `None` when even one session will not fit,
    ///         which leaves the caller to write nothing at all.
    pub fn save(&mut self, buf: &mut [u8]) -> Option<usize> {
        loop {
            if let Some(n) = self.try_save(buf) {
                return Some(n);
            }
            if self.len == 0 {
                return None;
            }
            self.evict_oldest();
        }
    }

    fn try_save(&self, buf: &mut [u8]) -> Option<usize> {
        let cap = buf.len().min(MAX_STORE_BYTES);
        let mut w = Writer::new(&mut buf[..cap]);
        w.byte(b'{');
        w.num("version", SCHEMA_VERSION);
        w.byte(b',');
        w.text("app", &self.app[..self.app_len]);
        w.byte(b',');
        w.text("sport", &self.sport[..self.sport_len]);
        w.byte(b',');
        w.num("kept", self.len as u64);
        w.byte(b',');
        w.num("dropped", self.dropped as u64);
        w.byte(b',');
        w.key("sessions").byte(b'[');
        for (i, s) in self.sessions[..self.len].iter().enumerate() {
            if i > 0 {
                w.byte(b',');
            }
            write_session(&mut w, s);
        }
        w.raw("]}");
        if w.overflowed() {
            None
        } else {
            Some(w.len())
        }
    }

    fn push(&mut self, s: Session) {
        if self.len == MAX_SESSIONS {
            self.evict_oldest();
        }
        self.sessions[self.len] = s;
        self.len += 1;
    }

    fn evict_oldest(&mut self) {
        if self.len == 0 {
            return;
        }
        self.sessions.copy_within(1..self.len, 0);
        self.len -= 1;
        self.dropped = self.dropped.saturating_add(1);
    }
}

impl Default for History {
    fn default() -> Self {
        History::new()
    }
}

// -- The schema ---------------------------------------------------------------

fn write_session(w: &mut Writer, s: &Session) {
    w.byte(b'{');
    w.num("start_utc", s.start_utc as u64);
    w.byte(b',');
    w.num("active_s", s.active_s as u64);
    w.byte(b',');
    w.num("elapsed_s", s.elapsed_s as u64);
    w.byte(b',');
    w.num("hr_avg", s.hr_avg as u64);
    w.byte(b',');
    w.num("hr_max", s.hr_max as u64);
    w.byte(b',');
    w.num("hr_max_setting", s.hr_max_setting as u64);
    w.byte(b',');
    w.num("weight_kg", s.weight_kg as u64);
    w.byte(b',');
    w.num("kcal", s.kcal as u64);

    // Absent, not zero, exactly as the FIT file leaves it out: zero is a
    // measurement claiming the wearer produced no work.
    if s.work_kj > 0 {
        w.byte(b',');
        w.num("work_kj", s.work_kj as u64);
    }

    w.byte(b',');
    w.num("zone_count", s.zone_count as u64);
    w.byte(b',');
    w.key("zone_floors").byte(b'[');
    for i in 0..(s.zone_count as usize).min(MAX_ZONES) {
        if i > 0 {
            w.byte(b',');
        }
        w.u64(s.zone_floor[i] as u64);
    }
    w.raw("],");
    w.key("zone_s").byte(b'[');
    for i in 0..s.zone_bucket_count() {
        if i > 0 {
            w.byte(b',');
        }
        w.u64(s.zone_s[i] as u64);
    }
    w.byte(b']');

    // Only for the ladder the weights are defined over; see load.rs.
    if let Some(t) = edwards_trimp(s) {
        w.byte(b',');
        w.num("edwards_trimp", t as u64);
    }

    w.byte(b',');
    w.key("recoveries").byte(b'[');
    for (i, r) in s.recoveries().iter().enumerate() {
        if i > 0 {
            w.byte(b',');
        }
        write_recovery(w, r);
    }
    w.byte(b']');
    w.byte(b',');
    w.num("recoveries_dropped", s.recoveries_dropped as u64);
    w.byte(b'}');
}

fn write_recovery(w: &mut Writer, r: &Recovery) {
    w.byte(b'{');
    w.num("at_active_s", r.at_active_s as u64);
    w.byte(b',');
    w.text("trigger", trigger_name(r.trigger));
    w.byte(b',');
    w.num("hr0", r.hr0 as u64);
    w.byte(b',');
    w.num("hr_end", r.hr_end as u64);
    w.byte(b',');
    w.num("drop_bpm", r.drop_bpm() as u64);
    w.byte(b',');
    w.num("window_s", r.window_s as u64);
    w.byte(b',');
    w.num("hr0_pct_max", r.hr0_pct_max as u64);
    w.byte(b',');
    w.num("trusted_s", r.trusted_s as u64);
    w.byte(b',');
    w.text("source", source_name(r.source));
    w.byte(b',');
    w.key("curve").byte(b'[');
    for (i, v) in r.curve.iter().enumerate() {
        if i > 0 {
            w.byte(b',');
        }
        w.u64(*v as u64);
    }
    w.raw("]}");
}

fn parse_session(obj: &[u8]) -> Session {
    let mut s = Session {
        start_utc: json::as_u32_or(json::member(obj, "start_utc"), 0),
        active_s: json::as_u32_or(json::member(obj, "active_s"), 0),
        elapsed_s: json::as_u32_or(json::member(obj, "elapsed_s"), 0),
        kcal: json::as_u16_or(json::member(obj, "kcal"), 0),
        work_kj: json::as_u16_or(json::member(obj, "work_kj"), 0),
        hr_avg: json::as_u8_or(json::member(obj, "hr_avg"), 0),
        hr_max: json::as_u8_or(json::member(obj, "hr_max"), 0),
        hr_max_setting: json::as_u8_or(json::member(obj, "hr_max_setting"), 0),
        weight_kg: json::as_u8_or(json::member(obj, "weight_kg"), 0),
        zone_count: json::as_u8_or(json::member(obj, "zone_count"), 0),
        recoveries_dropped: json::as_u8_or(json::member(obj, "recoveries_dropped"), 0),
        ..Session::default()
    };
    if s.zone_count as usize > MAX_ZONES {
        s.zone_count = MAX_ZONES as u8;
    }

    if let Some(list) = json::member(obj, "zone_floors") {
        for (i, v) in json::items(list).enumerate().take(MAX_ZONES) {
            s.zone_floor[i] = json::as_u8_or(Some(v), 0);
        }
    }
    if let Some(list) = json::member(obj, "zone_s") {
        for (i, v) in json::items(list).enumerate().take(MAX_ZONE_BUCKETS) {
            s.zone_s[i] = json::as_u16_or(Some(v), 0);
        }
    }
    if let Some(list) = json::member(obj, "recoveries") {
        let mut n = 0;
        for item in json::items(list) {
            if n == MAX_RECOVERIES {
                s.recoveries_dropped = s.recoveries_dropped.saturating_add(1);
                continue;
            }
            s.recoveries[n] = parse_recovery(item);
            n += 1;
        }
        s.recovery_count = n as u8;
    }
    s
}

fn parse_recovery(obj: &[u8]) -> Recovery {
    let mut r = Recovery {
        at_active_s: json::as_u32_or(json::member(obj, "at_active_s"), 0),
        hr0: json::as_u8_or(json::member(obj, "hr0"), 0),
        hr_end: json::as_u8_or(json::member(obj, "hr_end"), 0),
        window_s: json::as_u8_or(json::member(obj, "window_s"), 0),
        trusted_s: json::as_u8_or(json::member(obj, "trusted_s"), 0),
        hr0_pct_max: json::as_u8_or(json::member(obj, "hr0_pct_max"), 0),
        trigger: json::member(obj, "trigger").map_or(0, trigger_value),
        source: json::member(obj, "source").map_or(0, source_value),
        ..Recovery::default()
    };
    if let Some(list) = json::member(obj, "curve") {
        for (i, v) in json::items(list).enumerate().take(CURVE_POINTS) {
            r.curve[i] = json::as_u8_or(Some(v), 0);
        }
    }
    r
}

fn trigger_name(t: u8) -> &'static [u8] {
    match t {
        TRIGGER_PAUSE => b"pause",
        TRIGGER_LAP => b"lap",
        TRIGGER_STOP => b"stop",
        _ => b"unknown",
    }
}

fn source_name(s: u8) -> &'static [u8] {
    match s {
        HR_SOURCE_OPTICAL => b"optical",
        HR_SOURCE_EXTERNAL => b"external",
        _ => b"unknown",
    }
}

fn source_value(v: &[u8]) -> u8 {
    match v {
        b"\"optical\"" => HR_SOURCE_OPTICAL,
        b"\"external\"" => HR_SOURCE_EXTERNAL,
        _ => HR_SOURCE_NONE,
    }
}

fn trigger_value(v: &[u8]) -> u8 {
    match v {
        b"\"pause\"" => TRIGGER_PAUSE,
        b"\"lap\"" => TRIGGER_LAP,
        b"\"stop\"" => TRIGGER_STOP,
        _ => 0,
    }
}

fn copy_name(dst: &mut [u8; MAX_NAME_LEN], src: &[u8]) -> usize {
    let mut n = 0;
    for &c in src {
        if n == MAX_NAME_LEN {
            break;
        }
        // Anything outside plain ASCII would need escaping, and the only names
        // here are chosen in this repository.
        if !(c.is_ascii_alphanumeric() || c == b'_' || c == b'-') {
            continue;
        }
        dst[n] = c;
        n += 1;
    }
    n
}
