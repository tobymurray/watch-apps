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
#include <vector>

#include "Commands.hpp"
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

// --------------------------------------------------------------------------
// The roster the GUI's list is drawn from.
//
// The stock StubAppComm drops whatever it is sent, so these build a Kernel
// around a comm that copies each roster row out first. Kernel takes its
// collaborators by reference, so this needs no change to the shared doubles.
// --------------------------------------------------------------------------

struct SentRow {
    std::string name;
    uint16_t    index;
    uint16_t    total;
    uint8_t     state;
};

/// Flattens the chunked burst back into rows, and counts the messages it took
/// -- the GUI's queue holds ten and drops its oldest, so how many messages a
/// roster costs is part of what these assert.
class CapturingComm : public SDK::TestSupport::StubAppComm {
public:
    std::vector<SentRow> rows;
    int                  messages = 0;
    uint16_t             lastTotal = 0;

    bool sendMessage(SDK::MessageBase* msg, uint32_t timeoutMs = 0) override
    {
        if (msg != nullptr && msg->getType() == CustomMessage::MAP_MANAGER_PACK_STATUS) {
            const auto* chunk = static_cast<CustomMessage::MapManagerPackStatus*>(msg);
            ++messages;
            lastTotal = chunk->total;
            for (uint8_t i = 0; i < chunk->count; ++i) {
                rows.push_back({chunk->rows[i].name,
                                static_cast<uint16_t>(chunk->firstIndex + i), chunk->total,
                                chunk->rows[i].state});
            }
        }
        return SDK::TestSupport::StubAppComm::sendMessage(msg, timeoutMs);
    }

    void reset()
    {
        rows.clear();
        messages  = 0;
        lastTotal = 0;
    }
};

/// A fixture whose comm records the roster rows the service publishes.
struct CapturingFixture {
    KernelFixture base;
    CapturingComm comm;
    SDK::Kernel   kernel{base.system, base.logger, base.memory, comm, base.fileSystem};
};

TEST(ServiceRoster, DescribesEveryTrackedPackInListOrder)
{
    CapturingFixture fixture;
    fixture.base.fileSystem.seedFile(mapPath("athens.rawtiles"), pack(9000, 'A'));
    fixture.base.fileSystem.seedFile(mapPath("wide.rawtiles"), pack(5000, 'B'));

    Service service(fixture.kernel);
    settle(service, fixture.base, 200);

    fixture.comm.reset();
    service.publishRoster();

    ASSERT_EQ(fixture.comm.rows.size(), 2u);
    for (size_t i = 0; i < fixture.comm.rows.size(); ++i) {
        EXPECT_EQ(fixture.comm.rows[i].index, i) << "rows must be numbered 0..total-1";
        EXPECT_EQ(fixture.comm.rows[i].total, 2u)
            << "every row carries the burst length, so the GUI can tell it has them all";
    }

    // Names, not paths: the row is what a narrow list draws.
    EXPECT_EQ(fixture.comm.rows[0].name, "athens.rawtiles");
    EXPECT_EQ(fixture.comm.rows[1].name, "wide.rawtiles");
}

TEST(ServiceRoster, ReportsEachPacksOwnVerdict)
{
    CapturingFixture fixture;
    fixture.base.fileSystem.seedFile(mapPath("good.rawtiles"), pack(4000, 'A'));
    fixture.base.fileSystem.seedFile(mapPath("bad.rawtiles"), corruptPack(4000, 'B'));

    Service service(fixture.kernel);
    settle(service, fixture.base, 300);

    fixture.comm.reset();
    service.publishRoster();

    ASSERT_EQ(fixture.comm.rows.size(), 2u);
    for (const SentRow& row : fixture.comm.rows) {
        if (row.name == "good.rawtiles") {
            EXPECT_EQ(row.state, static_cast<uint8_t>(CustomMessage::PackState::Verified));
        } else if (row.name == "bad.rawtiles") {
            EXPECT_EQ(row.state, static_cast<uint8_t>(CustomMessage::PackState::Mismatched))
                << "a corrupt pack must say so, not sit as still-pending";
        } else {
            ADD_FAILURE() << "unexpected pack in the roster: " << row.name;
        }
    }
}

