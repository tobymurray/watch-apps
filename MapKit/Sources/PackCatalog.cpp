#include <MapKit/PackCatalog.hpp>

#include <MapKit/MapMath.hpp>
#include <MapKit/PackTrustReader.hpp>
#include <SDK/RawTiles/Container.hpp>

#include <cstdio>
#include <cstring>
#include <memory>

namespace MapKit
{
namespace
{

uint32_t readU32LE(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

int32_t readI32LE(const uint8_t* p)
{
    return static_cast<int32_t>(readU32LE(p));
}

uint16_t readU16LE(const uint8_t* p)
{
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0])
                                 | (static_cast<uint16_t>(p[1]) << 8));
}

// Header field offsets, rawtiles wire format v1 (spec § 4). Only the fields
// the selection rule and the screen need are decoded here; Container does the
// full job on the one pack that gets opened.
constexpr size_t kOffFormatMajor = 4;
constexpr size_t kOffPixelFormat = 56;
constexpr size_t kOffProjection  = 57;
constexpr size_t kOffAddressing  = 58;
constexpr size_t kOffTileDim     = 60;
constexpr size_t kOffZoomMax     = 63;
constexpr size_t kOffBbox        = 64;   // minLon, minLat, maxLon, maxLat (i32 each)

bool hasExtension(const char* name, const char* ext)
{
    const size_t nameLen = std::strlen(name);
    const size_t extLen  = std::strlen(ext);
    return nameLen > extLen && std::strcmp(name + (nameLen - extLen), ext) == 0;
}

} // namespace

bool PackCatalog::fullPathFor(const char* name, char* out, size_t outSize)
{
    const int n = std::snprintf(out, outSize, "%s/%s", kMapsDir, name);
    return n > 0 && static_cast<size_t>(n) < outSize;
}

bool PackCatalog::declaredCrc(const char* fullPath, uint64_t& sizeOut, uint32_t& crcOut) const
{
    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(fullPath);
    if (!file || !file->open(false, false)) {
        return false;
    }
    const size_t size = file->size();
    bool ok = false;
    if (size >= 4 && file->seek(size - 4)) {
        uint8_t footer[4];
        size_t got = 0;
        ok = file->read(reinterpret_cast<char*>(footer), 4, got) && got == 4;
        if (ok) {
            crcOut  = readU32LE(footer);
            sizeOut = static_cast<uint64_t>(size);
        }
    }
    file->close();
    return ok;
}

bool PackCatalog::peek(const char* fullPath, PackFacts& out) const
{
    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(fullPath);
    if (!file || !file->open(false, false)) {
        return false;
    }
    uint8_t hdr[kHeaderBytes];
    size_t got = 0;
    const bool read = file->size() >= kMinPackBytes
                   && file->read(reinterpret_cast<char*>(hdr), kHeaderBytes, got)
                   && got == kHeaderBytes;
    file->close();
    if (!read) {
        return false;
    }

    if (!(hdr[0] == 'R' && hdr[1] == 'A' && hdr[2] == 'W' && hdr[3] == 'T')) {
        return false;
    }
    if (hdr[kOffFormatMajor] != 1) {
        return false;
    }
    // Screen out anything this renderer cannot draw. See the header comment:
    // each of these is a hard assumption somewhere in the draw path, not a
    // preference.
    if (hdr[kOffPixelFormat] != static_cast<uint8_t>(SDK::RawTiles::PixelFormat::ABGR2222)) {
        return false;
    }
    if (hdr[kOffProjection] != static_cast<uint8_t>(SDK::RawTiles::Projection::WebMercator)) {
        return false;
    }
    if (hdr[kOffAddressing] != static_cast<uint8_t>(SDK::RawTiles::Addressing::Quadtree)) {
        return false;
    }
    if (readU16LE(hdr + kOffTileDim) != MapMath::TILE_DIM) {
        return false;
    }

    const uint8_t zoomMax = hdr[kOffZoomMax];
    if (zoomMax >= 24) {
        return false;
    }

    PackFacts facts {};
    facts.zoomMax        = zoomMax;
    facts.bboxMinLonUDeg = readI32LE(hdr + kOffBbox + 0);
    facts.bboxMinLatUDeg = readI32LE(hdr + kOffBbox + 4);
    facts.bboxMaxLonUDeg = readI32LE(hdr + kOffBbox + 8);
    facts.bboxMaxLatUDeg = readI32LE(hdr + kOffBbox + 12);
    if (facts.bboxMinLonUDeg > facts.bboxMaxLonUDeg
            || facts.bboxMinLatUDeg > facts.bboxMaxLatUDeg) {
        return false;
    }

    out = facts;
    return true;
}

