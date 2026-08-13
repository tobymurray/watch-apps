/**
 * @file    Service_test.cpp
 * @brief   Host tests for Service's directory-scan orchestration.
 *
 * Covers what PackCrcVerifier_test.cpp cannot: discovering N files, tracking N
 * independent verifier states, and reconciling that set against a directory
 * that changes underneath it. run() itself blocks forever on the kernel
 * message queue, so these drive Service::poll() -- the one iteration of
 * background work run() performs between message waits.
 *
 * Uses the SDK's InMemoryDirectory (KernelTestDoubles), which enumerates
 * seeded files rather than always reporting an empty directory.
 */
#include <gtest/gtest.h>

#include <array>
#include <string>

#include "KernelTestDoubles.hpp"
#include "PackTrustMarker.hpp"
#include "Service.hpp"

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

std::string pack(size_t bodyBytes, char fill = 'A')
{
    std::string body(bodyBytes, fill);
    const uint32_t c = crc32(body);
    body.push_back(static_cast<char>(c));
    body.push_back(static_cast<char>(c >> 8));
    body.push_back(static_cast<char>(c >> 16));
    body.push_back(static_cast<char>(c >> 24));
    return body;
}

/// Same pack, but with the body corrupted after the footer was computed.
std::string corruptPack(size_t bodyBytes, char fill = 'A')
{
    std::string p = pack(bodyBytes, fill);
    p[0] = static_cast<char>(p[0] ^ 0xFF);
    return p;
}

const char* kMapsDir = "../SharedData/maps";

std::string mapPath(const char* name)
{
    return std::string(kMapsDir) + "/" + name;
}

/// Drive the service until nothing is pending, or the budget runs out.
/// Advances the stub clock so the rescan throttle behaves like real time.
void settle(Service& service, KernelFixture& fixture, int maxPolls = 2000)
{
    for (int i = 0; i < maxPolls; ++i) {
        service.poll();
        fixture.system.nowMs += 10;
    }
}

/// Jump past the rescan throttle so the next poll() re-lists the directory.
void advancePastRescan(KernelFixture& fixture)
{
    fixture.system.nowMs += 31000;
}

PackTrustMarker::Trust trustOf(KernelFixture& fixture, const std::string& packPath,
                               uint64_t& size, uint32_t& crc)
{
    const std::string markerPath = packPath + ".trust";
    return PackTrustMarker(fixture.kernel, markerPath.c_str()).read(size, crc);
}

// --------------------------------------------------------------------------

TEST(Service, DiscoversAndVerifiesEveryPackInTheSharedDirectory)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(mapPath("athens.rawtiles"), pack(9000, 'A'));
    fixture.fileSystem.seedFile(mapPath("wide.rawtiles"), pack(5000, 'B'));

    Service service(fixture.kernel);
    settle(service, fixture, 200);

    EXPECT_EQ(service.trackedCount(), 2u);
    EXPECT_EQ(service.verifiedCount(), 2u);

    uint64_t size = 0;
    uint32_t crc  = 0;
    EXPECT_EQ(trustOf(fixture, mapPath("athens.rawtiles"), size, crc),
              PackTrustMarker::Trust::Good);
    EXPECT_EQ(size, 9004u);
    EXPECT_EQ(trustOf(fixture, mapPath("wide.rawtiles"), size, crc),
              PackTrustMarker::Trust::Good);
    EXPECT_EQ(size, 5004u);
}

TEST(Service, IgnoresFilesThatAreNotPacks)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(mapPath("athens.rawtiles"), pack(2000));
    fixture.fileSystem.seedFile(mapPath("notes.txt"), "hello");
    fixture.fileSystem.seedFile(mapPath("rawtiles"), "no dot, no extension");
    fixture.fileSystem.seedFile(mapPath(".rawtiles"), "extension but no name");

    Service service(fixture.kernel);
    settle(service, fixture, 200);

    EXPECT_EQ(service.trackedCount(), 1u)
        << "only *.rawtiles files with an actual name should be tracked";
}

TEST(Service, DoesNotRediscoverTheTrustMarkersItWrites)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(mapPath("athens.rawtiles"), pack(2000));

    Service service(fixture.kernel);
    settle(service, fixture, 100);
    ASSERT_EQ(service.trackedCount(), 1u);

    // A .trust file now exists next to the pack; rescanning must not adopt it.
    advancePastRescan(fixture);
    settle(service, fixture, 100);
    EXPECT_EQ(service.trackedCount(), 1u);
}

