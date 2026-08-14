/**
 ******************************************************************************
 * @file    PackTrustReader.hpp
 * @brief   Read-only mirror of Map Manager's 16-byte pack trust marker.
 *
 * ===========================================================================
 * THIS IS A MIRROR, NOT THE SPEC. The normative definition of the format is
 * the class comment on Map Manager's own
 * `MapManager/Software/Libs/Header/PackTrustMarker.hpp` (this repository).
 * Read that before changing anything here. If the two ever disagree, that
 * one is right.
 * ===========================================================================
 *
 * Short version, enough to follow the code:
 *
 *   - For a pack at <path>, the marker is at exactly "<path>.trust".
 *   - Exactly 16 bytes, little-endian: magic (u32), fileSize (u64), crc (u32).
 *     Magic 'MPT1' = Good, 'MPTX' = Bad. Anything else -- wrong length,
 *     unknown magic, unreadable, torn read from a concurrent writer -- is
 *     Absent, which is a normal state and not an error.
 *   - Three states, all three needing handling: Good (safe to use), Bad
 *     (confirmed corrupt -- report it, do not wait), Absent (no verdict yet
 *     -- keep polling; this is the normal state for the first minutes after
 *     a pack is deployed).
 *
 * Why this class exists rather than a bare `read()`: the format's `(size,
 * crc)` guard is the consumer's job, not the marker's, and it is the one part
 * an integrator can silently omit and still appear to work. `verdictFor()`
 * applies it, so callers cannot forget: a marker only speaks for a file whose
 * current size and self-declared CRC still match what was scanned. Everything
 * else reads as Absent.
 *
 * Write support is deliberately absent. Map Manager owns verification; these
 * apps are pure consumers, and an app that could write a marker could publish
 * a verdict it never earned.
 ******************************************************************************
 */

#ifndef MAPKIT_PACKTRUSTREADER_HPP
#define MAPKIT_PACKTRUSTREADER_HPP

#include <cstdint>
#include <cstdio>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/Kernel.hpp"

namespace MapKit
{

/// Suffix appended to a pack's full path to name its marker.
inline constexpr const char* kTrustSuffix = ".trust";

class PackTrustReader
{
public:
    enum class Trust { Absent, Bad, Good };

    static constexpr uint32_t kMagicGood  = 0x3154504Du; // 'M','P','T','1' (file byte order)
    static constexpr uint32_t kMagicBad   = 0x5854504Du; // 'M','P','T','X' (file byte order)
    static constexpr size_t   kMarkerSize = 16;

    PackTrustReader(const SDK::Kernel& kernel, const char* markerPath)
        : mKernel(kernel), mPath(markerPath)
    {
    }

    /// Raw marker read, guard NOT applied. @return Good/Bad with (sizeOut,
    /// crcOut) filled iff a well-formed marker of that kind exists; Absent
    /// (outputs untouched) on anything else. Prefer verdictFor().
    Trust read(uint64_t& sizeOut, uint32_t& crcOut) const
    {
        std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(mPath);
        if (!file || !file->exist()) {
            return Trust::Absent;
        }
        if (!file->open(false, false)) {
            return Trust::Absent;
        }
        uint8_t buf[kMarkerSize];
        bool ok = false;
        if (file->size() == kMarkerSize) {
            size_t got = 0;
            ok = file->read(reinterpret_cast<char*>(buf), kMarkerSize, got) && (got == kMarkerSize);
        }
        file->close();
        if (!ok) {
            return Trust::Absent;
        }

        const uint32_t magic = readU32LE(buf + 0);
        if (magic != kMagicGood && magic != kMagicBad) {
            return Trust::Absent;
        }
        sizeOut = readU64LE(buf + 4);
        crcOut  = readU32LE(buf + 12);
        return (magic == kMagicGood) ? Trust::Good : Trust::Bad;
    }

    /// The verdict that actually applies to the file described by
    /// (@p fileSize, @p declaredCrc) -- read() with the mandatory (size, crc)
    /// guard applied. A verdict for different bytes at the same path is
    /// reported as Absent, because that is what it is: no verdict about
    /// *this* file. This is the entry point consumers should use.
    Trust verdictFor(uint64_t fileSize, uint32_t declaredCrc) const
    {
        uint64_t markedSize = 0;
        uint32_t markedCrc  = 0;
        const Trust trust = read(markedSize, markedCrc);
        if (trust == Trust::Absent) {
            return Trust::Absent;
        }
        if (markedSize != fileSize || markedCrc != declaredCrc) {
            return Trust::Absent;
        }
        return trust;
    }

    /// Fills @p out with "<packPath>.trust". @return false (leaving @p out
    /// untouched) if the result would not fit -- callers must treat that as
    /// "no verdict available" rather than truncating, since a truncated path
    /// names a different file.
    static bool markerPathFor(const char* packPath, char* out, size_t outSize)
    {
        const int n = std::snprintf(out, outSize, "%s%s", packPath, kTrustSuffix);
        return n > 0 && static_cast<size_t>(n) < outSize;
    }

private:
    const SDK::Kernel& mKernel;
    const char*        mPath;

    static uint32_t readU32LE(const uint8_t* p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
             | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    static uint64_t readU64LE(const uint8_t* p)
    {
        uint64_t lo = readU32LE(p);
        uint64_t hi = readU32LE(p + 4);
        return lo | (hi << 32);
    }
};

} // namespace MapKit

#endif // MAPKIT_PACKTRUSTREADER_HPP
