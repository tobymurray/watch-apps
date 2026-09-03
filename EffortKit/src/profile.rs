//! The wearer's own file: the sessions a baseline is built from.
//!
//! Written by the app that owns it and read by whatever comes next, so the
//! schema is a contract; `README.md` documents it as a table with units. It
//! stores measurements rather than conclusions, which is why the baselines are
//! derived on load instead of persisted — a change to how one is computed then
//! applies to the whole history rather than orphaning it.
//!
//! Every value on the wire is an integer. There is no float formatter in
//! `no_std`, and a fixed-point hundredth is exact where a hand-rolled decimal
//! would not be.

use crate::baseline::{RollingBaseline, WINDOW};
use crate::recovery::HrSource;
use crate::session::{Metric, SessionRecord};

/// The schema this build writes and the only one it will read.
pub const SCHEMA_VERSION: i64 = 1;

/// Sessions kept in the file.
///
/// Matched to [`crate::baseline::WINDOW`] because a session older than the
/// rolling window can no longer affect any baseline, so keeping it would grow
/// the file for nothing.
pub const MAX_SESSIONS: usize = WINDOW;

/// Bytes the writer will not exceed.
///
/// A full file is twenty records of about 210 bytes; 8 KiB leaves room for the
/// schema to gain fields without the cap becoming the thing that stops it.
pub const MAX_BYTES: usize = 8192;

/// What happened when a file was read.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Load {
    /// A file of this schema, read completely.
    Ok,
    /// No file, or an empty one. The ordinary first-run case.
    Absent,
    /// The file names a schema this build does not read, so nothing was taken
    /// from it. Refusing rather than reading what is recognised, because these
    /// fields are bare integers whose meaning a later schema could change, and
    /// a wrong baseline is worse than a warm-up.
    UnknownSchema,
    /// The file did not parse as far as the session list.
    Malformed,
    /// More sessions in the file than [`MAX_SESSIONS`]; the newest were kept.
    Truncated,
}

/// The writer could not fit the file into the buffer it was given.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BufferTooSmall;

/// The wearer's sessions, newest last.
#[derive(Clone, Copy, Debug)]
pub struct Profile {
    sessions: [SessionRecord; MAX_SESSIONS],
    len: usize,
}

impl Default for Profile {
    fn default() -> Self {
        Self::empty()
    }
}

impl Profile {
    /// A profile with no history, which is what every failure returns.
    pub const fn empty() -> Self {
        Self { sessions: [EMPTY_RECORD; MAX_SESSIONS], len: 0 }
    }

    /// Sessions held.
    pub const fn len(&self) -> usize {
        self.len
    }

    /// True when no session has been recorded.
    pub const fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// The sessions, oldest first.
    pub fn sessions(&self) -> &[SessionRecord] {
        &self.sessions[..self.len]
    }

    /// Add a session, dropping the oldest once the file is full.
    pub fn record(&mut self, r: SessionRecord) {
        if self.len == MAX_SESSIONS {
            self.sessions.copy_within(1.., 0);
            self.sessions[MAX_SESSIONS - 1] = r;
        } else {
            self.sessions[self.len] = r;
            self.len += 1;
        }
    }

    /// The baseline for one metric, built from the sessions admitted to it.
    pub fn baseline_of(&self, metric: Metric) -> RollingBaseline {
        let mut b = RollingBaseline::new();
        for s in self.sessions() {
            if s.admits(metric).is_ok() {
                if let Some(v) = s.value(metric) {
                    b.push(v);
                }
            }
        }
        b
    }

