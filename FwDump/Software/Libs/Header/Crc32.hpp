/**
 ******************************************************************************
 * @file    Crc32.hpp
 * @brief   CRC-32/ISO-HDLC, byte-compatible with Python's zlib.crc32.
 ******************************************************************************
 *
 * The one thing in this app that has to agree with a program running on
 * another machine. The host-side reassembler (reassemble_dump.py, see the
 * README) recomputes every chunk's CRC with zlib.crc32 and compares it against
 * what this file produced on the watch; a difference is reported as a corrupt
 * dump. So an incompatibility here does not show up as a wrong number, it
 * shows up as an honest dump being condemned -- which is why the host tests
 * check this against hardcoded zlib output rather than only against itself.
 *
 * The variant, spelled out because "CRC-32" names a dozen incompatible things:
 * reflected polynomial 0xEDB88320, init 0xFFFFFFFF, final xor 0xFFFFFFFF, no
 * bit/byte reversal of the result. That is what zlib, PNG, gzip and most
 * language standard libraries mean by crc32. Its published check value -- the
 * CRC of ASCII "123456789" -- is 0xCBF43926, asserted in the tests.
 *
 * Split into a header and an implementation, rather than folded into the
 * dumper the way MapManager folds its copy into PackCrcVerifier.cpp, for
 * exactly one reason: the tests must be able to hash bytes without going
 * anywhere near a Kernel or a flash address.
 *
 ******************************************************************************
 */

#ifndef CRC32_HPP
#define CRC32_HPP

#include <cstddef>
#include <cstdint>

namespace Crc32
{

/// Value a fresh running CRC starts from. Exposed because a chunked hash has
/// to hold the running value between calls, and callers should not have to
/// know that "all ones" is the right seed.
constexpr uint32_t kInit = 0xFFFFFFFFu;

/**
 * @brief Fold @p length bytes into a running CRC.
 * @param crc  Running value: kInit to begin, or what the previous call
 *             returned. NOT the finalised value -- feeding a finalised CRC
 *             back in silently produces nonsense.
 * @return The new running value.
 *
 * Table-driven (256 entries, built once on first use). The bitwise form in
 * the sweep-7 reference implementation this app was ported from is byte-for-
 * byte equivalent and about eight times slower; at 4 MB hashed per dump that
 * difference is worth the kilobyte of table.
 */
uint32_t update(uint32_t crc, const uint8_t* data, size_t length);

/**
 * @brief Turn a running value into the CRC as the rest of the world writes it.
 *
 * Idempotent-looking but not idempotent: applying it twice returns the
 * running value again. Call it once, at the end.
 */
inline uint32_t finalise(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }

/**
 * @brief One-shot hash of a whole buffer: seed, fold, finalise.
 *
 * Equivalent to zlib.crc32(bytes) in Python, and to
 * finalise(update(kInit, data, length)) here.
 */
inline uint32_t of(const uint8_t* data, size_t length)
{
    return finalise(update(kInit, data, length));
}

} // namespace Crc32

#endif // CRC32_HPP
