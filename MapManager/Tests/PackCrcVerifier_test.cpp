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
std::string buildValidPack()
{
    std::string body;
    for (int i = 0; i < 4000; ++i) {
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