    /// Serialise into `out`, returning the bytes written.
    ///
    /// Nothing is written unless the whole file fits, so a caller that commits
    /// only on `Ok` can never leave a truncated file behind.
    pub fn write_json(&self, out: &mut [u8]) -> Result<usize, BufferTooSmall> {
        let mut w = Writer { out, at: 0, overflowed: false };
        w.str("{\"schema\":");
        w.int(SCHEMA_VERSION);
        w.str(",\"sessions\":[");
        for (i, s) in self.sessions().iter().enumerate() {
            if i > 0 {
                w.str(",");
            }
            w.str("{\"utc\":");
            w.int(s.started_utc as i64);
            w.field("active_s", s.active_s as i64);
            w.field("hr_mean_x100", hundredths(s.hr_mean));
            w.field("hr_max_x100", hundredths(s.hr_max));
            w.field("hr_covered_s", s.hr_covered_s as i64);
            w.field("hr_source", source_code(s.hr_source));
            w.field("segmented", s.segmented as i64);
            w.field("rally_count", s.rally_count as i64);
            w.field("rally_s", s.rally_s as i64);
            w.field("rest_s", s.rest_s as i64);
            w.field("off_court_s", s.off_court_s as i64);
            w.field("rec_short_x100", hundredths(s.recovery_short_mean));
            w.field("rec_short_n", s.recovery_short_n as i64);
            w.field("rec_long_x100", hundredths(s.recovery_long_mean));
            w.field("rec_long_n", s.recovery_long_n as i64);
            w.str("}");
        }
        w.str("]}");
        if w.overflowed || w.at > MAX_BYTES {
            return Err(BufferTooSmall);
        }
        Ok(w.at)
    }

    /// Read a file, which cannot fail.
    ///
    /// Every way of being wrong — absent, truncated, from a schema this build
    /// does not know — yields an empty profile and a [`Load`] saying which, so
    /// a file somebody else wrote can never stop the app starting.
    pub fn parse_json(bytes: &[u8]) -> (Self, Load) {
        if bytes.is_empty() {
            return (Self::empty(), Load::Absent);
        }
        match find_int(bytes, "schema") {
            None => return (Self::empty(), Load::Malformed),
            Some(v) if v != SCHEMA_VERSION => return (Self::empty(), Load::UnknownSchema),
            Some(_) => {}
        }
        let Some(list) = after_key(bytes, "sessions").and_then(|i| bytes[i..].iter().position(|b| *b == b'[').map(|j| i + j + 1)) else {
            return (Self::empty(), Load::Malformed);
        };

        let mut p = Self::empty();
        let mut truncated = false;
        let mut at = list;
        while let Some((start, end)) = next_object(bytes, at) {
            let obj = &bytes[start..end];
            let r = SessionRecord {
                started_utc: find_int(obj, "utc").unwrap_or(0) as u32,
                active_s: find_int(obj, "active_s").unwrap_or(0) as u32,
                hr_mean: from_hundredths(find_int(obj, "hr_mean_x100").unwrap_or(0)),
                hr_max: from_hundredths(find_int(obj, "hr_max_x100").unwrap_or(0)),
                hr_covered_s: find_int(obj, "hr_covered_s").unwrap_or(0) as u32,
                hr_source: source_of(find_int(obj, "hr_source").unwrap_or(0)),
                segmented: find_int(obj, "segmented").unwrap_or(0) != 0,
                rally_count: find_int(obj, "rally_count").unwrap_or(0) as u32,
                rally_s: find_int(obj, "rally_s").unwrap_or(0) as u32,
                rest_s: find_int(obj, "rest_s").unwrap_or(0) as u32,
                off_court_s: find_int(obj, "off_court_s").unwrap_or(0) as u32,
                recovery_short_mean: from_hundredths(find_int(obj, "rec_short_x100").unwrap_or(0)),
                recovery_short_n: find_int(obj, "rec_short_n").unwrap_or(0) as u16,
                recovery_long_mean: from_hundredths(find_int(obj, "rec_long_x100").unwrap_or(0)),
                recovery_long_n: find_int(obj, "rec_long_n").unwrap_or(0) as u16,
            };
            if p.len == MAX_SESSIONS {
                truncated = true;
            }
            p.record(r);
            at = end;
        }
        (p, if truncated { Load::Truncated } else { Load::Ok })
    }
}

const EMPTY_RECORD: SessionRecord = SessionRecord {
    started_utc: 0,
    active_s: 0,
    hr_mean: 0.0,
    hr_max: 0.0,
    hr_covered_s: 0,
    hr_source: HrSource::Unknown,
    segmented: false,
    rally_count: 0,
    rally_s: 0,
    rest_s: 0,
    off_court_s: 0,
    recovery_short_mean: 0.0,
    recovery_short_n: 0,
    recovery_long_mean: 0.0,
    recovery_long_n: 0,
};

