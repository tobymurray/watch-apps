//! The corpus for `effortkit::intervals`: the sessions people ride, the
//! malformed values a FAT volume can hand the watch, and the round trip back to
//! text the phone's own pattern accepts.

use effortkit::intervals::{Error, Session, Step, MAX_ITEMS, MAX_TEXT, OFF};

/// Parse, or fail the test with the reason.
fn ok(text: &str) -> Session {
    Session::parse(text.as_bytes())
        .unwrap_or_else(|e| panic!("{text:?} should parse, got {e:?}"))
}

/// Every step the session runs through, in order, as
/// `(seconds, effort, rep, reps)`.
fn walk(s: &Session) -> Vec<(u16, u8, u8, u8)> {
    let mut out = Vec::new();
    let Some(mut c) = s.start() else { return out };
    loop {
        let p = c.current(s).expect("a cursor that has not run out has a step");
        out.push((p.step.seconds, p.step.effort, p.rep, p.reps));
        if !c.advance(s) {
            break;
        }
    }
    out
}

fn rendered(s: &Session) -> String {
    let mut buf = [0u8; MAX_TEXT];
    let n = s.render(&mut buf).expect("128 bytes is always enough");
    String::from_utf8(buf[..n].to_vec()).unwrap()
}

// -- The sessions people ride -------------------------------------------------

/// The session ridden on 2026-09-04, which is the only interval ride this
/// repository has actually recorded. See
/// `Spin/Tests/pulled/20260904-intervals-real-max-184/`.
const REAL_RIDE: &str = "5m@2,6x(20s@5,40s@2),6m@2,3x(1m@5,1m@2),7m@2,2x(4m@4,3m@2)";

#[test]
fn the_ride_of_2026_09_04() {
    let s = ok(REAL_RIDE);
    assert_eq!(REAL_RIDE.len(), 58, "58 of the 128 bytes available");
    assert_eq!(s.item_count(), 6);
    assert_eq!(s.step_count(), 1 + 12 + 1 + 6 + 1 + 4);
    // Five minutes easy, six times twenty seconds all-out against forty easy.
    let steps = walk(&s);
    assert_eq!(steps[0], (300, 2, 0, 0));
    assert_eq!(steps[1], (20, 5, 1, 6));
    assert_eq!(steps[2], (40, 2, 1, 6));
    assert_eq!(steps[3], (20, 5, 2, 6));
    assert_eq!(steps[13], (360, 2, 0, 0));
    assert_eq!(*steps.last().unwrap(), (180, 2, 2, 2));
    assert_eq!(s.total_seconds(), 300 + 6 * 60 + 360 + 3 * 120 + 420 + 2 * 420);
}

#[test]
fn the_sessions_the_evaluation_tabled() {
    // name, text, items, steps -- the shapes §4 of INTERVAL-DSL-PROMPT.md asks
    // about, each written as far as this grammar allows.
    let corpus: &[(&str, &str, usize, u16)] = &[
        ("Norwegian 4x4", "10m@2,4x(4m@4,3m@2),10m@1", 3, 10),
        ("Tabata", "5m@2,8x(20s@5,10s@1),5m@1", 3, 18),
        ("30/30s", "10m@2,20x(30s@5,30s@2),10m@1", 3, 42),
        ("40/20s", "10m@2,15x(40s@5,20s@2),8m@1", 3, 32),
        ("Pyramid, written out", "10m@2,1m@4,2m@4,3m@5,2m@4,1m@4,10m@1", 7, 7),
        ("Over-unders, one set", "10m@2,3x(2m@3,2m@4),10m@1", 3, 8),
        ("Sweet spot block", "15m@2,3x(12m@3,5m@2),10m@1", 3, 8),
        ("Endurance with surges", "20m@2,6x(1m@5,9m@2),10m@2", 3, 14),
        ("A single steady hour", "60m@2", 1, 1),
        ("Warm-up and one effort", "10m@2,20m@4,10m@1", 3, 3),
        // The three-step block the pattern allows, which a work/float/rest
        // structure needs.
        ("Work, float, rest", "10m@2,4x(2m@5,2m@3,2m@1)", 2, 13),
        ("A four-step block", "5m@2,3x(30s@5,30s@2,30s@4,30s@2)", 2, 13),
    ];
    for (name, text, items, steps) in corpus {
        let s = ok(text);
        assert!(text.len() <= MAX_TEXT, "{name}: {} bytes", text.len());
        assert_eq!(s.item_count(), *items, "{name}: items");
        assert_eq!(s.step_count(), *steps, "{name}: steps");
    }
}

