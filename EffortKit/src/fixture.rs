//! Reads a recording off disk, so a session recorded on court is a host-test
//! fixture and an analyser input without being converted first.
//!
//! Three files share one clock, all begun from the same sensor tick:
//! `imu_<stamp>.csv` carries the samples, `imu_<stamp>_events.csv` the button
//! presses, and `imu_<stamp>_hr.csv` the heart rate. A marker row and a sample
//! row with the same `t_ms` are the same instant, so nothing has to be
//! correlated.
//!
//! Labels come from the markers themselves: each carries the state the wearer
//! selected on the watch, and it holds until the next one that changes it. A
//! recording made before the watch could label carries kind 0 throughout and
//! reads back unlabelled, which is what a hand-written `imu_<stamp>_labels.txt`
//! is for; when that file is present it wins.

use std::fs;
use std::path::{Path, PathBuf};

use crate::epoch::ImuSample;
use crate::hr::{HrSample, HrSource};

/// A button press, on the recording's clock.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Marker {
    /// Milliseconds from the first sample.
    pub t_ms: u32,
    /// 1-based and gap-free, so a truncated sidecar is obvious.
    pub seq: u32,
    /// The state the wearer selected, as `ImuMarkerLog::Kind`; 0 asserts none.
    pub kind: u8,
}

/// What the wearer says they were doing between two markers.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Label {
    /// A rally.
    Rally,
    /// Between rallies, on court.
    Rest,
    /// Off court entirely.
    OffCourt,
    /// The warm-up, which is neither.
    WarmUp,
    /// Continuous hitting with no rests.
    Drill,
    /// Worn but not playing — walking to court, talking, tying a shoe.
    Idle,
    /// Named in a labels file but not one this build knows.
    Unknown,
    /// Nothing named this stretch. Not a state — the absence of one.
    Unlabelled,
}

impl Label {
    /// Parse a label name from the labels file.
    pub fn parse(s: &str) -> Self {
        match s.trim().to_ascii_lowercase().as_str() {
            "rally" => Label::Rally,
            "rest" => Label::Rest,
            "off_court" | "offcourt" => Label::OffCourt,
            "warmup" | "warm_up" => Label::WarmUp,
            "drill" => Label::Drill,
            "idle" => Label::Idle,
            _ => Label::Unknown,
        }
    }

    /// Name, for reports.
    pub const fn name(&self) -> &'static str {
        match self {
            Label::Rally => "rally",
            Label::Rest => "rest",
            Label::OffCourt => "off_court",
            Label::WarmUp => "warmup",
            Label::Drill => "drill",
            Label::Idle => "idle",
            Label::Unknown => "unknown",
            Label::Unlabelled => "unlabelled",
        }
    }
}

/// One labelled stretch of a recording.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Interval {
    /// Inclusive start on the recording's clock, ms.
    pub start_ms: u32,
    /// Exclusive end, ms.
    pub end_ms: u32,
    /// What the wearer says was happening.
    pub label: Label,
}

/// One session's files, read into memory.
#[derive(Clone, Debug, Default)]
pub struct Recording {
    /// Where it was read from.
    pub path: PathBuf,
    /// Samples in order, each with its millisecond.
    pub samples: Vec<(u32, ImuSample)>,
    /// Button presses, if a sidecar was present.
    pub markers: Vec<Marker>,
    /// Heart rate, if a sidecar was present.
    pub hr: Vec<HrSample>,
    /// Labelled stretches, from the markers or from a labels file; empty when
    /// nothing in the recording named a state.
    pub intervals: Vec<Interval>,
    /// What went wrong that did not stop the read.
    pub warnings: Vec<String>,
}

impl Recording {
    /// Milliseconds from the first sample to the last.
    pub fn duration_ms(&self) -> u32 {
        match (self.samples.first(), self.samples.last()) {
            (Some((a, _)), Some((b, _))) => b - a,
            _ => 0,
        }
    }

    /// The label covering an instant, if anything named one.
    pub fn label_at(&self, t_ms: u32) -> Option<Label> {
        self.intervals
            .iter()
            .find(|i| t_ms >= i.start_ms && t_ms < i.end_ms)
            .map(|i| i.label)
    }
}

fn sidecar(path: &Path, suffix: &str) -> PathBuf {
    let stem = path.file_stem().and_then(|s| s.to_str()).unwrap_or("imu");
    path.with_file_name(format!("{stem}{suffix}"))
}

