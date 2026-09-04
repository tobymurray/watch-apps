//! The app's own file: the sessions a baseline is built from.
//!
//! Owned and written by one app and read by nobody else, which is what
//! separates it from [`crate::history`]. That file is the *series* — one
//! comparable measurement per session, cross-app, a documented contract. This
//! is the *interior*: every window a session measured and whatever structure
//! its sport has. An app that segments needs both; an app that does not may
//! never write this one at all.
//!
//! It stores measurements rather than conclusions. Baselines are derived on
//! load and never persisted, and so are the per-kind recovery means — a change
//! to how either is computed then applies to the whole history instead of
//! orphaning it. Persisting a mean would orphan it, because the windows it
//! averaged would be gone.
//!
//! Every value on the wire is an integer. There is no float formatter in
//! `no_std`, and a fixed-point hundredth is exact where a hand-rolled decimal
//! would not be.

use crate::baseline::{RollingBaseline, WINDOW};
use crate::hr::HrSource;
use crate::json::{self, Writer};
use crate::record::Recovery;
use crate::session::{Metric, SessionRecord};

/// The schema this build writes and the only one it will read.
pub const SCHEMA_VERSION: u64 = 1;

/// Sessions kept in the file.
///
/// Matched to [`crate::baseline::WINDOW`] because a session older than the
/// rolling window can no longer affect any baseline, so keeping it would grow
/// the file for nothing.
pub const MAX_SESSIONS: usize = WINDOW;

/// Bytes the writer will not exceed.
///
/// MEASURED, by `the_widest_profile_fits_its_cap` in `tests/profile.rs`, which
/// asserts the exact byte count and must be re-run after any schema change.
pub const MAX_BYTES: usize = 16384;

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

/// The wearer's sessions, oldest first.
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
        Self { sessions: [SessionRecord::EMPTY; MAX_SESSIONS], len: 0 }
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

    /// Add a session, replacing any entry that already claims the same start.
    ///
    /// `started_utc` is the entry's identity for the same reason it is in
    /// [`crate::history`]: a write retried after a failure must not let one
    /// session vote twice in every baseline derived from this file.
    pub fn record(&mut self, r: SessionRecord) {
        if let Some(i) = self.sessions[..self.len]
            .iter()
            .position(|e| e.started_utc == r.started_utc && r.started_utc != 0)
        {
            self.sessions[i] = r;
            return;
        }
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
    /// only on `Ok` can never leave a truncated file behind. Unlike the shared
    /// log this does not evict to make room: this file is the app's own and a
    /// caller that cannot write it all has a bug, not a full disk.
    pub fn write_json(&self, out: &mut [u8]) -> Result<usize, BufferTooSmall> {
        let cap = out.len().min(MAX_BYTES);
        let mut w = Writer::new(&mut out[..cap]);
        w.byte(b'{');
        w.num("schema", SCHEMA_VERSION);
        w.byte(b',');
        w.key("sessions").byte(b'[');
        for (i, s) in self.sessions().iter().enumerate() {
            if i > 0 {
                w.byte(b',');
            }
            write_session(&mut w, s);
        }
        w.raw("]}");
        if w.overflowed() {
            return Err(BufferTooSmall);
        }
        Ok(w.len())
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
        match json::member(bytes, "schema").and_then(json::as_u64) {
            None => return (Self::empty(), Load::Malformed),
            Some(v) if v != SCHEMA_VERSION => return (Self::empty(), Load::UnknownSchema),
            Some(_) => {}
        }
        let Some(list) = json::member(bytes, "sessions") else {
            return (Self::empty(), Load::Malformed);
        };

        let mut p = Self::empty();
        let mut truncated = false;
        for item in json::items(list) {
            if p.len == MAX_SESSIONS {
                truncated = true;
            }
            p.record(parse_session(item));
        }
        (p, if truncated { Load::Truncated } else { Load::Ok })
    }
}

// -- The schema ---------------------------------------------------------------

fn write_session(w: &mut Writer, s: &SessionRecord) {
    w.byte(b'{');
    w.num("utc", s.started_utc as u64);
    w.byte(b',');
    w.num("active_s", s.active_s as u64);
    w.byte(b',');
    w.num("hr_mean_x100", hundredths(s.hr_mean));
    w.byte(b',');
    w.num("hr_max_x100", hundredths(s.hr_max));
    w.byte(b',');
    w.num("hr_covered_s", s.hr_covered_s as u64);
    w.byte(b',');
    w.num("hr_source", s.hr_source.code() as u64);
    w.byte(b',');
    w.num("segmented", s.segmented as u64);
    w.byte(b',');
    w.num("rally_count", s.rally_count as u64);
    w.byte(b',');
    w.num("rally_s", s.rally_s as u64);
    w.byte(b',');
    w.num("rest_s", s.rest_s as u64);
    w.byte(b',');
    w.num("off_court_s", s.off_court_s as u64);
    w.byte(b',');
    w.num("windows_dropped", s.windows_dropped as u64);
    w.byte(b',');
    // A sum and a count per kind, so the mean is over every qualifying window
    // and not only over the four whose detail fits below.
    w.key("drop_sum").byte(b'[');
    for (i, v) in s.drop_sum.iter().enumerate() {
        if i > 0 {
            w.byte(b',');
        }
        w.u64(*v as u64);
    }
    w.raw("],");
    w.key("drop_n").byte(b'[');
    for (i, v) in s.drop_n.iter().enumerate() {
        if i > 0 {
            w.byte(b',');
        }
        w.u64(*v as u64);
    }
    w.raw("],");
    w.key("windows").byte(b'[');
    for (i, r) in s.windows().iter().enumerate() {
        if i > 0 {
            w.byte(b',');
        }
        write_window(w, r);
    }
    w.raw("]}");
}