TEST(ServiceRoster, AnnouncesAnEmptyRosterRatherThanSayingNothing)
{
    CapturingFixture fixture; // no packs seeded at all

    Service service(fixture.kernel);
    settle(service, fixture.base, 100);

    fixture.comm.reset();
    service.publishRoster();

    ASSERT_TRUE(fixture.comm.rows.empty())
        << "an empty roster carries no rows";
    EXPECT_EQ(fixture.comm.lastTotal, 0u);
    EXPECT_EQ(fixture.comm.messages, 1) << "one chunk, carrying no rows";
}

TEST(ServiceRoster, DropsAPackFromTheRosterOnceItsFileIsGone)
{
    CapturingFixture fixture;
    fixture.base.fileSystem.seedFile(mapPath("athens.rawtiles"), pack(4000, 'A'));
    fixture.base.fileSystem.seedFile(mapPath("doomed.rawtiles"), pack(3000, 'B'));

    Service service(fixture.kernel);
    settle(service, fixture.base, 200);

    fixture.base.fileSystem.remove(mapPath("doomed.rawtiles").c_str());
    advancePastRescan(fixture.base);
    settle(service, fixture.base, 200);

    fixture.comm.reset();
    service.publishRoster();

    ASSERT_EQ(fixture.comm.rows.size(), 1u);
    EXPECT_EQ(fixture.comm.rows[0].name, "athens.rawtiles");
    EXPECT_EQ(fixture.comm.rows[0].total, 1u);
}

TEST(ServiceRoster, FitsAFullRosterInFewerMessagesThanTheGuiQueueHolds)
{
    // The GUI's incoming queue holds ten and discards its *oldest* entry on
    // overflow, so a burst longer than that could never be delivered whole --
    // it would arrive as a roster missing its head, every time. This is the
    // regression test for that: it was a row per message once, and a full
    // roster could not survive the trip.
    CapturingFixture fixture;
    for (size_t i = 0; i < CustomMessage::kMaxRosterPacks; ++i) {
        const std::string name = "pack-" + std::to_string(i) + ".rawtiles";
        fixture.base.fileSystem.seedFile(mapPath(name.c_str()),
                                         pack(1000 + i * 10, static_cast<char>('A' + i)));
    }

    Service service(fixture.kernel);
    settle(service, fixture.base, 600);
    ASSERT_EQ(service.trackedCount(), CustomMessage::kMaxRosterPacks);

    fixture.comm.reset();
    service.publishRoster();

    EXPECT_EQ(fixture.comm.rows.size(), CustomMessage::kMaxRosterPacks)
        << "every tracked pack must reach the GUI";
    EXPECT_LE(fixture.comm.messages, 3)
        << "a full roster must fit well inside the GUI's ten-deep queue, with room "
           "for a progress snapshot sharing the same frame";
}

TEST(ServiceRoster, CoalescesRapidVerdictChangesIntoOneBurst)
{
    // Verdicts land in clusters -- a screenful of cached markers all resolve
    // within a few hundred milliseconds at boot. Publishing per transition
    // would stack bursts inside one 10Hz frame and overrun the queue, so they
    // are throttled and coalesced instead.
    CapturingFixture fixture;
    for (int i = 0; i < 6; ++i) {
        const std::string name = "pack-" + std::to_string(i) + ".rawtiles";
        fixture.base.fileSystem.seedFile(mapPath(name.c_str()),
                                         pack(1000, static_cast<char>('A' + i)));
    }

    Service service(fixture.kernel);
    fixture.comm.reset();

    // 300ms of polling, comfortably inside one throttle period, during which
    // every pack is discovered and reaches a verdict.
    for (int i = 0; i < 30; ++i) {
        service.poll();
        fixture.base.system.nowMs += 10;
    }

    EXPECT_LE(fixture.comm.messages, 3)
        << "many verdicts inside one period must coalesce, not burst per transition";
}

TEST(ServiceRoster, StaysSilentWhileNoGuiIsAttached)
{
    CapturingFixture fixture;
    fixture.base.fileSystem.seedFile(mapPath("athens.rawtiles"), pack(9000, 'A'));

    Service service(fixture.kernel);
    settle(service, fixture.base, 300);

    EXPECT_TRUE(fixture.comm.rows.empty())
        << "the roster changes most at boot, when nothing is watching -- publishing then "
           "would be message traffic with no consumer";
}

} // namespace