#[test]
fn what_the_grammar_cannot_hold() {
    // Priced in the evaluation and out of scope by decision: each is a
    // rejection rather than something quietly reinterpreted.
    for text in [
        "3x(4x(1m,1m),5m)", // nested repeats -- two levels costs 477 pattern chars
        "1m-5m",            // a ramp
        "5m@200w",          // a power target
        "5m@90rpm",         // a cadence target
        "10m,rest",         // an open-ended step
        "5m,until@5",       // recovery-driven
    ] {
        assert!(Session::parse(text.as_bytes()).is_err(), "{text:?} should be refused");
    }
}

// -- The questions the pattern does not answer --------------------------------

#[test]
fn leading_zeros_are_the_number_they_spell() {
    // Accepted, not rejected: `\d{1,3}` matches it, so the phone would send it
    // and the watch must not disagree with the phone about what is valid.
    assert_eq!(walk(&ok("007s")), vec![(7, 0, 0, 0)]);
    assert_eq!(walk(&ok("05m@3")), vec![(300, 3, 0, 0)]);
}

#[test]
fn zero_seconds_alone_is_no_session() {
    let s = ok("0s");
    assert!(s.is_empty());
    assert_eq!(s.step_count(), 0);
    assert!(s.start().is_none(), "no cursor, so nothing can be in a step");
    assert_eq!(rendered(&s), "0s");
}

#[test]
fn a_zero_length_step_anywhere_else_is_refused() {
    // A step nobody can be in is a boundary with no interval on either side.
    assert_eq!(Session::parse(b"5m,0s,3m"), Err(Error::ZeroStep));
    assert_eq!(Session::parse(b"0s,5m"), Err(Error::ZeroStep));
    assert_eq!(Session::parse(b"0m"), Err(Error::ZeroStep));
    assert_eq!(Session::parse(b"6x(0s,40s)"), Err(Error::ZeroStep));
}

#[test]
fn zero_repeats_is_refused_for_the_same_reason() {
    assert_eq!(Session::parse(b"0x(1m,1m)"), Err(Error::BadRepeats));
    assert_eq!(Session::parse(b"00x(1m,1m)"), Err(Error::BadRepeats));
}

#[test]
fn one_repeat_of_a_block_is_accepted() {
    // Degenerate and harmless. Not rewritten into two plain items: the cursor
    // walks it in place, and `1/1` on the banner is what was asked for.
    let s = ok("1x(1m,1m)");
    assert_eq!(s.item_count(), 1);
    assert_eq!(walk(&s), vec![(60, 0, 0, 0), (60, 0, 0, 0)]);
    // A block of one repetition names no repetition on the screen, exactly as a
    // step outside a block does.
    assert_eq!(walk(&s), walk(&ok("1m,1m")));
}

#[test]
fn case_and_whitespace_are_refused() {
    // The pattern is case-sensitive and the companion app must not trim
    // (Docs/app-config-fields.md §3.2), so each of these is a real byte in the
    // value and the watch has to reject exactly what the phone rejects.
    assert_eq!(Session::parse(b"10M"), Err(Error::Unexpected));
    assert_eq!(Session::parse(b" 10m"), Err(Error::BadDuration));
    assert_eq!(Session::parse(b"10m "), Err(Error::Unexpected));
    assert_eq!(Session::parse(b"6X(20s,40s)"), Err(Error::Unexpected));
    assert_eq!(Session::parse(b"10m, 5m"), Err(Error::BadDuration));
    assert_eq!(Session::parse(b"10S"), Err(Error::Unexpected));
}

#[test]
fn an_effort_out_of_range_is_refused_here_and_clamped_elsewhere() {
    // 1..8 is what the pattern admits, so 0 and 9 are rejections. `@8` on a
    // five-zone ladder is not this type's business: it is stored as written and
    // clamped where the word is chosen, so that the *ordering* survives -- see
    // Spin's `effort_word`.
    assert_eq!(Session::parse(b"5m@0"), Err(Error::Unexpected));
    assert_eq!(Session::parse(b"5m@9"), Err(Error::Unexpected));
    assert_eq!(walk(&ok("5m@8")), vec![(300, 8, 0, 0)]);
}

