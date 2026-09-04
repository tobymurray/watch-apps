//! Phase A: measure what the signals are doing before modelling them.
//!
//! Two questions, and a feature is not buildable until its answer exists.
//! A1 asks what the heart-rate signal is actually doing — its update cadence,
//! its quantisation, and how long it takes to respond when effort changes — and
//! if its own time constant is comparable to the rests recovery would be
//! measured over, then recovery as a slope is not measurable and the tool says
//! so. A2 asks whether the movement states are separable at all, and answers it
//! with distributions and an overlap rather than a threshold.
//!
//! Usage: `phase-a <imu_*.csv> [...]`, optionally `--report <out.md>` and
//! `--epochs <out.csv>`. Both write files: a report that exists only in a
//! terminal's scrollback is a report nobody has when it matters.

use std::collections::BTreeMap;
use std::env;
use std::io::Write;
use std::process::ExitCode;
use std::sync::Mutex;

use effortkit::epoch::{EpochAccumulator, EpochFeatures, Feature, ALL_FEATURES, EPOCH_MS};
use effortkit::fixture::{self, Label, Recording};
use effortkit::hr::{HrSample, HrSource};

/// Everything `say!` has emitted, so the report can be written to a file as
/// well as to the terminal.
static REPORT: Mutex<String> = Mutex::new(String::new());

/// Print a line and keep it for the report file.
macro_rules! say {
    () => { say!("") };
    ($($arg:tt)*) => {{
        let line = format!($($arg)*);
        println!("{line}");
        if let Ok(mut r) = REPORT.lock() {
            r.push_str(&line);
            r.push('\n');
        }
    }};
}

fn main() -> ExitCode {
    let args: Vec<String> = env::args().skip(1).collect();
    let mut paths = Vec::new();
    let mut epoch_csv = None;
    let mut report_md = None;
    let mut sidecars = 0usize;
    let mut it = args.iter();
    while let Some(a) = it.next() {
        match a.as_str() {
            "--epochs" => epoch_csv = it.next().cloned(),
            "--report" => report_md = it.next().cloned(),
            "-h" | "--help" => {
                eprintln!("usage: phase-a <imu_*.csv> [...] [--report out.md] [--epochs out.csv]");
                return ExitCode::SUCCESS;
            }
            // `imu_*.csv` is the natural glob and it catches the sidecars too.
            // They are not recordings and `fixture::load` finds them by name
            // anyway, so they are dropped here rather than parsed as samples --
            // which produced one "not a sample row" per line and buried the
            // report.
            _ if is_sidecar(a) => sidecars += 1,
            _ => paths.push(a.clone()),
        }
    }

    if paths.is_empty() && sidecars > 0 {
        eprintln!(
            "every path given was a sidecar; pass the recordings themselves, and their \
             sidecars are picked up by name"
        );
        return ExitCode::FAILURE;
    }

    if paths.is_empty() {
        say!("# Phase A\n");
        say!("No recordings given, and there are none in this repository.\n");
        say!(
            "Nothing in Phase A can be answered without them, so no threshold can be set and\n\
             nothing can be displayed. `Squash/Docs/RECORDING-PROTOCOL.md` says which recordings\n\
             are needed and what each one settles."
        );
        return ExitCode::FAILURE;
    }

    if sidecars > 0 {
        eprintln!("skipped {sidecars} sidecar(s); they are read from the recordings they belong to");
    }

    let mut recordings = Vec::new();
    for p in &paths {
        match fixture::load(p) {
            Ok(r) => recordings.push(r),
            Err(e) => {
                eprintln!("{p}: {e}");
                return ExitCode::FAILURE;
            }
        }
    }

    say!("# Phase A measurements\n");
    inventory(&recordings);
    a1(&recordings);
    let labelled = a2(&recordings);

    if let Some(path) = epoch_csv {
        if let Err(e) = write_epochs(&path, &labelled) {
            eprintln!("{path}: {e}");
            return ExitCode::FAILURE;
        }
        eprintln!("wrote {} epochs to {path}", labelled.len());
    }

    if let Some(path) = report_md {
        let text = REPORT.lock().map(|r| r.clone()).unwrap_or_default();
        match std::fs::File::create(&path).and_then(|mut f| f.write_all(text.as_bytes())) {
            Ok(()) => eprintln!("wrote the report to {path}"),
            Err(e) => {
                eprintln!("{path}: {e}");
                return ExitCode::FAILURE;
            }
        }
    }
    ExitCode::SUCCESS
}

