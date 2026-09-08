//! One panic handler, so no app has to get it right twice.
//!
//! Five of the seven renderers this crate was distilled from captured
//! `file:line` and the panic message; two sent the literal `"panic"`, having
//! been forked before the improvement landed. The two were the most complex
//! shipping apps, so the worst diagnostics were on the code most likely to
//! need them. Adopting this module is what fixes that, and it is why the
//! handler is here rather than in a template.

#[cfg(all(feature = "panic-handler", not(feature = "std"), not(test)))]
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

#[cfg(all(feature = "panic-handler", not(feature = "std"), not(test)))]
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