#[test]
fn the_item_ceiling_is_where_the_pattern_stops() {
    let fits: String = core::iter::repeat("1s").take(MAX_ITEMS).collect::<Vec<_>>().join(",");
    assert!(fits.len() <= MAX_TEXT);
    assert_eq!(ok(&fits).item_count(), MAX_ITEMS);

    // 128 bytes admits 43 of these; the pattern's `{0,40}` admits 41. A 42nd is
    // a value the phone would have refused.
    let over: String = core::iter::repeat("1s").take(MAX_ITEMS + 1).collect::<Vec<_>>().join(",");
    assert!(over.len() <= MAX_TEXT, "the 42nd item still fits in the field");
    assert_eq!(Session::parse(over.as_bytes()), Err(Error::TooManyItems));
}

#[test]
fn the_worst_case_a_128_byte_value_can_name() {
    // MEASURED by searching every item shape against the 128-byte cap: 2871
    // steps, in 126 bytes and 8 items. The bound a flat array would have to
    // carry, and the reason there is no flat array.
    let worst = "99x(1s,1s,1s,1s),99x(1s,1s,1s,1s),99x(1s,1s,1s,1s),\
                 99x(1s,1s,1s,1s),99x(1s,1s,1s,1s),99x(1s,1s,1s,1s),\
                 99x(1s,1s,1s),99x(1s,1s)";
    assert_eq!(worst.len(), 126);
    let s = ok(worst);
    assert_eq!(s.item_count(), 8);
    assert_eq!(s.step_count(), 2871);
    // Walked in place, so the cost of the worst case is the cursor and nothing
    // else.
    assert_eq!(walk(&s).len(), 2871);
    assert_eq!(core::mem::size_of_val(&s.start().unwrap()), 3);
}

// -- Totality -----------------------------------------------------------------

#[test]
fn the_empty_value_and_the_oversized_one() {
    assert_eq!(Session::parse(b""), Err(Error::Empty));
    let long = "1s,".repeat(50);
    assert!(long.len() > MAX_TEXT);
    assert_eq!(Session::parse(long.as_bytes()), Err(Error::TooLong));
}

#[test]
fn a_truncated_value_never_reads_past_its_end() {
    for text in ["5", "5m,", "6x", "6x(", "6x(20s", "6x(20s,", "6x(20s,40s", "5m@", "5m@5,"] {
        assert!(Session::parse(text.as_bytes()).is_err(), "{text:?} should be refused");
    }
}

#[test]
fn every_short_value_over_the_grammars_own_alphabet_terminates() {
    // Exhaustive over the bytes that can appear in a legal session, to four
    // characters: 8^4 values, every one of which must parse or be refused
    // without panicking or looping.
    const ALPHABET: &[u8] = b"05sm@x(),";
    let mut parsed = 0;
    for a in ALPHABET {
        for b in ALPHABET {
            for c in ALPHABET {
                for d in ALPHABET {
                    let buf = [*a, *b, *c, *d];
                    for len in 1..=4 {
                        if Session::parse(&buf[..len]).is_ok() {
                            parsed += 1;
                        }
                    }
                }
            }
        }
    }
    assert!(parsed > 0, "some of them are real sessions");
}

#[test]
fn every_byte_in_every_position_of_a_real_session_terminates() {
    // The value is plaintext on a FAT volume readable over USB, so any byte can
    // be in any position. Each mutation must parse or be refused; nothing here
    // may panic.
    let base = REAL_RIDE.as_bytes();
    let mut mutated = base.to_vec();
    for i in 0..base.len() {
        for byte in 0u8..=255 {
            mutated[i] = byte;
            let _ = Session::parse(&mutated);
        }
        mutated[i] = base[i];
    }
}

// -- The round trip -----------------------------------------------------------

#[test]
fn a_rendered_session_reparses_to_the_same_session() {
    for text in [
        REAL_RIDE,
        "0s",
        "007s",
        "1x(1m,1m)",
        "60m@2",
        "999s",
        "999m@8",
        "10m@2,20x(30s@5,30s@2),10m@1",
        "5m,3x(30s@5,30s@2,30s@4,30s@2)",
    ] {
        let s = ok(text);
        let out = rendered(&s);
        assert_eq!(Session::parse(out.as_bytes()), Ok(s), "{text:?} -> {out:?}");
        assert!(out.len() <= MAX_TEXT);
    }
}