/// True for a file `fixture::load` finds on its own from a recording's name.
fn is_sidecar(path: &str) -> bool {
    ["_events.csv", "_hr.csv", "_labels.txt"].iter().any(|s| path.ends_with(s))
}

fn inventory(rs: &[Recording]) {
    say!("## Recordings\n");
    say!("| File | Duration | Samples | Markers | HR samples | Labelled |");
    say!("| --- | --- | --- | --- | --- | --- |");
    for r in rs {
        say!(
            "| `{}` | {} | {} | {} | {} | {} |",
            r.path.file_name().unwrap_or_default().to_string_lossy(),
            hms(r.duration_ms()),
            r.samples.len(),
            r.markers.len(),
            r.hr.len(),
            if r.intervals.is_empty() { "no" } else { "yes" },
        );
    }
    say!();
    let warnings: Vec<&String> = rs.iter().flat_map(|r| r.warnings.iter()).collect();
    if !warnings.is_empty() {
        // Capped: a wholly unreadable sidecar warns once per row, and a report
        // that is ten thousand identical lines is one nobody reads to the end.
        const SHOWN: usize = 15;
        say!("### Warnings\n");
        for w in warnings.iter().take(SHOWN) {
            say!("- {w}");
        }
        if warnings.len() > SHOWN {
            say!("- ...and {} more of the same shape.", warnings.len() - SHOWN);
        }
        say!();
    }
}

// ---------------------------------------------------------------- A1

