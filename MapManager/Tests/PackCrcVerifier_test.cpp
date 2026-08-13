/**
 * @file    PackCrcVerifier_test.cpp
 * @brief   Host tests for PackCrcVerifier and PackTrustMarker.
 *
 * Uses SDK::TestSupport::KernelFixture's InMemoryFileSystem -- the same
 * fixture pattern as the SDK's own apps/Running/SettingsSerializer_test.cpp,
 * and the direct ancestor of this test file: AthensRun's
 * MapPackCrcVerifier_test.cpp, ported here with the class renamed and the
 * constructor taking an explicit path (this app discovers paths via
 * directory scan; the verifier itself no longer resolves them).
 */
#include <gtest/gtest.h>

#include <array>
#include <string>

#include "KernelTestDoubles.hpp"
#include "PackCrcVerifier.hpp"
#include "PackTrustMarker.hpp"

namespace {

using SDK::TestSupport::KernelFixture;

uint32_t crc32(const std::string& data)
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char b : data) {
        crc = table[(crc ^ b) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

void appendU32LE(std::string& out, uint32_t v)
{
    out.push_back(static_cast<char>(v));
    out.push_back(static_cast<char>(v >> 8));
    out.push_back(static_cast<char>(v >> 16));
    out.push_back(static_cast<char>(v >> 24));
}

const std::string kPackPath   = "../SharedData/maps/test.rawtiles";
const std::string kMarkerPath = kPackPath + ".trust";

// A few KB of arbitrary content, correct trailing CRC-32 appended -- enough
// bytes to exercise several step() calls at a small chunk size. Verified
// independently of the spec's pinned test vector (0xCBF43926 for ASCII
// "123456789") isn't needed here: this class is format-agnostic and never
// claims rawtiles conformance, unlike AthensRun's Container-level tests.
std::string buildValidPack(size_t bodyBytes = 4000)
{
    std::string body;
    for (size_t i = 0; i < bodyBytes; ++i) {
        body.push_back(static_cast<char>('A' + (i % 26)));
    }
    appendU32LE(body, crc32(body));
    return body;
}

std::string buildCorruptedPack()
{
    std::string pack = buildValidPack();
    pack[pack.size() - 1] ^= 0xFF; // flip a footer byte -> declared CRC now wrong
    return pack;
}

void runToCompletion(PackCrcVerifier& verifier, size_t chunkBytes = 64)
{
    ASSERT_NE(verifier.start(), PackCrcVerifier::Status::IoError);
    int guard = 0;
    while (!verifier.done()) {
        verifier.step(chunkBytes);
        ASSERT_LT(++guard, 100000) << "step() never completed -- possible infinite loop";
    }
}

TEST(PackCrcVerifier, IoErrorWhenFileDoesNotExist)
{
    KernelFixture fixture;
    PackCrcVerifier verifier(fixture.kernel, kPackPath);
    EXPECT_EQ(verifier.start(), PackCrcVerifier::Status::IoError);
    EXPECT_TRUE(verifier.done());
}

TEST(PackCrcVerifier, FullCycleWritesGoodMarker)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(kPackPath, buildValidPack());

    PackCrcVerifier verifier(fixture.kernel, kPackPath);
    runToCompletion(verifier);

    EXPECT_EQ(verifier.status(), PackCrcVerifier::Status::Verified);

    PackTrustMarker marker(fixture.kernel, kMarkerPath.c_str());
    uint64_t markedSize = 0;
    uint32_t markedCrc  = 0;
    EXPECT_EQ(marker.read(markedSize, markedCrc), PackTrustMarker::Trust::Good);
    EXPECT_EQ(markedSize, buildValidPack().size());
}

TEST(PackCrcVerifier, SecondInstanceSkipsScanWhenMarkerAlreadyGood)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(kPackPath, buildValidPack());

    PackCrcVerifier first(fixture.kernel, kPackPath);
    runToCompletion(first);
    ASSERT_EQ(first.status(), PackCrcVerifier::Status::Verified);

    PackCrcVerifier second(fixture.kernel, kPackPath);
    EXPECT_EQ(second.start(), PackCrcVerifier::Status::Verified);
    EXPECT_TRUE(second.done());
    EXPECT_EQ(second.step(), PackCrcVerifier::Status::Verified); // no-op once done
}