/// Read `imu_<stamp>.csv` and whichever of its sidecars are present.
///
/// A missing sidecar is not an error — a recording taken without a strap has no
/// heart-rate file and one taken without a marker log has no labels — but a
/// malformed row is reported in [`Recording::warnings`] rather than skipped
/// silently, since a parser that quietly drops rows makes a short recording
/// look like a quiet one.
pub fn load(path: impl AsRef<Path>) -> std::io::Result<Recording> {
    let path = path.as_ref().to_path_buf();
    let mut out = Recording { path: path.clone(), ..Recording::default() };

    let text = fs::read_to_string(&path)?;
    for (n, line) in text.lines().enumerate() {
        if line.is_empty() || line.starts_with("t_ms") {
            continue;
        }
        match parse_sample(line) {
            Some(s) => out.samples.push(s),
            None => out.warnings.push(format!("{}:{}: not a sample row", path.display(), n + 1)),
        }
    }

    let events = sidecar(&path, "_events.csv");
    if events.exists() {
        for (n, line) in fs::read_to_string(&events)?.lines().enumerate() {
            if line.is_empty() || line.starts_with("t_ms") {
                continue;
            }
            match parse_marker(line) {
                Some(m) => out.markers.push(m),
                None => {
                    out.warnings.push(format!("{}:{}: not a marker row", events.display(), n + 1))
                }
            }
        }
        for (i, m) in out.markers.iter().enumerate() {
            if m.seq != i as u32 + 1 {
                out.warnings.push(format!(
                    "{}: marker {} has seq {}, so the sidecar is truncated or reordered",
                    events.display(),
                    i + 1,
                    m.seq
                ));
                break;
            }
        }
    }

    let hr = sidecar(&path, "_hr.csv");
    if hr.exists() {
        for (n, line) in fs::read_to_string(&hr)?.lines().enumerate() {
            if line.is_empty() || line.starts_with("t_ms") {
                continue;
            }
            match parse_hr(line) {
                Some(s) => out.hr.push(s),
                None => out.warnings.push(format!("{}:{}: not a heart-rate row", hr.display(), n + 1)),
            }
        }
    }

    let labels = sidecar(&path, "_labels.txt");
    if labels.exists() {
        // A hand-written file wins: it is the only way to label a recording
        // made before the watch could, and the only way to correct one.
        let (intervals, mut warnings) =
            parse_labels(&fs::read_to_string(&labels)?, &out.markers, out.duration_ms());
        out.intervals = intervals;
        out.warnings.append(&mut warnings);
    } else {
        out.intervals = intervals_from_markers(&out.markers, out.duration_ms());
    }

    Ok(out)
}

/// What a marker's `kind` column says the wearer was doing from that instant.
///
/// FROZEN WIRE FORMAT, shared with `ImuMarkerLog::Kind`. Kind 0 is a marker
/// that asserts no state, which is what every recording made before the watch
/// could label carries, so those read back as unlabelled rather than as
/// rallies.
fn label_from_kind(kind: u8) -> Option<Label> {
    match kind {
        1 => Some(Label::Rally),
        2 => Some(Label::Rest),
        3 => Some(Label::OffCourt),
        4 => Some(Label::WarmUp),
        5 => Some(Label::Drill),
        6 => Some(Label::Idle),
        _ => None,
    }
}

/// Intervals from the markers alone, for a recording the watch labelled itself.
///
/// A label holds from its marker until the next one that carries a different
/// state, so the wearer presses once per change rather than once per stretch.
/// A kind-0 marker is "note this instant" and does not end the stretch it sits
/// in: it is a bookmark, not a boundary.
fn intervals_from_markers(markers: &[Marker], duration_ms: u32) -> Vec<Interval> {
    let mut intervals: Vec<Interval> = Vec::new();
    let mut start = 0u32;
    let mut current = Label::Unlabelled;

    for m in markers {
        let Some(label) = label_from_kind(m.kind) else {
            continue;
        };
        if label == current {
            continue;
        }
        if m.t_ms > start {
            intervals.push(Interval { start_ms: start, end_ms: m.t_ms, label: current });
        }
        start = m.t_ms;
        current = label;
    }

    if duration_ms > start {
        intervals.push(Interval { start_ms: start, end_ms: duration_ms, label: current });
    }

    // Nothing was ever labelled, so say so by having no intervals rather than
    // by having one that covers everything with the absence of a label.
    if intervals.iter().all(|i| i.label == Label::Unlabelled) {
        return Vec::new();
    }
    intervals
}