/// No curve here. It is seven numbers, it is only ever wanted for a fit this
/// hardware cannot support, and the cross-app log already keeps one per
/// measurement — which is enough to revisit that decision later.
fn write_window(w: &mut Writer, r: &Recovery) {
    w.byte(b'{');
    w.num("kind", r.kind as u64);
    w.byte(b',');
    w.num("at_active_s", r.at_active_s as u64);
    w.byte(b',');
    w.num("hr0", r.hr0 as u64);
    w.byte(b',');
    w.num("hr_end", r.hr_end as u64);
    w.byte(b',');
    w.num("window_s", r.window_s as u64);
    w.byte(b',');
    w.num("trusted_s", r.trusted_s as u64);
    w.byte(b',');
    w.num("hr0_pct_max", r.hr0_pct_max as u64);
    w.byte(b',');
    w.num("source", r.source as u64);
    w.byte(b'}');
}

fn parse_session(obj: &[u8]) -> SessionRecord {
    let mut s = SessionRecord {
        started_utc: json::as_u32_or(json::member(obj, "utc"), 0),
        active_s: json::as_u32_or(json::member(obj, "active_s"), 0),
        hr_mean: from_hundredths(json::as_u32_or(json::member(obj, "hr_mean_x100"), 0)),
        hr_max: from_hundredths(json::as_u32_or(json::member(obj, "hr_max_x100"), 0)),
        hr_covered_s: json::as_u32_or(json::member(obj, "hr_covered_s"), 0),
        hr_source: HrSource::from_code(json::as_u8_or(json::member(obj, "hr_source"), 0)),
        segmented: json::as_u8_or(json::member(obj, "segmented"), 0) != 0,
        rally_count: json::as_u32_or(json::member(obj, "rally_count"), 0),
        rally_s: json::as_u32_or(json::member(obj, "rally_s"), 0),
        rest_s: json::as_u32_or(json::member(obj, "rest_s"), 0),
        off_court_s: json::as_u32_or(json::member(obj, "off_court_s"), 0),
        windows_dropped: json::as_u16_or(json::member(obj, "windows_dropped"), 0),
        ..SessionRecord::EMPTY
    };
    // The windows first, so add_window's tally is then overwritten by the
    // stored one -- which counts every qualifying window, not just these.
    if let Some(list) = json::member(obj, "windows") {
        for item in json::items(list) {
            s.add_window(parse_window(item));
        }
    }
    if let Some(list) = json::member(obj, "drop_sum") {
        for (i, v) in json::items(list).enumerate().take(s.drop_sum.len()) {
            s.drop_sum[i] = json::as_u32_or(Some(v), 0);
        }
    }
    if let Some(list) = json::member(obj, "drop_n") {
        for (i, v) in json::items(list).enumerate().take(s.drop_n.len()) {
            s.drop_n[i] = json::as_u16_or(Some(v), 0);
        }
    }
    s
}

fn parse_window(obj: &[u8]) -> Recovery {
    Recovery {
        kind: json::as_u8_or(json::member(obj, "kind"), 0),
        at_active_s: json::as_u32_or(json::member(obj, "at_active_s"), 0),
        hr0: json::as_u8_or(json::member(obj, "hr0"), 0),
        hr_end: json::as_u8_or(json::member(obj, "hr_end"), 0),
        window_s: json::as_u8_or(json::member(obj, "window_s"), 0),
        trusted_s: json::as_u8_or(json::member(obj, "trusted_s"), 0),
        hr0_pct_max: json::as_u8_or(json::member(obj, "hr0_pct_max"), 0),
        source: json::as_u8_or(json::member(obj, "source"), 0),
        ..Recovery::EMPTY
    }
}

/// Rounded, not truncated, so the value here and the same value in a `.fit`
/// cannot disagree by a beat the way Spin's two records did on 2026-09-03.
fn hundredths(v: f32) -> u64 {
    // is_finite first, so a NaN reaches the wire as an absent value rather
    // than as a plain 0 no reader could tell from a measured one.
    if !v.is_finite() || v <= 0.0 {
        return 0;
    }
    libm::roundf(v * 100.0) as u64
}

fn from_hundredths(v: u32) -> f32 {
    v as f32 / 100.0
}
