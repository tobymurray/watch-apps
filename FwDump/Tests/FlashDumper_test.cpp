/**
 * @file FlashDumper_test.cpp
 * @brief Drives the chunk/CRC/manifest/resume loop against a synthetic region.
 *
 * The dumper reads its region through a window pointer rather than a hardcoded
 * address, which is what makes all of this testable: here the window is a
 * std::vector standing in for flash, and everything the app does with it --
 * tiling it into chunks, hashing it, writing it, deciding what to skip on a
 * resume, formatting the manifest -- is the same code that runs on the watch.
 *
 * What this cannot test is the part that is genuinely device-only: that the
 * real 0x08000000 is readable from an app, and that a bare relative filename
 * lands in the app's own sandbox folder. Both are noted in the README as
 * hardware-only claims rather than papered over here.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Crc32.hpp"
#include "DumpRegion.hpp"
#include "FlashDumper.hpp"
#include "KernelTestDoubles.hpp"

namespace {

/// A region small enough to dump inside a test but with the same shape as the
/// real one: several chunks, several sub-writes per chunk.
DumpRegion smallRegion()
{
    DumpRegion region;
    region.base     = 0x08000000u;
    region.size     = 4096u;
    region.chunk    = 1024u;
    region.subwrite = 256u;
    return region;
}

std::vector<uint8_t> syntheticFlash(size_t size)
{
    std::vector<uint8_t> flash(size);
    for (size_t i = 0; i < size; ++i) {
        flash[i] = static_cast<uint8_t>((i * 31 + 7) & 0xFF);
    }
    return flash;
}

std::string chunkName(uint32_t off)
{
    char name[32];
    std::snprintf(name, sizeof(name), "dump_%06lX.bin", static_cast<unsigned long>(off));
    return name;
}

/// Runs a dumper to a terminal state, with a step budget small enough that the
/// slicing is genuinely exercised rather than the whole pass happening in one
/// call. Bounded so a loop bug fails the test instead of hanging it.
void runToCompletion(FlashDumper& dumper, size_t budget = 256)
{
    for (int guard = 0; guard < 100000; ++guard) {
        if (dumper.state() != FlashDumper::State::Dumping
                && dumper.state() != FlashDumper::State::Checking) {
            return;
        }
        dumper.step(budget);
    }
    FAIL() << "dumper did not reach a terminal state";
}

} // namespace

TEST(DumpRegionTest, DefaultsAreTheFlashOfThisWatch)
{
    const DumpRegion region;
    EXPECT_EQ(0x08000000u, region.base);
    EXPECT_EQ(0x00400000u, region.size);
    EXPECT_EQ(0x00020000u, region.chunk);
    EXPECT_EQ(32u, region.nchunks());
    EXPECT_TRUE(region.valid());
}

TEST(DumpRegionTest, RejectsGeometryTheLoopCouldNotHonour)
{
    DumpRegion region = smallRegion();

    // A size that is not a whole number of chunks would drop the remainder with
    // no manifest line to say so.
    region.size = 4097u;
    EXPECT_FALSE(region.valid());

    // A chunk that is not a whole number of sub-writes would make the last
    // sub-write of every chunk overrun into the next.
    region      = smallRegion();
    region.chunk = 1000u;
    region.size  = 4000u;
    EXPECT_FALSE(region.valid());

    region = smallRegion();
    region.subwrite = 0u;
    EXPECT_FALSE(region.valid());

    region = smallRegion();
    region.subwrite = DumpRegion::kMaxSubwrite * 2u;
    region.chunk    = region.subwrite;
    region.size     = region.subwrite;
    EXPECT_FALSE(region.valid()) << "a sub-write larger than the read-back buffer";

    // A region that wraps the address space.
    region      = smallRegion();
    region.base = 0xFFFFF000u;
    region.size = 0x00020000u;
    region.chunk = 0x00010000u;
    region.subwrite = 0x1000u;
    EXPECT_FALSE(region.valid());
}

TEST(FlashDumperTest, WritesEveryChunkAndACompleteManifest)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpRegion region = smallRegion();
    const std::vector<uint8_t> flash = syntheticFlash(region.size);

    FlashDumper dumper(fixture.kernel, region, flash.data());
    dumper.beginDump();
    runToCompletion(dumper);

    ASSERT_EQ(FlashDumper::State::Done, dumper.state()) << "error " << static_cast<unsigned>(dumper.error());
    EXPECT_EQ(region.nchunks(), dumper.chunksDone());
    EXPECT_EQ(0u, dumper.chunksVerified()) << "a fresh run has nothing to re-verify";

    // Every chunk file holds exactly its slice of the region.
    for (unsigned i = 0; i < region.nchunks(); ++i) {
        const uint32_t off = i * region.chunk;
        const std::string content = fixture.fileSystem.readFile(chunkName(off));
        ASSERT_EQ(region.chunk, content.size()) << "chunk " << i;
        EXPECT_EQ(0, std::memcmp(content.data(), flash.data() + off, region.chunk)) << "chunk " << i;
    }

    // The whole-image CRC is the CRC of the region, in order.
    EXPECT_EQ(Crc32::of(flash.data(), flash.size()), dumper.wholeCrc());

    const std::string manifest = fixture.fileSystem.readFile(FlashDumper::kManifestName);
    EXPECT_NE(std::string::npos, manifest.find("nchunks=4"));
    EXPECT_NE(std::string::npos, manifest.find("DUMP whole_image_crc32="));
    EXPECT_NE(std::string::npos, manifest.find("DUMP spot addr=08000000 "));
    EXPECT_EQ(std::string::npos, manifest.find("ok=N")) << "no chunk should have failed";
}

TEST(FlashDumperTest, ManifestGainsALinePerChunkAsItGoes)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpRegion region = smallRegion();
    const std::vector<uint8_t> flash = syntheticFlash(region.size);

    FlashDumper dumper(fixture.kernel, region, flash.data());
    dumper.beginDump();

    // The header must be on disk before any chunk is: without it the host
    // cannot interpret whatever chunk files an interrupted run left behind.
    std::string manifest = fixture.fileSystem.readFile(FlashDumper::kManifestName);
    EXPECT_NE(std::string::npos, manifest.find("DUMP base=08000000"));
    EXPECT_EQ(std::string::npos, manifest.find("DUMP chunk="));

    unsigned seen = 0;
    while (dumper.state() == FlashDumper::State::Dumping) {
        dumper.step(region.chunk);
        manifest = fixture.fileSystem.readFile(FlashDumper::kManifestName);

        // Whatever is on disk is always a whole number of lines, and never
        // describes more chunks than have actually finished.
        ASSERT_FALSE(manifest.empty());
        EXPECT_EQ('\n', manifest.back());

        unsigned lines = 0;
        for (size_t at = manifest.find("DUMP chunk="); at != std::string::npos;
             at = manifest.find("DUMP chunk=", at + 1)) {
            ++lines;
        }
        EXPECT_EQ(dumper.chunksDone(), lines);
        seen = lines;
    }
    EXPECT_EQ(region.nchunks(), seen);
}

TEST(FlashDumperTest, ResumeSkipsChunksAlreadyOnDiskAndRewritesTheRest)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpRegion region = smallRegion();
    const std::vector<uint8_t> flash = syntheticFlash(region.size);

    // First run, completed.
    {
        FlashDumper dumper(fixture.kernel, region, flash.data());
        dumper.beginDump();
        runToCompletion(dumper);
        ASSERT_EQ(FlashDumper::State::Done, dumper.state());
    }

    // Throw away the last two chunks, as an interrupted run would have left
    // them: absent entirely.
    ASSERT_TRUE(fixture.fileSystem.remove(chunkName(2 * region.chunk).c_str()));
    ASSERT_TRUE(fixture.fileSystem.remove(chunkName(3 * region.chunk).c_str()));

    FlashDumper dumper(fixture.kernel, region, flash.data());
    dumper.beginDump();
    runToCompletion(dumper);

    ASSERT_EQ(FlashDumper::State::Done, dumper.state());
    EXPECT_EQ(region.nchunks(), dumper.chunksDone());
    EXPECT_EQ(2u, dumper.chunksVerified()) << "the two surviving chunks should be skipped, not rewritten";

    // The result is a complete, correct dump either way.
    for (unsigned i = 0; i < region.nchunks(); ++i) {
        const uint32_t off = i * region.chunk;
        const std::string content = fixture.fileSystem.readFile(chunkName(off));
        ASSERT_EQ(region.chunk, content.size()) << "chunk " << i;
        EXPECT_EQ(0, std::memcmp(content.data(), flash.data() + off, region.chunk)) << "chunk " << i;
    }
    EXPECT_EQ(Crc32::of(flash.data(), flash.size()), dumper.wholeCrc())
        << "the whole-image CRC must not depend on which chunks were rewritten";
}

TEST(FlashDumperTest, RewritesAChunkWhoseContentsDoNotMatchMemory)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpRegion region = smallRegion();
    const std::vector<uint8_t> flash = syntheticFlash(region.size);

    // A chunk file of exactly the right size but the wrong bytes: a torn write,
    // or a chunk left over from different firmware. Trusting the size alone
    // would carry this corruption into the reassembled image.
    fixture.fileSystem.seedFile(chunkName(0), std::string(region.chunk, '\xAA'));

    FlashDumper dumper(fixture.kernel, region, flash.data());
    dumper.beginDump();
    runToCompletion(dumper);

    ASSERT_EQ(FlashDumper::State::Done, dumper.state());
    EXPECT_EQ(0u, dumper.chunksVerified()) << "a mismatching chunk is not a verified one";

    const std::string content = fixture.fileSystem.readFile(chunkName(0));
    ASSERT_EQ(region.chunk, content.size());
    EXPECT_EQ(0, std::memcmp(content.data(), flash.data(), region.chunk))
        << "the mismatching chunk should have been rewritten from memory";
    EXPECT_EQ(Crc32::of(flash.data(), flash.size()), dumper.wholeCrc())
        << "a rewrite must not fold the same bytes into the running CRC twice";
}

TEST(FlashDumperTest, RewritesAChunkOfTheWrongSize)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpRegion region = smallRegion();
    const std::vector<uint8_t> flash = syntheticFlash(region.size);

    // A half-written chunk from a run that was interrupted mid-write.
    fixture.fileSystem.seedFile(chunkName(region.chunk), std::string(region.chunk / 2, '\x00'));

    FlashDumper dumper(fixture.kernel, region, flash.data());
    dumper.beginDump();
    runToCompletion(dumper);

    ASSERT_EQ(FlashDumper::State::Done, dumper.state());
    const std::string content = fixture.fileSystem.readFile(chunkName(region.chunk));
    ASSERT_EQ(region.chunk, content.size());
    EXPECT_EQ(0, std::memcmp(content.data(), flash.data() + region.chunk, region.chunk));
}

TEST(FlashDumperTest, ReportsAShortWriteRatherThanClaimingSuccess)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpRegion region = smallRegion();
    const std::vector<uint8_t> flash = syntheticFlash(region.size);

    // Storage that stops accepting bytes partway through -- a full volume. The
    // manifest header write happens first, so allow for it.
    fixture.fileSystem.failWritesAfterBytes = 1500;

    FlashDumper dumper(fixture.kernel, region, flash.data());
    dumper.beginDump();
    runToCompletion(dumper);

    ASSERT_EQ(FlashDumper::State::Error, dumper.state());
    EXPECT_TRUE(dumper.error() == FlashDumper::Error::ShortWrite
                || dumper.error() == FlashDumper::Error::ManifestFailed)
        << "error was " << static_cast<unsigned>(dumper.error());
    EXPECT_LT(dumper.chunksDone(), region.nchunks());
}

TEST(FlashDumperTest, RefusesAnIncoherentRegion)
{
    SDK::TestSupport::KernelFixture fixture;
    DumpRegion region = smallRegion();
    region.size = 4097u; // Not a whole number of chunks.
    const std::vector<uint8_t> flash = syntheticFlash(8192);

    FlashDumper dumper(fixture.kernel, region, flash.data());
    dumper.beginDump();

    EXPECT_EQ(FlashDumper::State::Error, dumper.state());
    EXPECT_EQ(FlashDumper::Error::BadRegion, dumper.error());
    EXPECT_EQ(0u, dumper.chunksDone());
}

TEST(FlashDumperTest, PresenceScanCountsRightSizedChunksOnly)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpRegion region = smallRegion();
    const std::vector<uint8_t> flash = syntheticFlash(region.size);

    fixture.fileSystem.seedFile(chunkName(0), std::string(region.chunk, '\x11'));
    fixture.fileSystem.seedFile(chunkName(region.chunk), std::string(region.chunk / 2, '\x22'));

    FlashDumper dumper(fixture.kernel, region, flash.data());
    dumper.beginScan();
    runToCompletion(dumper);

    EXPECT_EQ(FlashDumper::State::Idle, dumper.state());
    EXPECT_TRUE(dumper.scanComplete());
    EXPECT_EQ(1u, dumper.chunksPresent())
        << "the half-sized chunk is not a chunk that can be skipped";
}

TEST(FlashDumperTest, ASecondStartWhileDumpingIsANoOp)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpRegion region = smallRegion();
    const std::vector<uint8_t> flash = syntheticFlash(region.size);

    FlashDumper dumper(fixture.kernel, region, flash.data());
    dumper.beginDump();
    dumper.step(region.chunk + region.subwrite); // Part-way into the second chunk.
    ASSERT_EQ(FlashDumper::State::Dumping, dumper.state());
    const unsigned progressed = dumper.chunksDone();
    ASSERT_GT(progressed, 0u);

    dumper.beginDump(); // The user pressed start again.
    EXPECT_EQ(FlashDumper::State::Dumping, dumper.state());
    EXPECT_EQ(progressed, dumper.chunksDone()) << "a double-start must not rewind the pass";

    runToCompletion(dumper);
    EXPECT_EQ(FlashDumper::State::Done, dumper.state());
}

TEST(FlashDumperTest, LeavesNoOpenHandlesBehind)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpRegion region = smallRegion();
    const std::vector<uint8_t> flash = syntheticFlash(region.size);

    FlashDumper dumper(fixture.kernel, region, flash.data());
    dumper.beginDump();
    runToCompletion(dumper);
    ASSERT_EQ(FlashDumper::State::Done, dumper.state());

    // Every handle opened must have been closed. On the real filesystem an
    // unclosed handle holds a FatFs lock slot, and there are only a handful --
    // a dump of 32 chunks would exhaust them long before finishing.
    for (const auto& entry : fixture.fileSystem.openHandles) {
        EXPECT_EQ(0u, entry.second) << "handle left open on " << entry.first;
    }
}

TEST(FlashDumperTest, EachChunkFileIsFlushedBeforeTheNextIsOpened)
{
    SDK::TestSupport::KernelFixture fixture;
    const DumpRegion region = smallRegion();
    const std::vector<uint8_t> flash = syntheticFlash(region.size);

    FlashDumper dumper(fixture.kernel, region, flash.data());
    dumper.beginDump();
    runToCompletion(dumper);
    ASSERT_EQ(FlashDumper::State::Done, dumper.state());

    // Resumability rests on this: a chunk file that was never flushed may not
    // survive the power loss that interrupted the dump, and a resumed run would
    // then skip a chunk whose bytes never reached the medium.
    for (unsigned i = 0; i < region.nchunks(); ++i) {
        const std::string name = chunkName(i * region.chunk);
        EXPECT_GE(fixture.fileSystem.flushCounts[name], 1u) << "never flushed: " << name;
    }
}