const fn source_code(s: HrSource) -> i64 {
    match s {
        HrSource::Unknown => 0,
        HrSource::Optical => 1,
        HrSource::External => 2,
    }
}

const fn source_of(v: i64) -> HrSource {
    match v {
        1 => HrSource::Optical,
        2 => HrSource::External,
        _ => HrSource::Unknown,
    }
}

fn hundredths(v: f32) -> i64 {
    libm::roundf(v * 100.0) as i64
}

fn from_hundredths(v: i64) -> f32 {
    v as f32 / 100.0
}

struct Writer<'a> {
    out: &'a mut [u8],
    at: usize,
    overflowed: bool,
}

impl Writer<'_> {
    fn byte(&mut self, b: u8) {
        if self.at < self.out.len() {
            self.out[self.at] = b;
        } else {
            self.overflowed = true;
        }
        self.at += 1;
    }

    fn str(&mut self, s: &str) {
        for b in s.as_bytes() {
            self.byte(*b);
        }
    }

    fn int(&mut self, mut v: i64) {
        if v < 0 {
            self.byte(b'-');
        }
        let mut digits = [0u8; 20];
        let mut n = 0;
        loop {
            let d = (v % 10).unsigned_abs() as u8;
            digits[n] = b'0' + d;
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
    }

    fn field(&mut self, key: &str, value: i64) {
        self.str(",\"");
        self.str(key);
        self.str("\":");
        self.int(value);
    }
}

fn find_quoted(haystack: &[u8], key: &str) -> Option<usize> {
    let k = key.as_bytes();
    let n = k.len();
    if haystack.len() < n + 2 {
        return None;
    }
    for i in 0..=(haystack.len() - n - 2) {
        if haystack[i] == b'"' && haystack[i + n + 1] == b'"' && &haystack[i + 1..i + 1 + n] == k {
            return Some(i + n + 2);
        }
    }
    None
}

fn after_key(haystack: &[u8], key: &str) -> Option<usize> {
    let mut at = find_quoted(haystack, key)?;
    while at < haystack.len() && (haystack[at] == b':' || haystack[at].is_ascii_whitespace()) {
        at += 1;
    }
    Some(at)
}

fn find_int(haystack: &[u8], key: &str) -> Option<i64> {
    let mut at = after_key(haystack, key)?;
    let negative = haystack.get(at) == Some(&b'-');
    if negative {
        at += 1;
    }
    let start = at;
    let mut v: i64 = 0;
    while at < haystack.len() && haystack[at].is_ascii_digit() {
        v = v.saturating_mul(10).saturating_add((haystack[at] - b'0') as i64);
        at += 1;
    }
    (at > start).then(|| if negative { -v } else { v })
}