TEST(Service, MarksACorruptPackBadAndKeepsGoing)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(mapPath("bad.rawtiles"), corruptPack(3000, 'A'));
    fixture.fileSystem.seedFile(mapPath("good.rawtiles"), pack(3000, 'B'));

    Service service(fixture.kernel);
    settle(service, fixture, 200);

    EXPECT_EQ(service.trackedCount(), 2u);
    EXPECT_EQ(service.verifiedCount(), 1u) << "one bad pack must not stall the other";

    uint64_t size = 0;
    uint32_t crc  = 0;
    EXPECT_EQ(trustOf(fixture, mapPath("bad.rawtiles"), size, crc), PackTrustMarker::Trust::Bad);
    EXPECT_EQ(trustOf(fixture, mapPath("good.rawtiles"), size, crc), PackTrustMarker::Trust::Good);
}

// --------------------------------------------------------------------------
// The mid-copy case: a pack discovered while it is still being written over
// USB gets a verdict about bytes that are about to change. The scan must be
// re-armed when the file settles, not left written off until a reboot.
// --------------------------------------------------------------------------
TEST(Service, ReVerifiesAPackThatGrewAfterBeingWrittenOffMidCopy)
{
    KernelFixture fixture;
    const std::string full = pack(9000, 'A');

    // A USB copy in progress: only part of the file has landed so far.
    fixture.fileSystem.seedFile(mapPath("athens.rawtiles"), full.substr(0, 3000));

    Service service(fixture.kernel);
    settle(service, fixture, 100);

    uint64_t size = 0;
    uint32_t crc  = 0;
    ASSERT_EQ(trustOf(fixture, mapPath("athens.rawtiles"), size, crc),
              PackTrustMarker::Trust::Bad)
        << "the partial file genuinely does not match its apparent footer";
    ASSERT_EQ(service.verifiedCount(), 0u);

    // The copy completes.
    fixture.fileSystem.seedFile(mapPath("athens.rawtiles"), full);
    advancePastRescan(fixture);
    settle(service, fixture, 200);

    EXPECT_EQ(service.trackedCount(), 1u) << "it must be re-armed, not tracked twice";
    EXPECT_EQ(service.verifiedCount(), 1u);
    EXPECT_EQ(trustOf(fixture, mapPath("athens.rawtiles"), size, crc),
              PackTrustMarker::Trust::Good);
    EXPECT_EQ(size, full.size());
}

TEST(Service, ReVerifiesAPackReplacedByADifferentlySizedOne)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(mapPath("athens.rawtiles"), pack(3000, 'A'));

    Service service(fixture.kernel);
    settle(service, fixture, 100);
    ASSERT_EQ(service.verifiedCount(), 1u);

    fixture.fileSystem.seedFile(mapPath("athens.rawtiles"), pack(7000, 'B'));
    advancePastRescan(fixture);
    settle(service, fixture, 200);

    uint64_t size = 0;
    uint32_t crc  = 0;
    EXPECT_EQ(service.verifiedCount(), 1u);
    EXPECT_EQ(trustOf(fixture, mapPath("athens.rawtiles"), size, crc),
              PackTrustMarker::Trust::Good);
    EXPECT_EQ(size, 7004u) << "the marker must describe the pack that is there now";
}

TEST(Service, RecoversAPackThatDidNotExistYetAtFirstScan)
{
    KernelFixture fixture;
    Service service(fixture.kernel);

    settle(service, fixture, 20);
    ASSERT_EQ(service.trackedCount(), 0u);

    fixture.fileSystem.seedFile(mapPath("athens.rawtiles"), pack(3000));
    advancePastRescan(fixture);
    settle(service, fixture, 200);

    EXPECT_EQ(service.trackedCount(), 1u);
    EXPECT_EQ(service.verifiedCount(), 1u);
}

// --------------------------------------------------------------------------
// A deleted pack must stop being counted, or the screen reports totals for
// packs that are not there.
// --------------------------------------------------------------------------
TEST(Service, DropsAPackThatDisappears)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(mapPath("athens.rawtiles"), pack(3000, 'A'));
    fixture.fileSystem.seedFile(mapPath("wide.rawtiles"), pack(3000, 'B'));

    Service service(fixture.kernel);
    settle(service, fixture, 200);
    ASSERT_EQ(service.trackedCount(), 2u);
    ASSERT_EQ(service.verifiedCount(), 2u);

    fixture.fileSystem.remove(mapPath("wide.rawtiles").c_str());
    advancePastRescan(fixture);
    settle(service, fixture, 100);

    EXPECT_EQ(service.trackedCount(), 1u);
    EXPECT_EQ(service.verifiedCount(), 1u);
}