TEST(PackCrcVerifier, CorruptedPackWritesBadMarker)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(kPackPath, buildCorruptedPack());

    PackCrcVerifier verifier(fixture.kernel, kPackPath);
    runToCompletion(verifier);

    EXPECT_EQ(verifier.status(), PackCrcVerifier::Status::Mismatched);

    PackTrustMarker marker(fixture.kernel, kMarkerPath.c_str());
    uint64_t markedSize = 0;
    uint32_t markedCrc  = 0;
    EXPECT_EQ(marker.read(markedSize, markedCrc), PackTrustMarker::Trust::Bad);
}

TEST(PackCrcVerifier, BytesDoneAndTotalTrackProgress)
{
    KernelFixture fixture;
    const std::string pack = buildValidPack();
    fixture.fileSystem.seedFile(kPackPath, pack);

    PackCrcVerifier verifier(fixture.kernel, kPackPath);
    ASSERT_EQ(verifier.start(), PackCrcVerifier::Status::InProgress);
    EXPECT_EQ(verifier.bytesTotal(), pack.size() - 4);
    EXPECT_EQ(verifier.bytesDone(), 0u);

    verifier.step(64);
    EXPECT_EQ(verifier.bytesDone(), 64u);
}

// --------------------------------------------------------------------------
// The (size, crc) guard on the cached-verdict fast path.
//
// This is the load-bearing expression in start(): every skipped scan is only
// safe because of it. Both of its false branches need to force a real scan,
// so both are pinned here rather than left to the happy-path test above.
// --------------------------------------------------------------------------
TEST(PackCrcVerifier, GoodMarkerForADifferentSizeIsIgnored)
{
    KernelFixture fixture;
    const std::string pack = buildValidPack();
    fixture.fileSystem.seedFile(kPackPath, pack);

    // A Good marker claiming the right CRC but the wrong length.
    PackTrustMarker(fixture.kernel, kMarkerPath.c_str())
        .writeGood(pack.size() + 1, crc32(pack.substr(0, pack.size() - 4)));

    PackCrcVerifier verifier(fixture.kernel, kPackPath);
    EXPECT_EQ(verifier.start(), PackCrcVerifier::Status::InProgress)
        << "a Good marker whose size does not match must not short-circuit the scan";
}

TEST(PackCrcVerifier, GoodMarkerForADifferentCrcIsIgnored)
{
    KernelFixture fixture;
    const std::string pack = buildValidPack();
    fixture.fileSystem.seedFile(kPackPath, pack);

    // A Good marker claiming the right length but some other CRC.
    PackTrustMarker(fixture.kernel, kMarkerPath.c_str()).writeGood(pack.size(), 0x12345678u);

    PackCrcVerifier verifier(fixture.kernel, kPackPath);
    EXPECT_EQ(verifier.start(), PackCrcVerifier::Status::InProgress)
        << "a Good marker whose CRC does not match must not short-circuit the scan";
}

TEST(PackCrcVerifier, StaleMarkerFromAReplacedPackIsIgnoredAndRescanned)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(kPackPath, buildValidPack());

    PackCrcVerifier first(fixture.kernel, kPackPath);
    runToCompletion(first);
    ASSERT_EQ(first.status(), PackCrcVerifier::Status::Verified);

    // Redeploy a different pack over the same path. The marker on disk still
    // says Good, but for the old pack's (size, crc).
    fixture.fileSystem.seedFile(kPackPath, buildValidPack(8000));

    PackCrcVerifier second(fixture.kernel, kPackPath);
    ASSERT_EQ(second.start(), PackCrcVerifier::Status::InProgress);
    while (!second.done()) {
        second.step();
    }
    EXPECT_EQ(second.status(), PackCrcVerifier::Status::Verified);

    uint64_t markedSize = 0;
    uint32_t markedCrc  = 0;
    ASSERT_EQ(PackTrustMarker(fixture.kernel, kMarkerPath.c_str()).read(markedSize, markedCrc),
              PackTrustMarker::Trust::Good);
    EXPECT_EQ(markedSize, 8004u) << "the marker must now describe the new pack";
}

// --------------------------------------------------------------------------
// A cached Bad verdict is honoured, so a corrupt pack is not re-scanned in
// full on every boot for the rest of the device's life.
// --------------------------------------------------------------------------
TEST(PackCrcVerifier, CachedBadMarkerSkipsTheRescan)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(kPackPath, buildCorruptedPack());

    PackCrcVerifier first(fixture.kernel, kPackPath);
    runToCompletion(first);
    ASSERT_EQ(first.status(), PackCrcVerifier::Status::Mismatched);

    PackCrcVerifier second(fixture.kernel, kPackPath);
    EXPECT_EQ(second.start(), PackCrcVerifier::Status::Mismatched);
    EXPECT_TRUE(second.done());
    EXPECT_EQ(second.bytesDone(), 0u) << "the cached Bad verdict must cost no scan I/O";
}