fn a1(rs: &[Recording]) {
    say!("## A1 — what the heart-rate signal is doing\n");
    let hr: Vec<&HrSample> = rs.iter().flat_map(|r| r.hr.iter()).collect();
    if hr.is_empty() {
        say!(
            "No heart-rate sidecar in any recording, so none of A1 is answered. Recovery is not\n\
             buildable until it is: without the update cadence and the settling time there is no\n\
             way to tell a physiological fall from the kernel's own filter.\n"
        );
        return;
    }

    say!("### Update cadence\n");
    let mut gaps: Vec<u32> = Vec::new();
    for r in rs {
        for w in r.hr.windows(2) {
            gaps.push(w[1].t_ms.saturating_sub(w[0].t_ms));
        }
    }
    let mut hist: BTreeMap<u32, usize> = BTreeMap::new();
    for g in &gaps {
        *hist.entry(g / 100 * 100).or_default() += 1;
    }
    say!("| Gap (ms, 100 ms buckets) | Count |");
    say!("| --- | --- |");
    for (k, v) in hist.iter().take(12) {
        say!("| {k} | {v} |");
    }
    say!("\nNominal is 1000 ms. {} gaps measured.\n", gaps.len());

    say!("### Quantisation\n");
    let mut steps: Vec<f32> = Vec::new();
    for r in rs {
        for w in r.hr.windows(2) {
            let d = (w[1].bpm - w[0].bpm).abs();
            if d > 0.0 {
                steps.push(d);
            }
        }
    }
    steps.sort_by(|a, b| a.partial_cmp(b).unwrap());
    if steps.is_empty() {
        say!("Every consecutive pair was identical, which is itself a finding.\n");
    } else {
        let sub_bpm = steps.iter().filter(|s| **s < 1.0).count();
        say!("| Smallest non-zero step | Median step | Steps below 1 bpm |");
        say!("| --- | --- | --- |");
        say!(
            "| {:.4} bpm | {:.4} bpm | {} of {} ({:.0}%) |",
            steps[0],
            steps[steps.len() / 2],
            sub_bpm,
            steps.len(),
            100.0 * sub_bpm as f32 / steps.len() as f32
        );
        say!(
            "\nA raw beat-derived rate cannot step by a fraction of a bpm at 1 Hz. A high\n\
             proportion below 1 bpm is the kernel's own smoothing, and is what makes a fitted\n\
             slope over a rest a measurement of the filter rather than of the wearer.\n"
        );
    }

    say!("### Step response at labelled effort transitions\n");
    let mut taus: Vec<(HrSource, f32, f32)> = Vec::new();
    for r in rs {
        for iv in &r.intervals {
            let effortful = matches!(iv.label, Label::Rally | Label::Drill);
            if !effortful {
                continue;
            }
            if let Some(t) = settle(&r.hr, iv.end_ms) {
                taus.push(t);
            }
        }
    }
    if taus.is_empty() {
        say!(
            "No labelled transition out of effort carried enough heart rate to measure across,\n\
             so the settling time is unmeasured and recovery stays unbuildable.\n"
        );
        return;
    }
    for src in [HrSource::External, HrSource::Optical, HrSource::Unknown] {
        let mut lags: Vec<f32> = taus.iter().filter(|t| t.0 == src).map(|t| t.1).collect();
        let mut ts: Vec<f32> = taus.iter().filter(|t| t.0 == src).map(|t| t.2).collect();
        if ts.is_empty() {
            continue;
        }
        lags.sort_by(|a, b| a.partial_cmp(b).unwrap());
        ts.sort_by(|a, b| a.partial_cmp(b).unwrap());
        say!(
            "- **{:?}**: {} transitions, median lag {:.1} s, median time constant {:.1} s.",
            src,
            ts.len(),
            lags[lags.len() / 2],
            ts[ts.len() / 2]
        );
    }
    say!(
        "\n**The verdict A1 owes Phase C**: compare the median time constant with the rest\n\
         windows recovery would be measured over. If they are comparable, a fall measured\n\
         across one rest is the filter settling and `Formulation::NotMeasurableOnThisHardware`\n\
         is the honest calibration.\n"
    );
}

/// Lag and time constant of the fall after effort ended at `t_ms`.
///
/// Lag is the time until the rate has moved at all; the time constant is the
/// time to fall 63.2% of the way to the lowest value in the two minutes that
/// follow, which is the exponential's own definition without fitting one.
fn settle(hr: &[HrSample], t_ms: u32) -> Option<(HrSource, f32, f32)> {
    const HORIZON_MS: u32 = 120_000;
    let after: Vec<&HrSample> =
        hr.iter().filter(|s| s.t_ms >= t_ms && s.t_ms <= t_ms + HORIZON_MS && s.trust > 0).collect();
    if after.len() < 10 {
        return None;
    }
    let start = after[0];
    let floor = after.iter().map(|s| s.bpm).fold(f32::INFINITY, f32::min);
    let fall = start.bpm - floor;
    if fall < 5.0 {
        return None;
    }
    let target = start.bpm - 0.632 * fall;
    let mut lag = None;
    let mut tau = None;
    for s in &after {
        if lag.is_none() && (start.bpm - s.bpm).abs() > 0.5 {
            lag = Some((s.t_ms - t_ms) as f32 / 1000.0);
        }
        if tau.is_none() && s.bpm <= target {
            tau = Some((s.t_ms - t_ms) as f32 / 1000.0);
        }
    }
    Some((start.source, lag.unwrap_or(0.0), tau?))
}

// ---------------------------------------------------------------- A2

/// One epoch with the label the wearer gave the stretch it fell in.
struct Labelled {
    file: String,
    features: EpochFeatures,
    label: Label,
}

