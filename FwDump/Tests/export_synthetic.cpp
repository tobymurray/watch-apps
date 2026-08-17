/**
 * @file export_synthetic.cpp
 * @brief Dumps a synthetic region to real files, for the real reassembler.
 *
 * Everything else in this suite checks the manifest against this repo's reading
 * of `reassemble_dump.py`'s regexes. This checks it against the script itself.
 *
 * It runs the actual FlashDumper over a synthetic region shaped like the
 * watch's flash -- 4 MB at 0x08000000, 128 kB chunks, 4 kB sub-writes, with
 * content that is pseudo-random for the first 2.04 MB and 0xFF beyond, which is
 * what the verified prior dump found real flash to look like -- and writes the
 * resulting chunk files and manifest into a directory given on the command
 * line. Pointing reassemble_dump.py at that directory then exercises the whole
 * contract: the header regex, the chunk regex, the filename derivation from
 * `off`, the per-chunk CRCs, the whole-image CRC and the spot lines.
 *
 * A mismatch here is a real finding either way round: either this app formats
 * something the script cannot read, or the two disagree about the CRC.
 *
 * Usage: fwdump-export-synthetic <output-dir>
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "DumpRegion.hpp"
#include "FlashDumper.hpp"
#include "KernelTestDoubles.hpp"

namespace {

/// Stands in for the watch's flash: a deterministic pseudo-random image
/// followed by erased space. Deterministic so a mismatch is reproducible, and
/// so the CRCs this produces are stable across runs.
std::vector<uint8_t> syntheticFlash(uint32_t size, uint32_t imageBytes)
{
    std::vector<uint8_t> flash(size, 0xFF);

    // xorshift32, written out rather than taken from <random> so the byte
    // sequence does not depend on a standard library implementation.
    uint32_t state = 0x1F2E3D4Cu;
    for (uint32_t i = 0; i < imageBytes && i < size; ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        flash[i] = static_cast<uint8_t>(state & 0xFFu);
    }
    return flash;
}

bool writeOut(const std::string& dir, const std::string& name, const std::string& content)
{
    const std::string path = dir + "/" + name;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        std::fprintf(stderr, "cannot open %s for writing\n", path.c_str());
        return false;
    }
    const size_t written = content.empty() ? 0 : std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
    if (written != content.size()) {
        std::fprintf(stderr, "short write to %s\n", path.c_str());
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output-dir>\n", argv[0]);
        return 2;
    }
    const std::string outDir = argv[1];

    // The real geometry, not a scaled-down one: the field widths in the
    // manifest depend on the magnitude of the numbers, so a small region would
    // not exercise the same formatting.
    DumpRegion region;
    const std::vector<uint8_t> flash = syntheticFlash(region.size, 0x0020A140u);

    SDK::TestSupport::KernelFixture fixture;
    FlashDumper dumper(fixture.kernel, region, flash.data());

    dumper.beginDump();
    while (dumper.state() == FlashDumper::State::Dumping) {
        dumper.step(64 * 1024);
    }

    if (dumper.state() != FlashDumper::State::Done) {
        std::fprintf(stderr, "dump did not complete: state=%u error=%u chunk=%u\n",
                     static_cast<unsigned>(dumper.state()),
                     static_cast<unsigned>(dumper.error()), dumper.errorChunk());
        return 1;
    }

    for (const auto& entry : fixture.fileSystem.files) {
        if (!writeOut(outDir, entry.first, entry.second.content)) {
            return 1;
        }
    }

    std::printf("exported %zu files to %s\n", fixture.fileSystem.files.size(), outDir.c_str());
    std::printf("chunks=%u verified=%u whole_image_crc32=%08lX\n", dumper.chunksDone(),
                dumper.chunksVerified(), static_cast<unsigned long>(dumper.wholeCrc()));
    return 0;
}
