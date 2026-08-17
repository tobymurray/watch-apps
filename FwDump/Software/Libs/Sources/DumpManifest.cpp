/**
 ******************************************************************************
 * @file    DumpManifest.cpp
 * @brief   Manifest line formatting.
 ******************************************************************************
 */

#include "DumpManifest.hpp"

#include <cstdarg>
#include <cstdio>

void DumpManifest::reset()
{
    mText[0]    = '\0';
    mLength     = 0;
    mOverflowed = false;
}

void DumpManifest::addLine(const char* format, ...)
{
    // Formatted into a scratch line first, then appended only if it fits
    // whole. snprintf straight into the tail of mText would happily write a
    // truncated line and report the length it wanted -- and a truncated line
    // is the one outcome this must not produce, because the host matches these
    // with regexes that can succeed on a partial line and yield a wrong
    // number rather than a parse failure.
    char line[160];

    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    if (written < 0 || static_cast<size_t>(written) >= sizeof(line)) {
        // The line itself did not fit the scratch buffer. Only reachable if a
        // field is far wider than the formats below can produce, but it is
        // still a manifest that will not describe the dump, so say so.
        mOverflowed = true;
        return;
    }

    const size_t len = static_cast<size_t>(written);
    if (mLength + len + 1 > kMaxText) {
        mOverflowed = true;
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        mText[mLength + i] = line[i];
    }
    mLength += len;
    mText[mLength] = '\0';
}

void DumpManifest::addHeader(uint32_t base, uint32_t size, uint32_t chunk, uint32_t subwrite,
                             unsigned nchunks)
{
    // Field widths match the sweep-7 reference byte for byte. The host's regex
    // accepts any run of hex digits here, so the widths are not load-bearing
    // for parsing -- they are load-bearing for a human diffing this manifest
    // against the one from the verified run.
    //
    // The unsigned long casts are not decoration: uint32_t is `long unsigned`
    // on arm-none-eabi and `unsigned` on the host, so %08lX with a bare
    // uint32_t is a real format mismatch on one of the two platforms.
    addLine("DUMP base=%08lX size=%08lX chunk=%08lX subwrite=%08lX nchunks=%u\n",
            static_cast<unsigned long>(base), static_cast<unsigned long>(size),
            static_cast<unsigned long>(chunk), static_cast<unsigned long>(subwrite), nchunks);
}

void DumpManifest::addChunk(unsigned index, unsigned total, uint32_t off, uint32_t size,
                            uint32_t crc32, uint32_t bw, bool ok)
{
    addLine("DUMP chunk=%u/%u off=%08lX size=%08lX crc32=%08lX bw=%lu ok=%s\n", index, total,
            static_cast<unsigned long>(off), static_cast<unsigned long>(size),
            static_cast<unsigned long>(crc32), static_cast<unsigned long>(bw), ok ? "Y" : "N");
}

void DumpManifest::addWhole(uint32_t crc32)
{
    addLine("DUMP whole_image_crc32=%08lX\n", static_cast<unsigned long>(crc32));
}

void DumpManifest::addSpot(uint32_t addr, const uint8_t* bytes, size_t count)
{
    // Two hex digits per byte plus a NUL. Sized for kSpotBytes; a caller
    // asking for more is clamped rather than overrunning, since a short spot
    // line still cross-checks correctly (the host reads the byte count back
    // out of the field's own length).
    char hex[kSpotBytes * 2 + 1];
    if (count > kSpotBytes) {
        count = kSpotBytes;
    }

    size_t at = 0;
    for (size_t i = 0; i < count; ++i) {
        const int written =
            std::snprintf(hex + at, sizeof(hex) - at, "%02X", static_cast<unsigned>(bytes[i]));
        if (written != 2) {
            break;
        }
        at += 2;
    }
    hex[at] = '\0';

    addLine("DUMP spot addr=%08lX bytes=%s\n", static_cast<unsigned long>(addr), hex);
}
