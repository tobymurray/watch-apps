/**
 ******************************************************************************
 * @file    DumpManifest.hpp
 * @brief   Builds dump_manifest.txt -- the wire format the host reassembler
 *          parses.
 ******************************************************************************
 *
 * This is a fixed contract, not a design decision. `reassemble_dump.py` (on
 * una-sdk@research, see the README) matches four regexes against this text,
 * and every field width and token below is chosen to satisfy them. The line
 * shapes are copied from the sweep-7 instrumentation that produced the one
 * dump already verified end to end, so a manifest from this app parses in the
 * same script with no changes to either side.
 *
 * The whole manifest is held in RAM and the file rewritten from scratch after
 * every chunk, rather than appended to. Three reasons, in order of how much
 * they matter:
 *
 *   1. An interrupted dump must leave a *parseable* manifest, not a truncated
 *      line. Rewriting a complete buffer means the file on disk is always a
 *      whole number of lines.
 *   2. Append would need open(write, override=false) plus a seek to the end,
 *      and "what does a short write halfway through an append leave behind"
 *      is a worse question than "did the rewrite land".
 *   3. It is what the reference implementation did, on the run that is known
 *      to have worked.
 *
 * The cost is bounded and small: the default 32-chunk region needs about 2.6 kB
 * (see kMaxText), and the buffer is a member, not a stack frame.
 *
 * Overflow is tracked rather than silently truncating. A manifest missing its
 * tail parses as a *partial* dump -- the reassembler zero-fills what it cannot
 * see and reports success on the rest -- which would turn "the buffer was too
 * small" into "chunks 28-31 never happened". overflowed() lets the dumper
 * report that as the error it is.
 *
 ******************************************************************************
 */

#ifndef DUMP_MANIFEST_HPP
#define DUMP_MANIFEST_HPP

#include <cstddef>
#include <cstdint>

/**
 * @brief Accumulates manifest lines in a fixed buffer.
 *
 * Formatting only: it never touches the filesystem. FlashDumper owns one of
 * these and is what writes text() out. Keeping the two apart is what lets the
 * host tests assert on the exact bytes the reassembler will see without
 * needing a Kernel at all.
 */
class DumpManifest
{
public:
    /// Buffer size. The default region (4 MB in 128 kB chunks) produces a
    /// 66-byte header, 32 chunk lines of ~74 bytes, one whole-image line and
    /// three spot lines: ~2.6 kB. Sized at 8 kB so a config-file override can
    /// raise the chunk count well past the default (see DumpConfig's
    /// kMaxChunks, which is what actually bounds this) without coming close.
    static constexpr size_t kMaxText = 8192;

    /// Bytes a spot line will report, and so half the width of its hex field.
    /// Sixteen is what the reference implementation used: enough that a match
    /// means something (a Cortex-M vector table's first four words), short
    /// enough to read off a screen.
    static constexpr size_t kSpotBytes = 16;

    void reset();

    /**
     * @brief The `DUMP base=... nchunks=...` header line.
     *
     * Must be first: the reassembler refuses a manifest without it, since it
     * is where the region geometry comes from.
     */
    void addHeader(uint32_t base, uint32_t size, uint32_t chunk, uint32_t subwrite,
                   unsigned nchunks);

    /**
     * @brief One `DUMP chunk=i/n ...` line.
     * @param off Offset of this chunk *from base*, not an absolute address --
     *            the reassembler derives the chunk's filename from it
     *            (`dump_%06X.bin`), so this and the file it describes have to
     *            agree.
     * @param bw  Bytes actually written, as the filesystem reported them. Not
     *            assumed equal to @p size: a short write is the failure this
     *            field exists to make visible.
     * @param ok  Whether the chunk is trustworthy: written (or re-verified)
     *            whole, with its CRC confirmed.
     */
    void addChunk(unsigned index, unsigned total, uint32_t off, uint32_t size, uint32_t crc32,
                  uint32_t bw, bool ok);

    /// The `DUMP whole_image_crc32=...` line: the CRC of every byte of the
    /// region in order, which the host recomputes from the reassembled file.
    void addWhole(uint32_t crc32);

    /**
     * @brief A `DUMP spot addr=... bytes=...` line.
     *
     * A cheap cross-check that does not go through the CRC at all: a handful
     * of bytes read at a known address, compared by the host against the same
     * offset of the reassembled image. It catches the class of error a CRC
     * cannot -- a whole image that is internally consistent but offset, or
     * chunks written to the wrong filenames -- because it is anchored to an
     * absolute address rather than to a checksum of itself.
     *
     * @param addr  Absolute address the bytes were read from.
     * @param bytes The bytes; @p count of them, uppercase-hex encoded.
     */
    void addSpot(uint32_t addr, const uint8_t* bytes, size_t count);

    /// The manifest so far: NUL-terminated, and exactly what should be written
    /// to dump_manifest.txt.
    const char* text() const { return mText; }

    /// Length of text() in bytes, excluding the NUL. This, not strlen, is what
    /// the file write should ask for.
    size_t length() const { return mLength; }

    /// True if any line did not fit. The manifest is then incomplete and must
    /// not be reported as a finished dump -- see the class comment.
    bool overflowed() const { return mOverflowed; }

private:
    /// Appends one printf-formatted line, setting mOverflowed and appending
    /// nothing at all if it would not fit whole. Never a partial line: half a
    /// line is worse than no line, because a regex can match the wrong half.
    void addLine(const char* format, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;

    char   mText[kMaxText] = {'\0'};
    size_t mLength         = 0;
    bool   mOverflowed     = false;
};

#endif // DUMP_MANIFEST_HPP
