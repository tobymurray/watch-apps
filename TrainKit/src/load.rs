//! The training-load arithmetic that this data can honestly support, and
//! nothing else.
//!
//! EDWARDS' TRIMP is the summated heart-rate-zone score from Edwards S, "The
//! Heart Rate Monitor Book", Polar Electro Oy, 1993: minutes in each of five
//! zones times a weight of 1..5, summed. Two independent sources agree on both
//! the zone bounds (50-60, 60-70, 70-80, 80-90, 90-100% of maximum heart rate)
//! and the weights.
//!
//! The formula is defined for *those five zones* and no others, so
//! `edwards_trimp` returns nothing for a ladder that is not them. An app whose
//! zones are configurable from two to eight can produce a ladder Edwards never
//! wrote weights for, and inventing weights for it would be a training model
//! invented here wearing a citation.
//!
//! BANISTER'S TRIMP is not here. It needs resting heart rate, which no watch
//! app in this repository has, and sex, which none knows; approximating both
//! and calling the result Banister's would be two guesses in a formula whose
//! whole claim is physiological grounding.
//!
//! NOTHING HERE IS ADVICE. These are arithmetic on what was recorded. What they
//! cannot tell a wearer is in `README.md`, and it is the part that keeps the
//! rest worth having.

use crate::record::{Session, MAX_ZONES};

/// Edwards' zone weights, 1 through 5.
const EDWARDS_WEIGHTS: [u32; 5] = [1, 2, 3, 4, 5];

/// The percentage of maximum heart rate each Edwards zone starts at.
const EDWARDS_FLOORS_PCT: [u32; 5] = [50, 60, 70, 80, 90];

/// A floor may sit this far from the percentage and still be that zone.
///
/// The floors reach this code as integer bpm that somebody rounded, so an exact
/// comparison would reject a ladder that is Edwards' ladder over one bpm.
const FLOOR_TOLERANCE_BPM: i32 = 1;

/// True when this ladder is the one Edwards' weights are defined for.
///
/// The watch's own default ladder is 50/60/70/80/90/100% of maximum, so a
/// five-zone ride that left the floors alone passes this; a three-zone
/// polarised split or a hand-entered ladder does not.
pub fn is_edwards_ladder(zone_floor: &[u8; MAX_ZONES], zone_count: u8, max_hr: u8) -> bool {
    if zone_count != 5 || max_hr == 0 {
        return false;
    }
    for (i, pct) in EDWARDS_FLOORS_PCT.iter().enumerate() {
        let want = ((max_hr as u32) * pct + 50) / 100;
        let diff = (zone_floor[i] as i32) - (want as i32);
        if diff.abs() > FLOOR_TOLERANCE_BPM {
            return false;
        }
    }
    true
}

/// Edwards' TRIMP for a session, in minute-weights, rounded.
///
/// `None` when the ladder is not the one the weights are defined for. Bucket
/// `[0]` is time below zone 1, which Edwards gives no weight, so it contributes
/// nothing rather than contributing one.
pub fn edwards_trimp(session: &Session) -> Option<u32> {
    if !is_edwards_ladder(
        &session.zone_floor,
        session.zone_count,
        session.hr_max_setting,
    ) {
        return None;
    }
    let mut second_weights: u32 = 0;
    for (i, w) in EDWARDS_WEIGHTS.iter().enumerate() {
        second_weights += (session.zone_s[i + 1] as u32) * w;
    }
    Some((second_weights + 30) / 60)
}

/// Average mechanical power, W, from the work the wearer read off the console.
///
/// 0 when nobody said, matching the app's rule that absent work is a normal
/// ride. The same arithmetic and the same divisor the FIT file's `avg_power`
/// uses, so the two can never disagree.
pub fn avg_power_w(work_kj: u16, active_s: u32) -> u16 {
    if work_kj == 0 || active_s == 0 {
        return 0;
    }
    let joules = (work_kj as u64) * 1000;
    let seconds = active_s as u64;
    let watts = (joules + seconds / 2) / seconds;
    watts.min(65535) as u16
}

/// Efficiency Factor, W per bpm, scaled by 1000 so it stays an integer.
///
/// 0 when there is no work figure or no heart rate, never imputed: this is the
/// one number in the whole app that is independent of the heart-rate model, and
/// a filled-in value would make it a function of that model instead.
///
/// Not stored in the shared log, because `work_kj`, `active_s` and `hr_avg` all
/// are and a reader can divide.
pub fn efficiency_factor_x1000(work_kj: u16, active_s: u32, hr_avg: u8) -> u32 {
    let watts = avg_power_w(work_kj, active_s);
    if watts == 0 || hr_avg == 0 {
        return 0;
    }
    ((watts as u32) * 1000 + (hr_avg as u32) / 2) / (hr_avg as u32)
}