TEST(PackCrcVerifier, BadMarkerIsIgnoredOnceTheFileIsReplaced)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(kPackPath, buildCorruptedPack());

    PackCrcVerifier first(fixture.kernel, kPackPath);
    runToCompletion(first);
    ASSERT_EQ(first.status(), PackCrcVerifier::Status::Mismatched);

    // A good copy of the same pack is redeployed. Same length, but its footer
    // now declares the CRC that actually matches its body, so the Bad
    // marker's (size, crc) no longer describes it.
    fixture.fileSystem.seedFile(kPackPath, buildValidPack());

    PackCrcVerifier second(fixture.kernel, kPackPath);
    ASSERT_EQ(second.start(), PackCrcVerifier::Status::InProgress)
        << "a Bad verdict must not outlive the bytes it was about";
    while (!second.done()) {
        second.step();
    }
    EXPECT_EQ(second.status(), PackCrcVerifier::Status::Verified);
}

// --------------------------------------------------------------------------
// Size edge cases around the 4-byte footer.
// --------------------------------------------------------------------------
TEST(PackCrcVerifier, FileExactlyFooterSizedHasAnEmptyScanRegion)
{
    KernelFixture fixture;
    std::string pack;
    appendU32LE(pack, crc32("")); // CRC of no bytes at all
    ASSERT_EQ(pack.size(), 4u);
    fixture.fileSystem.seedFile(kPackPath, pack);

    PackCrcVerifier verifier(fixture.kernel, kPackPath);
    ASSERT_EQ(verifier.start(), PackCrcVerifier::Status::InProgress);
    EXPECT_EQ(verifier.bytesTotal(), 0u);
    EXPECT_EQ(verifier.step(), PackCrcVerifier::Status::Verified)
        << "a zero-length scan region must terminate, not underflow or spin";
}

TEST(PackCrcVerifier, FileOneByteLargerThanTheFooter)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(kPackPath, buildValidPack(1));

    PackCrcVerifier verifier(fixture.kernel, kPackPath);
    ASSERT_EQ(verifier.start(), PackCrcVerifier::Status::InProgress);
    EXPECT_EQ(verifier.bytesTotal(), 1u);
    runToCompletion(verifier);
    EXPECT_EQ(verifier.status(), PackCrcVerifier::Status::Verified);
}

TEST(PackCrcVerifier, ShorterThanTheFooterIsAnIoError)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(kPackPath, "abc"); // 3 bytes: no room for a footer

    PackCrcVerifier verifier(fixture.kernel, kPackPath);
    EXPECT_EQ(verifier.start(), PackCrcVerifier::Status::IoError);
}

// --------------------------------------------------------------------------
// step()'s budget: one call spends the whole budget, in kIoChunkBytes reads.
// Throughput depends on this -- a call that did one chunk regardless of the
// budget would tie the scan rate to the caller's loop period.
// --------------------------------------------------------------------------
TEST(PackCrcVerifier, StepSpendsItsWholeBudgetInOneCall)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(kPackPath, buildValidPack(64 * 1024));

    PackCrcVerifier verifier(fixture.kernel, kPackPath);
    ASSERT_EQ(verifier.start(), PackCrcVerifier::Status::InProgress);

    const size_t budget = 5 * PackCrcVerifier::kIoChunkBytes;
    verifier.step(budget);
    EXPECT_EQ(verifier.bytesDone(), budget)
        << "step() must consume its whole budget, not a single I/O chunk";

    verifier.step(budget);
    EXPECT_EQ(verifier.bytesDone(), 2 * budget);
}

TEST(PackCrcVerifier, StepIsClampedToTheScanRegionNotTheBudget)
{
    KernelFixture fixture;
    const std::string pack = buildValidPack(1000);
    fixture.fileSystem.seedFile(kPackPath, pack);

    PackCrcVerifier verifier(fixture.kernel, kPackPath);
    ASSERT_EQ(verifier.start(), PackCrcVerifier::Status::InProgress);

    // A budget far larger than the file must finish it, not overrun it.
    EXPECT_EQ(verifier.step(1024 * 1024), PackCrcVerifier::Status::Verified);
    EXPECT_EQ(verifier.bytesDone(), 1000u);
}

