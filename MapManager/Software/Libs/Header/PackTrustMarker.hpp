#ifndef PACK_TRUST_MARKER_HPP
#define PACK_TRUST_MARKER_HPP

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/Kernel.hpp"

/**
 * @brief Tiny fixed-layout binary marker: "this exact file (by size +
 *        declared CRC-32) has already passed a full CRC-32 scan" -- or has
 *        been confirmed corrupt.
 *
 * ===========================================================================
 * THIS COMMENT IS THE NORMATIVE SPEC of the trust-marker format. It is the
 * entire contract between Map Manager and any app that consumes a pack it
 * verifies. Consumers reimplement the reader (AthensRun's
 * MapPackTrustMarker.hpp in the una-sdk repo is one such reimplementation),
 * so changes here are format changes: bump the magic, do not redefine a
 * field in place.
 * ===========================================================================
 *
 * WHERE
 *   For a tracked file <path>, its marker is at exactly "<path>.trust" --
 *   sibling, same directory, suffix appended to the full name including the
 *   original extension ("maps/athens.rawtiles" -> "maps/athens.rawtiles.trust").
 *
 * LAYOUT -- exactly 16 bytes, little-endian, no padding, no version field
 * beyond the magic:
 *   [0..3]   magic    (uint32) -- kMagicGood ('M','P','T','1') or
 *                                kMagicBad  ('M','P','T','X')
 *   [4..11]  fileSize (uint64) -- size in bytes of the file this verdict is
 *                                about, as it was when scanned
 *   [12..15] crc      (uint32) -- the CRC-32/ISO-HDLC the file declared in
 *                                its own trailing 4 bytes
 *   A marker whose length is not exactly 16, or whose magic is neither
 *   value, is not a marker. See Trust::Absent below.
 *
 * TRI-STATE, and what a consumer must do with each:
 *   Good    -- a full CRC-32 pass over [0, fileSize-4) matched the CRC the
 *              file declares in its last 4 bytes. Safe to use the contents.
 *   Bad     -- that same pass ran and did NOT match. The file is corrupt;
 *              do not use its contents, and do not wait for a verdict that
 *              will not change. Report it and move on.
 *   Absent  -- no verdict yet. This is NOT "no pack" and NOT "bad pack": it
 *              means keep waiting and re-read later. Verification is a
 *              background pass that may take minutes on a large file, and it
 *              starts at boot rather than when an app opens.
 *
 * THE (size, crc) GUARD -- required, not optional:
 *   A consumer MUST treat a Good or Bad marker as applying to its file only
 *   if BOTH the marker's fileSize equals the file's current size AND the
 *   marker's crc equals the CRC the file currently declares in its footer.
 *   On any mismatch, treat the marker as Absent. A marker describes the bytes
 *   that were scanned; a file replaced since then is a different file that
 *   happens to share a path, and the old verdict says nothing about it.
 *   This is what makes a stale marker harmless rather than dangerous.
 *
 * WHAT THIS DETECTS, AND WHAT IT DOES NOT:
 *   This is an integrity check against corruption -- a truncated or
 *   interrupted transfer, a bad sector, bit rot. It is NOT an authenticity
 *   check and gives no protection against a deliberately crafted file.
 *   The file declares its own checksum, so a file that lies consistently
 *   (body altered, footer updated to match) verifies as Good; and the marker
 *   lives in a shared directory that every installed app can write, so a
 *   marker itself can be forged with 16 bytes. The root of trust is that no
 *   app installed on the watch is hostile. If that ever stops being a fair
 *   assumption, this needs a signature, not a checksum.
 *
 * ROBUSTNESS:
 *   ANY failure to read a well-formed marker (absent file, short read, bad
 *   magic, or a torn read from a concurrent writer) is treated as
 *   Trust::Absent by read(). There is no distinct "corrupt marker" state
 *   because that fallback is always safe (never a false-trust) and always
 *   self-correcting (the next completed verification pass overwrites it).
 *   Power loss during a marker write therefore costs a rescan, never a wrong
 *   answer: writes truncate first, so a partial write leaves a file that is
 *   not 16 bytes long, which reads as Absent.
 */
class PackTrustMarker {
public:
    enum class Trust { Absent, Bad, Good };

    static constexpr uint32_t kMagicGood  = 0x3154504Du; // 'M','P','T','1' (file byte order)
    static constexpr uint32_t kMagicBad   = 0x5854504Du; // 'M','P','T','X' (file byte order)
    static constexpr size_t   kMarkerSize = 16;

    PackTrustMarker(const SDK::Kernel& kernel, const char* path)
        : mKernel(kernel), mPath(path)
    {
    }

    /// @return Trust::Good/Bad with (sizeOut, crcOut) filled iff a
    /// well-formed marker of that kind exists. Trust::Absent (outputs
    /// untouched) on anything else.
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

    /// Publishes a Good marker for (fileSize, crc). Overwrites any existing
    /// marker (of either kind). @return false on any I/O failure -- nothing
    /// is published in that case.
    bool writeGood(uint64_t fileSize, uint32_t crc) const { return write(kMagicGood, fileSize, crc); }

    /// Publishes a Bad marker recording the (fileSize, crc) that failed to
    /// verify, so callers can distinguish "confirmed corrupt" from
    /// "not yet checked" instead of waiting on a check that will never pass.
    bool writeBad(uint64_t fileSize, uint32_t crc) const { return write(kMagicBad, fileSize, crc); }

private:
    const SDK::Kernel& mKernel;
    const char*         mPath;

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

    static void writeU32LE(uint8_t* p, uint32_t v)
    {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 16);
        p[3] = static_cast<uint8_t>(v >> 24);
    }

    static void writeU64LE(uint8_t* p, uint64_t v)
    {
        writeU32LE(p, static_cast<uint32_t>(v));
        writeU32LE(p + 4, static_cast<uint32_t>(v >> 32));
    }

    bool write(uint32_t magic, uint64_t fileSize, uint32_t crc) const
    {
        const char* slash = strrchr(mPath, '/');
        if (slash) {
            char dir[SDK::Interface::IFileSystem::skMaxPathLen]{};
            snprintf(dir, sizeof(dir), "%.*s", static_cast<int>(slash - mPath), mPath);
            if (!mKernel.fs.mkdir(dir)) {
                return false;
            }
        }

        std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(mPath);
        if (!file || !file->open(true, true)) {
            return false;
        }

        uint8_t buf[kMarkerSize];
        writeU32LE(buf + 0, magic);
        writeU64LE(buf + 4, fileSize);
        writeU32LE(buf + 12, crc);

        size_t written = 0;
        bool ok = file->write(reinterpret_cast<const char*>(buf), kMarkerSize, written) && (written == kMarkerSize);
        ok = file->flush() && ok;
        file->close();
        return ok;
    }
};

#endif // PACK_TRUST_MARKER_HPP
