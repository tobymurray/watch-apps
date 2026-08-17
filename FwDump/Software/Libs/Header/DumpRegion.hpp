/**
 ******************************************************************************
 * @file    DumpRegion.hpp
 * @brief   What to read, and in what size pieces.
 ******************************************************************************
 *
 * Split out from both the dumper and the config reader so that "is this
 * geometry coherent" is one testable question asked in one place, rather than
 * a set of assumptions each of the two makes separately. The dumper's loop
 * relies on all of them (chunks tile the region exactly, sub-writes tile a
 * chunk exactly), and a config file is the one way a person can break them.
 *
 ******************************************************************************
 */

#ifndef DUMP_REGION_HPP
#define DUMP_REGION_HPP

#include <cstdint>

/**
 * @brief The address range to dump, and the granularity to do it at.
 *
 * Defaults are the internal flash of the STM32U5A5 this watch runs: 4 MB at
 * 0x08000000, confirmed by a live read of the flash-size register
 * (0x0BFA07A0, low 16 bits = 0x1000 KB) as well as by the datasheet.
 *
 * Dumping the whole 4 MB rather than the ~2.04 MB the real image occupies is
 * deliberate. The tail is erased flash (0xFF), so it costs time and space but
 * no correctness, and it means the app does not have to trust a hardcoded
 * image length -- which is exactly the kind of constant that goes stale on the
 * next firmware release and silently truncates a dump.
 */
struct DumpRegion {
    /// First address read. Absolute, and recorded as-is in the manifest.
    uint32_t base = 0x08000000u;

    /// Bytes to read from base. Must be a whole number of chunks.
    uint32_t size = 0x00400000u;

    /// Bytes per chunk, and so per output file. 128 kB gives 32 files for the
    /// default region -- few enough to list on a screen, small enough that
    /// losing the one in flight to an interruption costs little.
    uint32_t chunk = 0x00020000u;

    /// Bytes per read/hash/write step inside a chunk. This is the granularity
    /// the dumper yields at, so it bounds how long the service can go without
    /// servicing a message, and it is the size a short write would be detected
    /// at. Recorded in the manifest for the record; the host's arithmetic does
    /// not depend on it.
    uint32_t subwrite = 0x00001000u;

    /// Most chunks a region may be split into. Bounds DumpManifest's buffer
    /// (which must hold a line per chunk) and the resume scan's work. 256
    /// leaves room for a far smaller chunk size than the default while keeping
    /// the manifest inside 8 kB.
    static constexpr unsigned kMaxChunks = 256;

    /// Largest permitted sub-write. FlashDumper carries a buffer of this size
    /// to read chunk files back for re-verification, so this is a real RAM
    /// cost paid whether or not a config raises subwrite -- kept at 16 kB
    /// because that is the value the sweep-7 reference used on hardware
    /// without a single short write, making it the largest size known to be
    /// safe here.
    static constexpr uint32_t kMaxSubwrite = 16384;

    /// Number of chunk files this region produces. Only meaningful when
    /// valid(); the division is exact by construction there.
    unsigned nchunks() const { return chunk == 0 ? 0 : static_cast<unsigned>(size / chunk); }

    /**
     * @brief Whether the loop's invariants hold.
     *
     * Everything here is something the dumper would otherwise get wrong
     * quietly: a size that is not a whole number of chunks would drop the
     * remainder without any line in the manifest to say so, and a chunk that
     * is not a whole number of sub-writes would make the last sub-write of
     * every chunk overrun into the next one.
     */
    bool valid() const
    {
        if (size == 0 || chunk == 0 || subwrite == 0) {
            return false;
        }
        if (size % chunk != 0 || chunk % subwrite != 0) {
            return false;
        }
        if (subwrite > kMaxSubwrite) {
            return false;
        }
        if (nchunks() == 0 || nchunks() > kMaxChunks) {
            return false;
        }
        // The region must not wrap the 32-bit address space: base + size is
        // computed as a 64-bit sum precisely so that a config asking for
        // 0xFFFFF000 + 0x10000 is rejected rather than silently reading from 0.
        if (static_cast<uint64_t>(base) + static_cast<uint64_t>(size) > 0x100000000ull) {
            return false;
        }
        return true;
    }
};

#endif // DUMP_REGION_HPP
