//! The record that outlives the session: a bounded, versioned session log in
//! `../SharedData/`, for whatever wants to read a series later.
//!
//! WHAT THIS IS AND IS NOT. This is the *series* — one row per session, the
//! comparable measurements, and the protocol beside each one — and it is
//! cross-app by design. A session's own interior, every window it measured and
//! whatever structure its sport has, is the app's business and lives in
//! [`crate::profile`]. Nothing that belongs in the `.fit` is here either: the
//! `.fit` is the record of one session, this is the record of the series.
//!
//! WHY IT IS BOUNDED. This is flash with no allocator, and a log that appends
//! forever eventually fails to parse, fails to write, or fills the card. Two
//! caps hold it: [`MAX_SESSIONS`] entries and [`MAX_STORE_BYTES`] of text. The
//! byte cap is the real one — [`History::save`] drops the oldest entry and
//! re-serialises until the result fits — so the newest session always lands and
//! it is the oldest that goes. `"dropped"` counts everything ever evicted, so a
//! reader can tell a truncated series from a complete one.
//!
//! WHAT A READER DOES WITH A VERSION IT DOES NOT KNOW. [`History::load`]
//! reports [`Load::Newer`] for a `"version"` above [`SCHEMA_VERSION`] and reads
//! nothing: a writer that knows more than this one does must not be clobbered
//! by it. A file that is not JSON, or carries no version, reports
//! [`Load::Unreadable`] so the caller can keep it as evidence and start fresh.

use crate::json::{self, Writer};
use crate::load::edwards_trimp;
use crate::record::*;
use crate::window::WindowKind;

/// The schema this code writes.
///
/// Version 2 adds the per-reason discard counts. It is a superset of version 1,
/// so a v1 file reads correctly here and simply carries no counts — which is
/// truthful, because it did not record any. A v1 build refuses a v2 file
/// outright and writes nothing, so a downgrade loses nothing either.
pub const SCHEMA_VERSION: u64 = 2;

/// Entries kept: four or five weeks at four sessions a week.
///
/// MEASURED, by `the_widest_possible_log_fits_the_buffer` in `tests/history.rs`,
/// which asserts the exact byte count and must be re-run after any schema
/// change. The cap is what keeps the widest possible log inside
/// [`MAX_STORE_BYTES`] with room to add a field.
pub const MAX_SESSIONS: usize = 20;

/// The largest file this will write or read.
///
/// The same cap the SDK's own shared store uses
/// (`SDK::Calibration::StrideLut::kMaxStoreBytes`), and for the same reason:
/// the read is buffered, so the bound is what keeps a corrupt or foreign file
/// from asking for a transient allocation this device does not have.
pub const MAX_STORE_BYTES: usize = 16 * 1024;

/// Longest app or sport name the header carries.
pub const MAX_NAME_LEN: usize = 24;

/// Where a store file goes, relative to the app's own directory.
///
/// The SDK's own convention rather than an invention:
/// `SDK::Calibration::StrideLut::kDefaultPath` is `"../SharedData/stride.json"`.
pub const SHARED_DIR: &str = "../SharedData";

/// What every app's store file name ends with, so a reader wanting every sport
/// can glob for the pattern and merge on the `app` and `sport` fields.
pub const STORE_SUFFIX: &str = "_sessions.json";