#[test]
fn a_rendered_session_matches_the_manifests_own_pattern() {
    for text in [
        REAL_RIDE,
        "0s",
        "007s",
        "1x(1m,1m)",
        "999s@1",
        "99x(1s,1s,1s,1s)",
        "10m@2,4x(4m@4,3m@2),10m@1",
    ] {
        let out = rendered(&ok(text));
        assert!(pattern::matches(out.as_bytes()), "{out:?} does not match the pattern");
    }
    // The recogniser is worth nothing if it accepts everything.
    for bad in ["10M", " 10m", "1000m", "100x(1m,1m)", "3x(4x(1m,1m),5m)", "5m,"] {
        assert!(!pattern::matches(bad.as_bytes()), "{bad:?} should not match");
    }
}

/// The manifest's `pattern`, transcribed by hand rather than reused from the
/// parser, so the round trip is checked against the phone's rule and not
/// against the thing under test.
///
/// ```text
/// (\d{1,2}x\(\d{1,3}[sm](@[1-8])?(,\d{1,3}[sm](@[1-8])?){1,3}\)|\d{1,3}[sm](@[1-8])?)
/// (,(...same...)){0,40}
/// ```
mod pattern {
    /// Full match, the way `validate_app_config.py` §3.2 defines matching.
    pub fn matches(text: &[u8]) -> bool {
        let Some(mut at) = item(text, 0) else { return false };
        let mut items = 1;
        while at < text.len() {
            if text[at] != b',' || items > 40 {
                return false;
            }
            let Some(next) = item(text, at + 1) else { return false };
            at = next;
            items += 1;
        }
        true
    }

    fn item(text: &[u8], at: usize) -> Option<usize> {
        block(text, at).or_else(|| step(text, at))
    }

    /// `\d{1,2}x\(step(,step){1,3}\)`
    fn block(text: &[u8], at: usize) -> Option<usize> {
        let mut i = digits(text, at, 1, 2)?;
        if text.get(i) != Some(&b'x') || text.get(i + 1) != Some(&b'(') {
            return None;
        }
        i = step(text, i + 2)?;
        let mut more = 0;
        while text.get(i) == Some(&b',') {
            more += 1;
            if more > 3 {
                return None;
            }
            i = step(text, i + 1)?;
        }
        if more < 1 || text.get(i) != Some(&b')') {
            return None;
        }
        Some(i + 1)
    }

    /// `\d{1,3}[sm](@[1-8])?`
    fn step(text: &[u8], at: usize) -> Option<usize> {
        let mut i = digits(text, at, 1, 3)?;
        if !matches!(text.get(i), Some(b's') | Some(b'm')) {
            return None;
        }
        i += 1;
        if text.get(i) == Some(&b'@') {
            match text.get(i + 1) {
                Some(d) if (b'1'..=b'8').contains(d) => i += 2,
                _ => return None,
            }
        }
        Some(i)
    }

    /// `\d{min,max}`, greedy the way a regex engine is.
    fn digits(text: &[u8], at: usize, min: usize, max: usize) -> Option<usize> {
        let mut i = at;
        while i < text.len() && i - at < max && text[i].is_ascii_digit() {
            i += 1;
        }
        if i - at < min {
            None
        } else {
            Some(i)
        }
    }
}

// -- What the cursor costs ----------------------------------------------------

#[test]
fn the_parsed_form_and_the_cursor_are_the_whole_representation() {
    // The numbers the expansion decision was made on; a change here is a change
    // to the Service's .bss, so it should be a deliberate one.
    assert_eq!(core::mem::size_of::<Session>(), 740);
    assert_eq!(core::mem::size_of::<Step>(), 4);
    let c = ok(REAL_RIDE).start().unwrap();
    assert_eq!(core::mem::size_of_val(&c), 3);
}

#[test]
fn the_off_value_is_the_fields_declared_default() {
    assert_eq!(OFF, b"0s");
    assert!(Session::parse(OFF).unwrap().is_empty());
}