fn a2(rs: &[Recording]) -> Vec<Labelled> {
    say!("## A2 — are the movement states separable\n");
    let mut out = Vec::new();
    for r in rs {
        let file = r.path.file_name().unwrap_or_default().to_string_lossy().to_string();
        let mut acc = EpochAccumulator::new();
        let push = |e: EpochFeatures, out: &mut Vec<Labelled>| {
            // An epoch is labelled by its midpoint, so one straddling a marker
            // is attributed to whichever state held it longer. An epoch with no
            // label is still kept and still dumped: reading the features is how
            // a recording gets inspected *before* it is labelled, and throwing
            // them away made --epochs write an empty file for every recording
            // that had no labels file.
            let mid = e.start_ms() + EPOCH_MS / 2;
            let label = r.label_at(mid).unwrap_or(Label::Unlabelled);
            out.push(Labelled { file: file.clone(), features: e, label });
        };
        for (t, s) in &r.samples {
            if let Some(e) = acc.push(*t, s) {
                push(e, &mut out);
            }
        }
        if let Some(e) = acc.flush() {
            push(e, &mut out);
        }
    }

    if labelled_count(&out) == 0 {
        say!("{} epochs computed, none labelled.\n", out.len());
        say!(
            "No labelled epochs. Either no recording carried a labels file, or none of its\n\
             stretches named a state this build knows. Nothing in A2 is answered, so the\n\
             segmenter stays uncalibrated.\n"
        );
        return out;
    }

    let labels: Vec<Label> =
        present_labels(&out).into_iter().filter(|l| *l != Label::Unlabelled).collect();
    say!("### Distributions\n");
    for f in ALL_FEATURES {
        say!("**{}**\n", f.name());
        say!("| State | n | min | p5 | p25 | median | p75 | p95 | max |");
        say!("| --- | --- | --- | --- | --- | --- | --- | --- | --- |");
        for l in &labels {
            let v = values(&out, *l, f);
            if v.is_empty() {
                continue;
            }
            say!(
                "| {} | {} | {:.1} | {:.1} | {:.1} | {:.1} | {:.1} | {:.1} | {:.1} |",
                l.name(),
                v.len(),
                pct(&v, 0.0),
                pct(&v, 0.05),
                pct(&v, 0.25),
                pct(&v, 0.5),
                pct(&v, 0.75),
                pct(&v, 0.95),
                pct(&v, 1.0)
            );
        }
        say!();
    }

    say!("### Overlap between the pairs the segmenter has to separate\n");
    say!("| Pair | Feature | Best threshold | Error rate | Overlap |");
    say!("| --- | --- | --- | --- | --- |");
    for (a, b) in [(Label::Rally, Label::Rest), (Label::Rest, Label::OffCourt), (Label::Rally, Label::Drill)] {
        for f in ALL_FEATURES {
            let va = values(&out, a, f);
            let vb = values(&out, b, f);
            if va.len() < 10 || vb.len() < 10 {
                continue;
            }
            let (thr, err) = best_split(&va, &vb);
            say!(
                "| {} vs {} | {} | {:.1} | {:.1}% | {:.1}% |",
                a.name(),
                b.name(),
                f.name(),
                thr,
                err * 100.0,
                overlap(&va, &vb) * 100.0
            );
        }
    }
    say!(
        "\nA feature whose error rate at its own best single threshold is high has not\n\
         separated the two states, and a hysteresis machine over it will not either. That is a\n\
         result, not a failure: it says find a better feature before proceeding.\n"
    );
    out
}

fn labelled_count(v: &[Labelled]) -> usize {
    v.iter().filter(|l| l.label != Label::Unlabelled).count()
}

fn present_labels(v: &[Labelled]) -> Vec<Label> {
    let mut seen = Vec::new();
    for l in v {
        if !seen.contains(&l.label) {
            seen.push(l.label);
        }
    }
    seen
}