fn next_object(bytes: &[u8], from: usize) -> Option<(usize, usize)> {
    let start = from + bytes[from..].iter().position(|b| *b == b'{')?;
    let end = start + bytes[start..].iter().position(|b| *b == b'}')? + 1;
    Some((start, end))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::baseline::MIN_SESSIONS_FOR_COMPARISON;

    fn record(utc: u32, hr: f32) -> SessionRecord {
        SessionRecord {
            started_utc: utc,
            active_s: 3600,
            hr_mean: hr,
            hr_max: hr + 30.0,
            hr_covered_s: 3500,
            hr_source: HrSource::External,
            segmented: false,
            ..EMPTY_RECORD
        }
    }

    fn round_trip(p: &Profile) -> (Profile, Load) {
        let mut buf = [0u8; MAX_BYTES];
        let n = p.write_json(&mut buf).expect("a full profile fits");
        Profile::parse_json(&buf[..n])
    }

    #[test]
    fn an_absent_file_is_an_empty_profile_and_not_an_error() {
        let (p, load) = Profile::parse_json(b"");
        assert!(p.is_empty());
        assert_eq!(load, Load::Absent);
    }

    #[test]
    fn a_truncated_file_is_an_empty_profile_and_not_an_error() {
        let (p, load) = Profile::parse_json(b"{\"schema\":1,\"sess");
        assert!(p.is_empty());
        assert_eq!(load, Load::Malformed);
    }

    #[test]
    fn a_file_from_a_later_schema_is_refused_rather_than_half_read() {
        let (p, load) = Profile::parse_json(b"{\"schema\":99,\"sessions\":[{\"utc\":7}]}");
        assert!(p.is_empty());
        assert_eq!(load, Load::UnknownSchema);
    }

    #[test]
    fn garbage_is_an_empty_profile_and_not_an_error() {
        for junk in [&b"not json at all"[..], &b"\x00\x01\x02"[..], &b"{}"[..]] {
            let (p, _) = Profile::parse_json(junk);
            assert!(p.is_empty());
        }
    }

    #[test]
    fn a_session_survives_the_round_trip_exactly() {
        let mut p = Profile::empty();
        let r = SessionRecord {
            segmented: true,
            rally_count: 42,
            rally_s: 900,
            rest_s: 1200,
            off_court_s: 1500,
            recovery_short_mean: 12.25,
            recovery_short_n: 8,
            recovery_long_mean: 34.5,
            recovery_long_n: 3,
            ..record(1_756_900_000, 142.5)
        };
        p.record(r);
        let (back, load) = round_trip(&p);
        assert_eq!(load, Load::Ok);
        assert_eq!(back.sessions(), &[r]);
    }

    #[test]
    fn a_full_file_fits_the_stated_cap() {
        let mut p = Profile::empty();
        for i in 0..MAX_SESSIONS {
            p.record(SessionRecord {
                segmented: true,
                rally_count: 999,
                rally_s: 99_999,
                rest_s: 99_999,
                off_court_s: 99_999,
                recovery_short_mean: 99.99,
                recovery_short_n: 999,
                recovery_long_mean: 99.99,
                recovery_long_n: 999,
                ..record(2_000_000_000 + i as u32, 199.99)
            });
        }
        let mut buf = [0u8; MAX_BYTES];
        let n = p.write_json(&mut buf).expect("twenty full records must fit MAX_BYTES");
        assert!(n <= MAX_BYTES, "{n} bytes");
    }

    #[test]
    fn a_buffer_that_is_too_small_writes_nothing_a_caller_would_commit() {
        let mut p = Profile::empty();
        p.record(record(1, 150.0));
        let mut small = [0u8; 8];
        assert_eq!(p.write_json(&mut small), Err(BufferTooSmall));
    }

    #[test]
    fn the_oldest_session_falls_out_of_a_full_profile() {
        let mut p = Profile::empty();
        for i in 0..(MAX_SESSIONS + 3) {
            p.record(record(1000 + i as u32, 140.0));
        }
        assert_eq!(p.len(), MAX_SESSIONS);
        assert_eq!(p.sessions()[0].started_utc, 1003);
    }

    #[test]
    fn baselines_are_derived_from_the_stored_sessions() {
        let mut p = Profile::empty();
        for (i, hr) in [140.0, 142.0, 138.0, 141.0, 139.0].iter().enumerate() {
            p.record(record(1000 + i as u32, *hr));
        }
        let b = p.baseline_of(Metric::HrMean);
        assert_eq!(b.sessions(), MIN_SESSIONS_FOR_COMPARISON);
        assert_eq!(b.median(), Some(140.0));
    }

    #[test]
    fn a_session_that_may_not_vote_is_stored_but_not_in_the_baseline() {
        let mut p = Profile::empty();
        for i in 0..5 {
            p.record(record(1000 + i, 140.0));
        }
        p.record(SessionRecord { active_s: 120, ..record(2000, 90.0) });
        assert_eq!(p.len(), 6);
        assert_eq!(p.baseline_of(Metric::HrMean).sessions(), 5);
    }

    #[test]
    fn an_unsegmented_session_contributes_nothing_to_a_rally_baseline() {
        let mut p = Profile::empty();
        for i in 0..8 {
            p.record(record(1000 + i, 140.0));
        }
        assert_eq!(p.baseline_of(Metric::RallyCount).sessions(), 0);
        assert!(p.baseline_of(Metric::RallyCount).compare(10.0).is_err());
    }
}