fn parse_sample(line: &str) -> Option<(u32, ImuSample)> {
    let mut f = line.split(',');
    let t = f.next()?.trim().parse().ok()?;
    let mut next = || f.next()?.trim().parse::<i16>().ok();
    Some((
        t,
        ImuSample {
            ax: next()?,
            ay: next()?,
            az: next()?,
            gx: next()?,
            gy: next()?,
            gz: next()?,
        },
    ))
}

fn parse_marker(line: &str) -> Option<Marker> {
    let mut f = line.split(',');
    Some(Marker {
        t_ms: f.next()?.trim().parse().ok()?,
        seq: f.next()?.trim().parse().ok()?,
        kind: f.next().unwrap_or("0").trim().parse().unwrap_or(0),
    })
}

/// `t_ms,bpm_x100,trust,source,optical_x100,external_x100`, as `HrCsvLog` writes it.
///
/// Hundredths rather than a decimal because the watch has no float formatter,
/// and the sub-bpm steps they preserve are the point of the file.
fn parse_hr(line: &str) -> Option<HrSample> {
    let mut f = line.split(',');
    let t_ms = f.next()?.trim().parse().ok()?;
    let bpm = f.next()?.trim().parse::<i32>().ok()? as f32 / 100.0;
    let trust = f.next().unwrap_or("0").trim().parse().unwrap_or(0);
    let source = match f.next().unwrap_or("0").trim() {
        "1" => HrSource::Optical,
        "2" => HrSource::External,
        _ => HrSource::Unknown,
    };
    Some(HrSample { t_ms, bpm, trust, source })
}

