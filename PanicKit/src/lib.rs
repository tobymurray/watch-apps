//! One `#[panic_handler]` for this repository's Rust halves, reporting through
//! one symbol the C++ host defines.
//!
//! On this device a panic is a silent hang with a ride in progress, there is no
//! debugger, and the only recovery is a reboot — so the handler says where it
//! panicked. `.text` executes from the same 600 KiB window as the framebuffer,
//! so that diagnostic is charged against the pixels, which is why the location
//! and the message are separate features. What each costs, and the trap that
//! cost eight times the answer, are in
//! `PanicKit/Docs/2026-09-09-panic-handler-cost.md`.
//!
//! Nothing here draws, so this crate is not part of `inscribed-disc`: a
//! published graphics crate that owns `panic_impl` collides with `panic-halt`,
//! `panic-probe` and `defmt` for anyone who enables it. It is also usable by a
//! Service half, which has no framebuffer at all.
//!
//! The host side is `Header/HostPanic.hpp`.

#![cfg_attr(not(any(test, feature = "std")), no_std)]
#![deny(missing_docs)]

#[cfg(all(feature = "panic-message", not(feature = "std"), not(test)))]
use core::fmt::Write as _;

extern "C" {
    /// Defined by the host shell. Must not return normally.
    pub fn panickit_host_panic(msg: *const u8, len: u32) -> !;
}

/// A stack-allocated formatting buffer.
pub struct Buf<const N: usize> {
    b: [u8; N],
    n: usize,
}

impl<const N: usize> Buf<N> {
    /// An empty buffer.
    pub const fn new() -> Self {
        Buf { b: [0; N], n: 0 }
    }

    /// What has been written, or `""` if truncation split a character.
    pub fn as_str(&self) -> &str {
        // Truncation can land mid-character, and a panic handler is the last
        // place to panic again, so a bad tail becomes an empty message.
        core::str::from_utf8(&self.b[..self.n]).unwrap_or("")
    }
}

impl<const N: usize> Default for Buf<N> {
    fn default() -> Self {
        Self::new()
    }
}

impl<const N: usize> core::fmt::Write for Buf<N> {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        for &c in s.as_bytes() {
            if self.n >= N {
                return Err(core::fmt::Error);
            }
            self.b[self.n] = c;
            self.n += 1;
        }
        Ok(())
    }
}

/// Writes `loc.file():loc.line()` into `buf`, returning how many bytes.
///
/// Nothing here can itself panic — iterator zips, no indexing, no
/// `copy_from_slice`. MEASURED: written this way the location costs 319 bytes;
/// written with indexing it costs 2,513, because a panicking operation inside a
/// panic handler pulls its own panic path's formatting in. Re-run
/// `PanicKit/Docs/measurements/cost`.
// Compiled exactly where it has a caller: the location handler below, which
// needs `not(std)` and `not(test)`, or the tests, which need `test`. Under
// `panic-handler,std` -- the combination that host-tests this crate -- the
// handler is inert and the lib target has no tests, so without `any(...)` this
// is dead code.
#[cfg(all(
    feature = "panic-handler",
    not(feature = "panic-message"),
    any(test, not(feature = "std"))
))]
fn write_location(buf: &mut [u8], loc: &core::panic::Location<'_>) -> usize {
    let mut n = 0usize;
    for (dst, src) in buf.iter_mut().zip(loc.file().as_bytes()) {
        *dst = *src;
        n += 1;
    }
    for (dst, src) in buf.iter_mut().skip(n).zip(b":") {
        *dst = *src;
        n += 1;
    }
    let mut digits = [0u8; 10];
    let mut d = 0usize;
    let mut line = loc.line();
    loop {
        for (i, slot) in digits.iter_mut().enumerate() {
            if i == d {
                *slot = b'0' + (line % 10) as u8;
            }
        }
        d += 1;
        line /= 10;
        if line == 0 || d == digits.len() {
            break;
        }
    }
    for (dst, src) in buf.iter_mut().skip(n).zip(digits.iter().take(d).rev()) {
        *dst = *src;
        n += 1;
    }
    n
}

