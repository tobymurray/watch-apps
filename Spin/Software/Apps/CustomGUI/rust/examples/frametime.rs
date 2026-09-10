//! Frame time for every scene, and the ring sweep that InscribedDisc's disc
//! clip is measured against. The falsifier named by
//! `inscribed_disc::geometry::lit_span` and by the numbers in this app's
//! README: if the clip regresses, this is what says so.
//!
//!   cargo run --release --features std --example frametime
use std::time::Instant;
use spin_gui::{render, scenes};

fn main() {
    let (w, h) = (240u32, 240u32);
    let mut buf = vec![0u8; (w * h) as usize];
    let all: Vec<_> = scenes::scenes();
    for (_, f) in &all {
        render(&mut buf, w, h, f);
    }

    let reps = 200;
    let t0 = Instant::now();
    for _ in 0..reps {
        for (_, f) in &all {
            render(&mut buf, w, h, f);
        }
    }
    let per = t0.elapsed().as_secs_f64() * 1e6 / (reps as f64 * all.len() as f64);

    let mut worst = ("", 0.0f64);
    for (name, f) in &all {
        let t = Instant::now();
        for _ in 0..reps {
            render(&mut buf, w, h, f);
        }
        let us = t.elapsed().as_secs_f64() * 1e6 / reps as f64;
        if us > worst.1 {
            worst = (name, us);
        }
    }
    println!("mean {:.1} us/frame over {} scenes | worst {} {:.1} us", per, all.len(), worst.0, worst.1);
}