/// Turn the labels file into intervals between markers.
///
/// Markers delimit; the labels file says what each delimited stretch was. The
/// first interval runs from the start of the recording to the first marker, so
/// N markers give N+1 intervals.
fn parse_labels(text: &str, markers: &[Marker], duration_ms: u32) -> (Vec<Interval>, Vec<String>) {
    let mut names: Vec<Label> = Vec::new();
    let mut alternate: Option<(Label, Label)> = None;
    let mut warnings = Vec::new();

    for line in text.lines() {
        let line = line.split('#').next().unwrap_or("").trim();
        if line.is_empty() {
            continue;
        }
        if let Some(rest) = line.strip_prefix("alternate") {
            let parts: Vec<&str> = rest.split_whitespace().collect();
            if parts.len() == 2 {
                alternate = Some((Label::parse(parts[0]), Label::parse(parts[1])));
            } else {
                warnings.push(format!("labels: `alternate` needs two names, got `{line}`"));
            }
            continue;
        }
        let label = Label::parse(line);
        if label == Label::Unknown {
            warnings.push(format!("labels: `{line}` is not a state this build knows"));
        }
        names.push(label);
    }

    let bounds: Vec<u32> = core::iter::once(0)
        .chain(markers.iter().map(|m| m.t_ms))
        .chain(core::iter::once(duration_ms))
        .collect();

    let mut intervals = Vec::new();
    for (i, w) in bounds.windows(2).enumerate() {
        let label = match names.get(i) {
            Some(l) => *l,
            None => match alternate {
                Some((a, b)) => {
                    let phase = (i - names.len()) % 2;
                    if phase == 0 {
                        a
                    } else {
                        b
                    }
                }
                None => {
                    if i == 0 {
                        warnings.push("labels: no names and no `alternate` line".into());
                    }
                    Label::Unknown
                }
            },
        };
        if w[1] > w[0] {
            intervals.push(Interval { start_ms: w[0], end_ms: w[1], label });
        }
    }
    (intervals, warnings)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_heart_rate_row_keeps_its_hundredths() {
        let s = parse_hr("1000,14268,2,2,14100,14268").unwrap();
        assert_eq!(s.t_ms, 1000);
        assert_eq!(s.bpm, 142.68);
        assert_eq!(s.trust, 2);
        assert_eq!(s.source, HrSource::External);
    }

    #[test]
    fn a_sample_row_parses_into_raw_lsb() {
        let (t, s) = parse_sample("10,105,-203,4098,318,-411,502").unwrap();
        assert_eq!(t, 10);
        assert_eq!(s.ax, 105);
        assert_eq!(s.gz, 502);
    }

    #[test]
    fn labels_fill_the_stretches_between_markers() {
        let markers = [Marker { t_ms: 1000, seq: 1, kind: 0 }, Marker { t_ms: 2000, seq: 2, kind: 0 }];
        let (iv, w) = parse_labels("warmup\nrally\nrest\n", &markers, 3000);
        assert!(w.is_empty());
        assert_eq!(iv.len(), 3);
        assert_eq!(iv[0], Interval { start_ms: 0, end_ms: 1000, label: Label::WarmUp });
        assert_eq!(iv[2], Interval { start_ms: 2000, end_ms: 3000, label: Label::Rest });
    }

    #[test]
    fn alternate_fills_the_rest_of_a_match() {
        let markers: Vec<Marker> =
            (1..=4).map(|i| Marker { t_ms: i * 1000, seq: i, kind: 0 }).collect();
        let (iv, w) = parse_labels("warmup\nalternate rally rest\n", &markers, 5000);
        assert!(w.is_empty(), "{w:?}");
        let got: Vec<Label> = iv.iter().map(|i| i.label).collect();
        assert_eq!(got, vec![Label::WarmUp, Label::Rally, Label::Rest, Label::Rally, Label::Rest]);
    }

    #[test]
    fn an_unknown_state_name_is_reported_rather_than_guessed() {
        let (iv, w) = parse_labels("wibble\n", &[], 1000);
        assert_eq!(iv[0].label, Label::Unknown);
        assert_eq!(w.len(), 1);
    }

    #[test]
    fn the_watch_s_own_markers_label_a_recording_with_no_labels_file() {
        // rally at 0, rest at 2 s, rally again at 5 s.
        let markers = [
            Marker { t_ms: 0, seq: 1, kind: 1 },
            Marker { t_ms: 2000, seq: 2, kind: 2 },
            Marker { t_ms: 5000, seq: 3, kind: 1 },
        ];
        let iv = intervals_from_markers(&markers, 7000);
        let got: Vec<(u32, u32, Label)> =
            iv.iter().map(|i| (i.start_ms, i.end_ms, i.label)).collect();
        assert_eq!(
            got,
            vec![
                (0, 2000, Label::Rally),
                (2000, 5000, Label::Rest),
                (5000, 7000, Label::Rally),
            ]
        );
    }

    #[test]
    fn a_plain_marker_is_a_bookmark_and_does_not_end_the_stretch_it_sits_in() {
        let markers = [
            Marker { t_ms: 0, seq: 1, kind: 1 },
            Marker { t_ms: 1000, seq: 2, kind: 0 },
            Marker { t_ms: 3000, seq: 3, kind: 2 },
        ];
        let iv = intervals_from_markers(&markers, 4000);
        assert_eq!(iv.len(), 2);
        assert_eq!(iv[0], Interval { start_ms: 0, end_ms: 3000, label: Label::Rally });
        assert_eq!(iv[1], Interval { start_ms: 3000, end_ms: 4000, label: Label::Rest });
    }

    #[test]
    fn a_stretch_before_the_first_label_is_unlabelled_rather_than_guessed() {
        let markers = [Marker { t_ms: 2000, seq: 1, kind: 5 }];
        let iv = intervals_from_markers(&markers, 4000);
        assert_eq!(iv[0], Interval { start_ms: 0, end_ms: 2000, label: Label::Unlabelled });
        assert_eq!(iv[1], Interval { start_ms: 2000, end_ms: 4000, label: Label::Drill });
    }

    #[test]
    fn recordings_made_before_the_watch_could_label_stay_unlabelled() {
        // Every marker in Tests/pulled carries kind 0.
        let markers = [
            Marker { t_ms: 266240, seq: 1, kind: 0 },
            Marker { t_ms: 830979, seq: 2, kind: 0 },
        ];
        assert!(intervals_from_markers(&markers, 1_800_000).is_empty());
        assert!(intervals_from_markers(&[], 1_800_000).is_empty());
    }

    #[test]
    fn holding_a_label_across_a_repeat_press_does_not_split_the_stretch() {
        let markers = [
            Marker { t_ms: 0, seq: 1, kind: 1 },
            Marker { t_ms: 1000, seq: 2, kind: 1 },
        ];
        let iv = intervals_from_markers(&markers, 2000);
        assert_eq!(iv, vec![Interval { start_ms: 0, end_ms: 2000, label: Label::Rally }]);
    }

    #[test]
    fn a_recording_with_no_labels_file_warns_rather_than_inventing_one() {
        let (iv, w) = parse_labels("", &[Marker { t_ms: 500, seq: 1, kind: 0 }], 1000);
        assert!(iv.iter().all(|i| i.label == Label::Unknown));
        assert_eq!(w.len(), 1);
    }
}
