//! The whole 64-colour gamut of a two-bits-a-channel panel, as one sheet.
//!
//!   cargo run --features preview --example gamut [-- <path>]

fn main() {
    let path = std::env::args().nth(1).unwrap_or_else(|| "Docs/gamut.png".to_string());
    inscribed_disc::preview::gamut_sheet(&path, 48).expect("cannot write the gamut sheet");
    println!("wrote {path}");
}
