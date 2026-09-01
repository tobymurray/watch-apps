//! Entering the kilojoules a bike console reports, with two buttons.
//!
//! Why the screen exists, why the entry is two places rather than a cursor or a
//! step mode, and why nothing is ever pre-filled into it: `Spin/README.md`,
//! "The kilojoules the bike knows".
//!
//! HARDWARE, and why there is no auto-repeat to build on: only `CLICK` arrives.
//! Spin sets `enMusicControl = true` in `Service::setCapabilities()`, so the
//! system claims the long press and `LONG_PRESS`/`HOLD_*` never reach the app.
//! Falsified by that setting changing, and only re-testable on the watch.
//!
//! MEASURED, over 200,000 simulated sessions of 25-90 min at 70-280 W (median
//! 490 kJ, p95 900 kJ), counting clicks to enter the value from zero: two
//! places costs 9.2 on average, against 10.2 for every scheme carrying a step
//! mode or a cursor -- they pay exactly the click they spend changing mode. A
//! third place costs 6.4 more (9.2 to 15.6) and buys +/-5 kJ, which is +/-1.85 W
//! on a 45-minute ride. Stopping the hundreds at 990 kJ leaves 2.3% of those
//! sessions unenterable. Re-measure by simulating that distribution and
//! counting clicks per scheme.

/// What each button adds, and — via `plus_label()` — what it says on the glass.
pub const HUNDREDS_STEP: u16 = 100;
pub const TENS_STEP: u16 = 10;

pub const HUNDREDS_PLACES: u16 = 20;
pub const TENS_PLACES: u16 = 10;

/// The largest enterable value, 1990 kJ.
pub const MAX_KJ: u16 = HUNDREDS_STEP * (HUNDREDS_PLACES - 1) + TENS_STEP * (TENS_PLACES - 1);

/// Fraction of metabolic energy that reaches the cranks. The literature puts
/// gross cycling efficiency at 21-25%; this is the midpoint, and the estimate
/// it feeds is a sanity check rather than a measurement.
const GROSS_EFFICIENCY: f32 = 0.23;

/// 1 kcal = 4.184 kJ, by definition of the thermochemical calorie.
const KJ_PER_KCAL: f32 = 4.184;

/// Splits a value into the two places the buttons turn. Total rather than
/// fallible, so a value somehow off the grid still decomposes onto it.
const fn places(kj: u16) -> (u16, u16) {
    let kj = if kj > MAX_KJ { MAX_KJ } else { kj };
    (kj / HUNDREDS_STEP, (kj % HUNDREDS_STEP) / TENS_STEP)
}

const fn compose(hundreds: u16, tens: u16) -> u16 {
    hundreds * HUNDREDS_STEP + tens * TENS_STEP
}

/// L1: the hundreds place, wrapping 1900 to 0.
pub const fn add_hundreds(kj: u16) -> u16 {
    let (h, t) = places(kj);
    compose((h + 1) % HUNDREDS_PLACES, t)
}

/// L2: the tens place, wrapping 90 to 0.
pub const fn add_tens(kj: u16) -> u16 {
    let (h, t) = places(kj);
    compose(h, (t + 1) % TENS_PLACES)
}

/// What the calorie model suggests the answer is near, on the grid the wearer
/// enters on. 0 = nothing worth showing; `as` saturates, so NaN and negatives
/// land there too.
pub fn estimate_kj(active_kcal: f32) -> u16 {
    let kj = active_kcal * KJ_PER_KCAL * GROSS_EFFICIENCY;
    let steps = (kj / TENS_STEP as f32 + 0.5) as u16;
    let rounded = steps.saturating_mul(TENS_STEP);
    if rounded > MAX_KJ {
        MAX_KJ
    } else {
        rounded
    }
}

// Gui.cpp holds the value and calls these; there is no entry state on the C++
// side to disagree with what the screen draws.

#[no_mangle]
pub extern "C" fn spin_gui_work_add_hundreds(kj: u16) -> u16 {
    add_hundreds(kj)
}

#[no_mangle]
pub extern "C" fn spin_gui_work_add_tens(kj: u16) -> u16 {
    add_tens(kj)
}

#[no_mangle]
pub extern "C" fn spin_gui_work_estimate_kj(active_kcal: f32) -> u16 {
    estimate_kj(active_kcal)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn nothing_said_is_where_it_starts() {
        assert_eq!(places(0), (0, 0));
    }

    #[test]
    fn each_button_adds_exactly_what_it_says_on_the_glass() {
        assert_eq!(add_hundreds(0), HUNDREDS_STEP);
        assert_eq!(add_tens(0), TENS_STEP);
        assert_eq!(add_tens(add_hundreds(0)), HUNDREDS_STEP + TENS_STEP);
    }

    #[test]
    fn places_wrap_within_themselves_and_carry_nothing() {
        assert_eq!(add_tens(90), 0);
        assert_eq!(add_tens(990), 900);
        assert_eq!(add_tens(430), 440);
        assert_eq!(add_tens(490), 400);

        let mut v = 430;
        for _ in 0..10 {
            v = add_tens(v);
        }
        assert_eq!(v, 430, "a full cycle of the tens should return the value");
    }

    #[test]
    fn the_hundreds_place_reaches_the_long_rides_and_comes_back_round() {
        assert_eq!(MAX_KJ, 1990);
        assert_eq!(add_hundreds(1790), 1890);
        assert_eq!(add_hundreds(1890), 1990, "the top of the range is reachable");
        assert_eq!(add_hundreds(1900), 0);
        assert_eq!(add_hundreds(1990), 90, "the tens survive the hundreds wrapping");
    }

    #[test]
    fn every_reachable_value_is_on_the_grid_and_in_range() {
        let mut v = 0u16;
        for _ in 0..HUNDREDS_PLACES {
            for _ in 0..TENS_PLACES {
                assert_eq!(v % TENS_STEP, 0);
                assert!(v <= MAX_KJ);
                v = add_tens(v);
            }
            v = add_hundreds(v);
        }
        assert_eq!(v, 0, "a full cycle of both places should return to 0");
    }

    #[test]
    fn a_value_off_the_grid_is_pulled_back_onto_it_rather_than_sticking() {
        assert_eq!(add_tens(437), 440);
        assert_eq!(add_hundreds(u16::MAX), 90);
    }

    #[test]
    fn the_estimate_is_work_and_not_dietary_energy() {
        // 402 kcal is 1682 kJ of food and about 387 kJ of work: a factor of
        // four, in the unit that invites the comparison.
        let got = estimate_kj(402.0);
        assert!((350..=420).contains(&got), "got {got}");
        assert!(got < 1000, "this is work, not the dietary kJ of the same estimate");
    }

    #[test]
    fn the_estimate_lands_on_the_grid_the_wearer_can_enter() {
        let mut kcal = 1.0f32;
        while kcal < 4000.0 {
            let got = estimate_kj(kcal);
            assert_eq!(got % TENS_STEP, 0, "kcal {kcal}");
            assert!(got <= MAX_KJ, "kcal {kcal}");
            kcal += 7.0;
        }
    }

    #[test]
    fn no_estimate_is_no_row_rather_than_a_zero() {
        assert_eq!(estimate_kj(0.0), 0);
        assert_eq!(estimate_kj(-5.0), 0);
        assert_eq!(estimate_kj(f32::NAN), 0);
        assert_eq!(estimate_kj(1.0), 0);
    }
}
