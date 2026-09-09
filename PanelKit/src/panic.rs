//! One panic handler, so no app has to get it right twice.
//!
//! Five of the seven renderers this crate was distilled from captured
//! `file:line` and the panic message; two sent the literal `"panic"`, having
//! been forked before the improvement landed. The two were the most complex
//! shipping apps, so the worst diagnostics were on the code most likely to
//! need them. Adopting this module is what fixes that, and it is why the
//! handler is here rather than in a template.

#[cfg(all(feature = "panic-message", not(feature = "std"), not(test)))]
use core::fmt::Write as _;

extern "C" {
    /// Defined by the host shell. Must not return normally.
    ///
    /// One symbol for every app, where the seven crates each declared their
    /// own: there is nothing per-app left for a rename to break.
    pub fn panelkit_host_panic(msg: *const u8, len: u32) -> !;
}

/// A stack-allocated formatting buffer.
///
/// Sized against a 10 KiB GUI stack: 192 bytes is a path, a line number and a
/// message, and is small enough to build while unwinding a stack that may
/// already be deep.
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
/// **Every operation here is one that cannot itself panic** — iterator zips
/// rather than indexing, no `copy_from_slice`. That is not fastidiousness: a
/// handler that can panic pulls its own panic paths' formatting into the
/// binary, and the formatting is the expensive part. MEASURED on
/// `thumbv8m.main-none-eabihf`, in a linked ELF: written this way the location
/// costs 319 bytes over a handler that reports nothing; written with slice
/// indexing and `copy_from_slice` it costs 2,513 — eight times more, for
/// byte-identical output. Falsified by
/// `PanelKit/Docs/measurements/panic-handler`.
#[cfg(all(feature = "panic-handler", not(feature = "panic-message")))]
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
/// The cheap half, and the half that carries most of the debugging value: on a
/// device with no debugger a panic is otherwise a silent hang, and the line is
/// usually enough to see what. MEASURED, linking `Spin`'s real renderer:
/// **+706 bytes** over the `b"panic"` literal it shipped.
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
    unsafe { panelkit_host_panic(buf.as_ptr(), n as u32) }
}

/// Reports `file:line: message`.
///
/// The message needs `core::fmt`, which is the expensive part of a panic
/// handler by a wide margin. MEASURED, linking `Spin`'s real renderer against
/// the `b"panic"` literal it shipped: the location alone is **+706 bytes**, and
/// the message takes it to **+3,226** — so the message is 78% of the cost and
/// 2.5 KB of a 600 KiB window. Take this when the message is worth that and
/// the line is not enough; take `panic-handler` alone otherwise. Falsified by
/// re-running `PanelKit/Docs/measurements/panic-handler`.
#[cfg(all(feature = "panic-message", not(feature = "std"), not(test)))]
#[panic_handler]
fn on_panic(info: &core::panic::PanicInfo) -> ! {
    let mut msg = Buf::<192>::new();
    if let Some(loc) = info.location() {
        let _ = write!(msg, "{}:{}: ", loc.file(), loc.line());
    }
    let _ = write!(msg, "{}", info.message());

    let s = msg.as_str();
    unsafe { panelkit_host_panic(s.as_ptr(), s.len() as u32) }
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::fmt::Write as _;

    #[test]
    fn a_message_that_fits_survives_whole() {
        let mut b = Buf::<64>::new();
        let _ = write!(b, "src/lib.rs:42: {}", "assertion failed");
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
