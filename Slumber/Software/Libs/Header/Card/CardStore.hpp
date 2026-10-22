#ifndef SLUMBER_CARD_STORE_HPP
#define SLUMBER_CARD_STORE_HPP

#include <cstdint>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"

#include "SlumberCard.hpp"

/// Where the card lives, and the only code that touches its files.
namespace SlumberCard
{

constexpr char kDir[]     = "../SharedData/slumber";
constexpr char kPath[]    = "../SharedData/slumber/card";
constexpr char kTmpPath[] = "../SharedData/slumber/card.tmp";
constexpr char kBakPath[] = "../SharedData/slumber/card.bak";

/// Writes `card` so that a crash at any step leaves one intact copy.
/// `rename()` will not replace an existing file on the watch, so this is
/// RecordingMarker's rotate: tmp, drop .bak, primary -> .bak, tmp -> primary.
inline bool publish(SDK::Interface::IFileSystem &fs, const Card &card)
{
    if (!fs.mkdir(kDir)) {
        return false;
    }

    uint8_t bytes[kSize];
    encode(card, bytes);

    {
        std::unique_ptr<SDK::Interface::IFile> tmp = fs.file(kTmpPath);
        if (!tmp || !tmp->open(true, true)) {
            return false;
        }
        size_t written = 0;
        bool   ok      = tmp->write(reinterpret_cast<const char *>(bytes), kSize, written) &&
                  written == kSize;
        ok = tmp->flush() && ok;
        tmp->close();
        if (!ok) {
            fs.remove(kTmpPath);
            return false;
        }
    }

    if (fs.exist(kPath)) {
        if (fs.exist(kBakPath)) {
            fs.remove(kBakPath);
        }
        fs.rename(kPath, kBakPath);
    }
    return fs.rename(kTmpPath, kPath);
}

enum class Read : uint8_t {
    Ok,
    FromBackup,  ///< the primary was torn or missing; the .bak was good
    Absent,      ///< neither file exists: Slumber has never published
    Busy,        ///< a file exists but would not open, or neither copy decodes
};

namespace detail
{

inline Decode readOne(SDK::Interface::IFileSystem &fs, const char *path, Card &out, bool &opened)
{
    opened = false;
    std::unique_ptr<SDK::Interface::IFile> f = fs.file(path);
    if (!f || !f->open(false, false)) {
        return Decode::Short;
    }
    opened = true;
    uint8_t bytes[kSize] = {};
    size_t  got          = 0;
    const bool ok        = f->read(reinterpret_cast<char *>(bytes), kSize, got);
    f->close();
    return ok ? decode(bytes, got, out) : Decode::Short;
}

} // namespace detail

/// `open()` reports absent, locked and out-of-handles alike as `false`
/// (una-sdk PR #270), so `exist()` is what separates Absent from Busy.
inline Read load(SDK::Interface::IFileSystem &fs, Card &out)
{
    bool opened = false;
    if (detail::readOne(fs, kPath, out, opened) == Decode::Ok) {
        return Read::Ok;
    }
    bool bakOpened = false;
    if (detail::readOne(fs, kBakPath, out, bakOpened) == Decode::Ok) {
        return Read::FromBackup;
    }
    if (!fs.exist(kPath) && !fs.exist(kBakPath) && !fs.exist(kTmpPath)) {
        return Read::Absent;
    }
    return Read::Busy;
}

} // namespace SlumberCard

#endif // SLUMBER_CARD_STORE_HPP