fn values(v: &[Labelled], label: Label, f: Feature) -> Vec<f32> {
    let mut out: Vec<f32> =
        v.iter().filter(|l| l.label == label).map(|l| f.of(&l.features)).collect();
    out.sort_by(|a, b| a.partial_cmp(b).unwrap());
    out
}

fn pct(sorted: &[f32], p: f32) -> f32 {
    if sorted.is_empty() {
        return f32::NAN;
    }
    let i = ((sorted.len() - 1) as f32 * p).round() as usize;
    sorted[i]
}

/// The single threshold that misclassifies the fewest epochs, and its balanced
/// error rate.
fn best_split(low: &[f32], high: &[f32]) -> (f32, f32) {
    let mut candidates: Vec<f32> = low.iter().chain(high.iter()).copied().collect();
    candidates.sort_by(|a, b| a.partial_cmp(b).unwrap());
    candidates.dedup();
    // A constant feature offers no split at all, which is a coin flip rather
    // than a total failure.
    let mut best = (f32::NAN, 0.5f32);
    // Midpoints between observed values, not the values themselves: a threshold
    // sitting exactly on one class's largest sample is not one to adopt.
    let candidates: Vec<f32> =
        candidates.windows(2).map(|w| (w[0] + w[1]) / 2.0).collect();
    for t in candidates {
        // `high` is whichever class sits above; try both directions.
        for flip in [false, true] {
            let (a, b) = if flip { (high, low) } else { (low, high) };
            let wrong_a = a.iter().filter(|v| **v > t).count() as f32 / a.len() as f32;
            let wrong_b = b.iter().filter(|v| **v <= t).count() as f32 / b.len() as f32;
            let err = (wrong_a + wrong_b) / 2.0;
            if err < best.1 {
                best = (t, err);
            }
        }
    }
    best
}

/// Fraction of the two distributions that share the same range, by histogram.
fn overlap(a: &[f32], b: &[f32]) -> f32 {
    let lo = a[0].min(b[0]);
    let hi = a[a.len() - 1].max(b[b.len() - 1]);
    // Negated so a NaN range reports total overlap rather than dividing by it.
    #[allow(clippy::neg_cmp_op_on_partial_ord)]
    let degenerate = !(hi > lo);
    if degenerate {
        return 1.0;
    }
    const BINS: usize = 64;
    let mut ha = [0f32; BINS];
    let mut hb = [0f32; BINS];
    let bin = |v: f32| (((v - lo) / (hi - lo) * (BINS - 1) as f32) as usize).min(BINS - 1);
    for v in a {
        ha[bin(*v)] += 1.0 / a.len() as f32;
    }
    for v in b {
        hb[bin(*v)] += 1.0 / b.len() as f32;
    }
    ha.iter().zip(hb.iter()).map(|(x, y)| x.min(*y)).sum()
}

fn write_epochs(path: &str, v: &[Labelled]) -> std::io::Result<()> {
    use std::io::Write;
    let mut f = std::fs::File::create(path)?;
    writeln!(f, "file,epoch,start_ms,samples,label,accel_var,accel_jerk,gyro_mean,gyro_max,accel_sat,gyro_sat")?;
    for l in v {
        let e = &l.features;
        writeln!(
            f,
            "{},{},{},{},{},{:.3},{:.3},{:.3},{:.3},{:.4},{:.4}",
            l.file,
            e.index,
            e.start_ms(),
            e.samples,
            l.label.name(),
            e.accel_mag_var,
            e.accel_jerk_mean,
            e.gyro_mag_mean,
            e.gyro_mag_max,
            e.accel_sat_frac,
            e.gyro_sat_frac
        )?;
    }
    Ok(())
}

fn hms(ms: u32) -> String {
    let s = ms / 1000;
    format!("{}h{:02}m{:02}s", s / 3600, (s % 3600) / 60, s % 60)
}