/// What [`History::load`] made of the bytes it was given.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Load {
    /// Parsed, or there was nothing there to parse.
    Ok,
    /// A schema this build does not write, and not to be overwritten.
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
    /// An empty log.
    pub const fn new() -> Self {
        History {
            sessions: [Session::EMPTY; MAX_SESSIONS],
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
    /// only `[A-Za-z0-9_-]` survives, which is both valid UTF-8 and free of
    /// JSON escapes by construction.
    pub fn name(&mut self, app: &[u8], sport: &[u8]) {
        self.app_len = copy_name(&mut self.app, app);
        self.sport_len = copy_name(&mut self.sport, sport);
    }

    /// The sessions held, oldest first.
    pub fn sessions(&self) -> &[Session] {
        &self.sessions[..self.len]
    }

    /// Sessions evicted over the life of the file.
    pub const fn dropped(&self) -> u32 {
        self.dropped
    }

    /// Read a store file. Clears whatever was held first, so a failed load
    /// leaves an empty log rather than a half-read one.
    pub fn load(&mut self, buf: &[u8]) -> Load {
        let (app, app_len) = (self.app, self.app_len);
        let (sport, sport_len) = (self.sport, self.sport_len);
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

        let Some(version) = json::member(buf, "version").and_then(json::as_u64) else {
            return Load::Unreadable;
        };
        if version > SCHEMA_VERSION {
            return Load::Newer;
        }

        let Some(list) = json::member(buf, "sessions") else {
            return Load::Unreadable;
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
    /// A re-recorded session is the same session: `start_utc` is the entry's
    /// identity, so a write retried after a failure cannot make one session
    /// into two and cannot let it vote twice in anything derived from this.
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
    /// Returns the bytes written, or `None` when not even one session fits,
    /// which leaves the caller to write nothing at all.
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
    for i in 0..zone_bucket_count(s) {
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
    write_discarded(w, &s.discarded);
    w.byte(b'}');
}

/// Emitted only for reasons that actually fired, and omitted entirely when none
/// did, so the ordinary session costs the bytes of nothing.
fn write_discarded(w: &mut Writer, d: &DiscardCounts) {
    if d.total() == 0 {
        return;
    }
    w.raw(",\"discarded\":{");
    let mut first = true;
    for (name, n) in discard_fields(d) {
        if n == 0 {
            continue;
        }
        if !first {
            w.byte(b',');
        }
        first = false;
        w.num(name, n as u64);
    }
    w.byte(b'}');
}

/// The wire name of every discard counter, beside its value.
///
/// One list, walked by both the writer and the reader, so a name cannot be
/// written under one spelling and read under another.
fn discard_fields(d: &DiscardCounts) -> [(&'static str, u16); 14] {
    [
        ("not_calibrated", d.not_calibrated),
        ("not_measurable", d.not_measurable),
        ("no_max_hr", d.no_max_hr),
        ("too_short", d.too_short),
        ("too_easy", d.too_easy),
        ("already_falling", d.already_falling),
        ("no_baseline_history", d.no_baseline_history),
        ("no_baseline", d.no_baseline),
        ("dropout", d.dropout),
        ("no_endpoint", d.no_endpoint),
        ("effort_resumed", d.effort_resumed),
        ("session_ended", d.session_ended),
        ("source_changed", d.source_changed),
        ("source_not_accepted", d.source_not_accepted),
    ]
}

fn parse_discarded(obj: &[u8]) -> DiscardCounts {
    let Some(d) = json::member(obj, "discarded") else {
        return DiscardCounts::NONE;
    };
    let g = |k: &str| json::as_u16_or(json::member(d, k), 0);
    DiscardCounts {
        not_calibrated: g("not_calibrated"),
        not_measurable: g("not_measurable"),
        no_max_hr: g("no_max_hr"),
        too_short: g("too_short"),
        too_easy: g("too_easy"),
        already_falling: g("already_falling"),
        no_baseline_history: g("no_baseline_history"),
        no_baseline: g("no_baseline"),
        dropout: g("dropout"),
        no_endpoint: g("no_endpoint"),
        effort_resumed: g("effort_resumed"),
        session_ended: g("session_ended"),
        source_changed: g("source_changed"),
        source_not_accepted: g("source_not_accepted"),
    }
}

fn write_recovery(w: &mut Writer, r: &Recovery) {
    w.byte(b'{');
    w.num("at_active_s", r.at_active_s as u64);
    w.byte(b',');
    w.text("kind", kind_name(r.kind));
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

fn zone_bucket_count(s: &Session) -> usize {
    if s.zone_count == 0 {
        0
    } else {
        (s.zone_count as usize + 1).min(MAX_ZONE_BUCKETS)
    }
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
        discarded: parse_discarded(obj),
        ..Session::EMPTY
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
        // "trigger" is version 1's spelling of the same field.
        kind: json::member(obj, "kind")
            .or_else(|| json::member(obj, "trigger"))
            .map_or(0, kind_value),
        source: json::member(obj, "source").map_or(0, source_value),
        ..Recovery::EMPTY
    };
    if let Some(list) = json::member(obj, "curve") {
        for (i, v) in json::items(list).enumerate().take(CURVE_POINTS) {
            r.curve[i] = json::as_u8_or(Some(v), 0);
        }
    }
    r
}

fn kind_name(k: u8) -> &'static [u8] {
    match WindowKind::from_code(k) {
        Some(WindowKind::Pause) => b"pause",
        Some(WindowKind::Lap) => b"lap",
        Some(WindowKind::Stop) => b"stop",
        Some(WindowKind::BetweenRallies) => b"between_rallies",
        Some(WindowKind::OffCourt) => b"off_court",
        None => b"unknown",
    }
}

fn kind_value(v: &[u8]) -> u8 {
    match v {
        b"\"pause\"" => WindowKind::Pause.code(),
        b"\"lap\"" => WindowKind::Lap.code(),
        b"\"stop\"" => WindowKind::Stop.code(),
        b"\"between_rallies\"" => WindowKind::BetweenRallies.code(),
        b"\"off_court\"" => WindowKind::OffCourt.code(),
        _ => 0,
    }
}

fn source_name(s: u8) -> &'static [u8] {
    match s {
        1 => b"optical",
        2 => b"external",
        _ => b"unknown",
    }
}

fn source_value(v: &[u8]) -> u8 {
    match v {
        b"\"optical\"" => 1,
        b"\"external\"" => 2,
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