void PackCatalog::collectAttribution(const char* fullPath)
{
    char attr[kMaxAttrLen];
    if (!SDK::RawTiles::Container::peekAttribution(mKernel.fs, fullPath, attr, sizeof(attr))) {
        // No ATTR, a malformed one, or one too long to hold. All three mean
        // the same thing to the screen that has to show a credit: this pack's
        // cannot be shown.
        ++mUnattributed;
        return;
    }
    for (size_t i = 0; i < mAttrCount; ++i) {
        if (std::strcmp(mAttributions[i], attr) == 0) {
            return;     // already crediting this source
        }
    }
    if (mAttrCount >= kMaxAttributions) {
        ++mUnattributed;
        return;
    }
    std::memcpy(mAttributions[mAttrCount], attr, std::strlen(attr) + 1);
    ++mAttrCount;
}

size_t PackCatalog::rescan()
{
    mCount        = 0;
    mScanned      = true;
    mAttrCount    = 0;
    mUnattributed = 0;

    std::unique_ptr<SDK::Interface::IDirectory> dir = mKernel.fs.dir(kMapsDir);
    if (!dir || !dir->open()) {
        // Normal, not an error: SharedData/maps does not exist until someone
        // deploys a pack there.
        return 0;
    }

    SDK::Interface::IFileSystem::ObjectInfo item {};
    while (mCount < kMaxPacks && dir->readNext(item)) {
        if (item.isDir || !hasExtension(item.name, kPackExtension)) {
            continue;   // skips subdirectories and Map Manager's own .trust files
        }
        const size_t nameLen = std::strlen(item.name);
        if (nameLen >= kMaxNameLen) {
            continue;   // cannot store the name, so cannot name the pack later
        }

        char fullPath[SDK::Interface::IFileSystem::skMaxPathLen];
        if (!fullPathFor(item.name, fullPath, sizeof(fullPath))) {
            continue;
        }

        PackFacts facts {};
        if (!peek(fullPath, facts)) {
            continue;
        }

        // Ask Map Manager whether this exact file is already known corrupt.
        // Only Bad matters here; Good vs "not yet" is decided per-tick by
        // MapSession, because it changes while the app is running and this
        // does not. The (size, crc) guard is applied by verdictFor().
        uint64_t sizeNow    = 0;
        uint32_t crcDeclared = 0;
        char markerPath[SDK::Interface::IFileSystem::skMaxPathLen];
        if (declaredCrc(fullPath, sizeNow, crcDeclared)
                && PackTrustReader::markerPathFor(fullPath, markerPath, sizeof(markerPath))) {
            const PackTrustReader marker(mKernel, markerPath);
            facts.knownCorrupt =
                marker.verdictFor(sizeNow, crcDeclared) == PackTrustReader::Trust::Bad;
        }

        // memcpy, not snprintf: nameLen was bounded above, and snprintf here
        // draws a -Wformat-truncation warning for a truncation that cannot
        // happen -- which is worse than useless, because a real one would then
        // be lost in the noise.
        std::memcpy(mNames[mCount], item.name, nameLen + 1);
        facts.name      = mNames[mCount];
        mFacts[mCount]  = facts;
        ++mCount;

        // After the pack is accepted, so nothing that failed the peek can
        // contribute a credit — a pack this app will never draw owes none.
        collectAttribution(fullPath);
    }

    dir->close();
    return mCount;
}

} // namespace MapKit