/// Reports `file:line` and nothing else.
///
/// MEASURED, linked: +706 bytes over a handler that reports nothing, against
/// +3,226 for one that adds the message. Re-run
/// `PanicKit/Docs/measurements/cost`.
#[cfg(all(
    feature = "panic-handler",
    not(feature = "panic-message"),
    not(feature = "std"),
    not(test)
))]
#[panic_handler]
fn on_panic(info: &core::panic::PanicInfo) -> ! {
    let mut buf = [0u8; 192];
    let n = match info.location() {
        Some(loc) => write_location(&mut buf, loc),
        None => 0,
    };
    unsafe { panickit_host_panic(buf.as_ptr(), n as u32) }
}

/// Reports `file:line: message`.
///
/// MEASURED, linked: +3,226 bytes against +706 for the location alone, because
/// the message needs `core::fmt`. Re-run
/// `PanicKit/Docs/measurements/cost`.
#[cfg(all(feature = "panic-message", not(feature = "std"), not(test)))]
#[panic_handler]
fn on_panic(info: &core::panic::PanicInfo) -> ! {
    let mut msg = Buf::<192>::new();
    if let Some(loc) = info.location() {
        let _ = write!(msg, "{}:{}: ", loc.file(), loc.line());
    }
    let _ = write!(msg, "{}", info.message());

    let s = msg.as_str();
    unsafe { panickit_host_panic(s.as_ptr(), s.len() as u32) }
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::fmt::Write as _;

    /// The location formatter is only reachable from a device build, so
    /// nothing exercised it on a host. It is also the function the design
    /// record makes a claim about -- written with iterator zips so that no
    /// operation in it can itself panic -- which is worth holding.
    #[cfg(all(feature = "panic-handler", not(feature = "panic-message")))]
    mod location {
        use super::*;

        fn rendered(buf_len: usize, loc: &core::panic::Location<'_>) -> String {
            let mut buf = vec![0u8; buf_len];
            let n = write_location(&mut buf, loc);
            String::from_utf8(buf[..n].to_vec()).unwrap()
        }

        #[test]
        fn it_writes_file_then_colon_then_line() {
            let loc = core::panic::Location::caller();
            let out = rendered(192, loc);
            let (file, line) = out.rsplit_once(':').expect("no colon");
            assert_eq!(file, loc.file());
            assert_eq!(line.parse::<u32>().unwrap(), loc.line());
        }

        /// The digits are emitted least-significant first and reversed, which
        /// is the part most likely to be wrong.
        #[test]
        fn multi_digit_lines_are_not_reversed() {
            let out = rendered(192, core::panic::Location::caller());
            let line: u32 = out.rsplit_once(':').unwrap().1.parse().unwrap();
            assert_eq!(line.to_string(), out.rsplit_once(':').unwrap().1);
            assert!(line > 9, "this call site is past line 9, so it exercises the reversal");
        }

        /// A buffer too small must truncate rather than write past its end --
        /// a panic handler that itself panics is the one thing this must not
        /// do.
        #[test]
        fn a_short_buffer_truncates_and_never_overruns() {
            let loc = core::panic::Location::caller();
            for len in 0..24 {
                let mut buf = vec![0xAAu8; len + 8];
                let n = write_location(&mut buf[..len], loc);
                assert!(n <= len, "wrote {n} into {len} bytes");
                assert!(buf[len..].iter().all(|&b| b == 0xAA), "overran at len {len}");
            }
        }
    }

    #[test]
    fn a_message_that_fits_survives_whole() {
        let mut b = Buf::<64>::new();
        let _ = write!(b, "src/lib.rs:42: assertion failed");
        assert_eq!(b.as_str(), "src/lib.rs:42: assertion failed");
    }

    #[test]
    fn a_message_that_does_not_fit_is_truncated_rather_than_lost() {
        let mut b = Buf::<8>::new();
        let _ = write!(b, "0123456789abcdef");
        assert_eq!(b.as_str(), "01234567");
    }

    #[test]
    fn a_truncated_multibyte_tail_yields_an_empty_string_rather_than_panicking() {
        let mut b = Buf::<2>::new();
        let _ = write!(b, "é!");
        // 'é' is two bytes, so the buffer holds a complete character here.
        assert_eq!(b.as_str(), "é");

        let mut c = Buf::<1>::new();
        let _ = write!(c, "é");
        assert_eq!(c.as_str(), "");
    }
}
