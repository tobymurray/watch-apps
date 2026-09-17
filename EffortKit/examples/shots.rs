//! Shots per labelled stretch, for choosing a threshold against real play.
//!
//!   cargo run --features std --example shots -- <imu_*.csv> [...]
use effortkit::fixture::{self, Label};
use effortkit::shot::{Config, Detector};

fn main() {
    let paths: Vec<String> = std::env::args().skip(1).collect();
    println!("{:<24}{:>10}{:>8}{:>9}{:>10}", "stretch", "gyro dps", "shots", "seconds", "per min");
    for enter in [12_000u32, 16_000, 20_000] {
        let cfg = Config::new(enter, enter / 2, 300).expect("hysteresis");
        for p in &paths {
            let Ok(r) = fixture::load(p) else { continue };
            let mut per: Vec<(Label, u32, u32)> = Vec::new();
            for iv in &r.intervals {
                if !matches!(iv.label, Label::Rally | Label::Game | Label::WarmUp | Label::Drill) {
                    continue;
                }
                let mut d = Detector::new(cfg);
                for (t, s) in r.samples.iter() {
                    if *t >= iv.start_ms && *t < iv.end_ms {
                        d.push(*t, s);
                    }
                }
                per.push((iv.label, d.count(), (iv.end_ms - iv.start_ms) / 1000));
            }
            let shots: u32 = per.iter().map(|x| x.1).sum();
            let secs: u32 = per.iter().map(|x| x.2).sum();
            if secs < 60 { continue; }
            let name = std::path::Path::new(p).file_stem().unwrap().to_string_lossy();
            println!("{:<24}{:>10}{:>8}{:>9}{:>10.1}",
                     &name[4..], enter / 164 * 10, shots, secs, shots as f32 * 60.0 / secs as f32);
        }
    }
}