TEST(Service, KeepsVerifyingTheRemainingPacksAfterOneIsDropped)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(mapPath("a.rawtiles"), pack(3000, 'A'));
    fixture.fileSystem.seedFile(mapPath("b.rawtiles"), pack(3000, 'B'));
    fixture.fileSystem.seedFile(mapPath("c.rawtiles"), pack(3000, 'C'));

    Service service(fixture.kernel);
    settle(service, fixture, 300);
    ASSERT_EQ(service.verifiedCount(), 3u);

    // Remove one and add another in the same window: the erase shifts the
    // entries after it down, which is exactly where a stale cursor would
    // silently skip work.
    fixture.fileSystem.remove(mapPath("a.rawtiles").c_str());
    fixture.fileSystem.seedFile(mapPath("d.rawtiles"), pack(4000, 'D'));
    advancePastRescan(fixture);
    settle(service, fixture, 300);

    EXPECT_EQ(service.trackedCount(), 3u);
    EXPECT_EQ(service.verifiedCount(), 3u) << "the newly added pack must still get verified";

    uint64_t size = 0;
    uint32_t crc  = 0;
    EXPECT_EQ(trustOf(fixture, mapPath("d.rawtiles"), size, crc), PackTrustMarker::Trust::Good);
}

// --------------------------------------------------------------------------
// Repeat polling of a settled set must be free: no rescanning of verified
// packs, no re-opening of files, no growth in tracked entries.
// --------------------------------------------------------------------------
TEST(Service, SettledPacksCostNoFurtherIo)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(mapPath("athens.rawtiles"), pack(9000));

    Service service(fixture.kernel);
    settle(service, fixture, 200);
    ASSERT_EQ(service.verifiedCount(), 1u);

    const size_t writesAfterFirstPass = fixture.fileSystem.bytesWritten;

    for (int rescans = 0; rescans < 5; ++rescans) {
        advancePastRescan(fixture);
        settle(service, fixture, 50);
    }

    EXPECT_EQ(service.trackedCount(), 1u);
    EXPECT_EQ(fixture.fileSystem.bytesWritten, writesAfterFirstPass)
        << "an already-verified pack must not rewrite its marker on every rescan";
}

// --------------------------------------------------------------------------
// Idle power. This service is APP_AUTOSTART and never exits, so what it does
// when there is nothing to do is a cost the device pays for its whole life.
// --------------------------------------------------------------------------
TEST(Service, SleepsToTheNextRescanOnceEverythingIsVerified)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(mapPath("athens.rawtiles"), pack(3000));

    Service service(fixture.kernel);

    // While there is work pending, come back promptly.
    service.poll();
    EXPECT_LE(service.nextWaitMs(), 50u);

    settle(service, fixture, 200);
    ASSERT_EQ(service.verifiedCount(), 1u);

    // Nothing left to do: the only reason to wake is the next rescan.
    EXPECT_GT(service.nextWaitMs(), 1000u)
        << "an idle autostart service must not poll at a busy cadence forever";
    EXPECT_LE(service.nextWaitMs(), 30000u);
}

TEST(Service, IdleWaitShrinksAsTheRescanBecomesDue)
{
    KernelFixture fixture;
    Service service(fixture.kernel);
    service.poll();

    const uint32_t justAfterScan = service.nextWaitMs();
    fixture.system.nowMs += 20000;
    const uint32_t laterOn = service.nextWaitMs();

    EXPECT_LT(laterOn, justAfterScan) << "the sleep must track time already elapsed";
    EXPECT_LE(laterOn, 10000u);
}

TEST(Service, LeavesNoOpenFileHandlesBehind)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(mapPath("athens.rawtiles"), pack(9000, 'A'));
    fixture.fileSystem.seedFile(mapPath("bad.rawtiles"), corruptPack(4000, 'B'));

    Service service(fixture.kernel);
    settle(service, fixture, 300);

    for (const auto& kv : fixture.fileSystem.openHandles) {
        EXPECT_EQ(kv.second, 0u) << "leaked handle on " << kv.first
                                 << " (a FatFs lock slot on device)";
    }
}

} // namespace