TEST(PackCrcVerifier, StepWithAZeroBudgetIsANoOp)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(kPackPath, buildValidPack());

    PackCrcVerifier verifier(fixture.kernel, kPackPath);
    ASSERT_EQ(verifier.start(), PackCrcVerifier::Status::InProgress);
    EXPECT_EQ(verifier.step(0), PackCrcVerifier::Status::InProgress);
    EXPECT_EQ(verifier.bytesDone(), 0u);
}

// --------------------------------------------------------------------------
// reset(): how Service re-arms an entry whose file changed underneath it.
// --------------------------------------------------------------------------
TEST(PackCrcVerifier, ResetReturnsToIdleAndReleasesTheFileHandle)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(kPackPath, buildValidPack());

    PackCrcVerifier verifier(fixture.kernel, kPackPath);
    ASSERT_EQ(verifier.start(), PackCrcVerifier::Status::InProgress);
    verifier.step(64);
    ASSERT_GT(verifier.bytesDone(), 0u);
    ASSERT_EQ(fixture.fileSystem.openHandles[kPackPath], 1u);

    verifier.reset();
    EXPECT_EQ(verifier.status(), PackCrcVerifier::Status::Idle);
    EXPECT_EQ(verifier.bytesDone(), 0u);
    EXPECT_EQ(verifier.fileSize(), 0u);
    EXPECT_EQ(fixture.fileSystem.openHandles[kPackPath], 0u)
        << "reset() must not leak the open handle (a FatFs lock slot on device)";

    // And the verifier is reusable afterwards.
    ASSERT_EQ(verifier.start(), PackCrcVerifier::Status::InProgress);
    runToCompletion(verifier);
    EXPECT_EQ(verifier.status(), PackCrcVerifier::Status::Verified);
}

TEST(PackCrcVerifier, VerifiedPassLeavesNoOpenHandle)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(kPackPath, buildValidPack());

    PackCrcVerifier verifier(fixture.kernel, kPackPath);
    runToCompletion(verifier);
    ASSERT_EQ(verifier.status(), PackCrcVerifier::Status::Verified);
    EXPECT_EQ(fixture.fileSystem.openHandles[kPackPath], 0u);
    EXPECT_EQ(fixture.fileSystem.openHandles[kMarkerPath], 0u);
}

TEST(PackTrustMarker, RoundTripsGoodAndBad)
{
    KernelFixture fixture;
    PackTrustMarker marker(fixture.kernel, "test.trust");

    ASSERT_TRUE(marker.writeGood(12345, 0xDEADBEEFu));
    uint64_t size = 0;
    uint32_t crc  = 0;
    EXPECT_EQ(marker.read(size, crc), PackTrustMarker::Trust::Good);
    EXPECT_EQ(size, 12345u);
    EXPECT_EQ(crc, 0xDEADBEEFu);

    ASSERT_TRUE(marker.writeBad(999, 0x11223344u));
    EXPECT_EQ(marker.read(size, crc), PackTrustMarker::Trust::Bad);
    EXPECT_EQ(size, 999u);
    EXPECT_EQ(crc, 0x11223344u);
}

TEST(PackTrustMarker, ReadOnAbsentPathIsAbsent)
{
    KernelFixture fixture;
    PackTrustMarker marker(fixture.kernel, "nonexistent.trust");
    uint64_t size = 0;
    uint32_t crc  = 0;
    EXPECT_EQ(marker.read(size, crc), PackTrustMarker::Trust::Absent);
}

TEST(PackTrustMarker, ReadOnGarbageOrShortFileIsAbsent)
{
    KernelFixture fixture;

    fixture.fileSystem.seedFile("short.trust", "abc"); // 3 bytes, not 16
    fixture.fileSystem.seedFile("garbage.trust", std::string(16, '\x7F')); // 16 bytes, bad magic

    uint64_t size = 0;
    uint32_t crc  = 0;
    EXPECT_EQ(PackTrustMarker(fixture.kernel, "short.trust").read(size, crc),
              PackTrustMarker::Trust::Absent);
    EXPECT_EQ(PackTrustMarker(fixture.kernel, "garbage.trust").read(size, crc),
              PackTrustMarker::Trust::Absent);
}

} // namespace
