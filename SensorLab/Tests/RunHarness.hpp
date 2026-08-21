/**
 ******************************************************************************
 * @file    RunHarness.hpp
 * @brief   Whole runs through the real Service, at a desk, in milliseconds.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * Why this exists
 *
 * SleepLab's `NightHarness.hpp` found two shipped bugs on the day it was
 * written, in code that had passed every unit test: a glance that was never
 * sent anything at all, and a home widget that was never claimed (ledger row
 * T2). Both were in the stretch between a message arriving and a file being
 * written -- the stretch no unit test reaches and no simulator run exercises.
 *
 * SensorLab's service loop is more complex than SleepLab's. It has three phases,
 * a thirty-seven-type handshake, a promotion pass that applies minimum-n and
 * source rules to about two thousand claims, an indexed GUI burst, and a resume
 * path that has to distinguish an app restart from a device reboot. Every one of
 * those is reachable only by wearing the watch, or by this.
 *
 * ---------------------------------------------------------------------------
 * How, and what was rejected
 *
 * `Service::run()` blocks on `mKernel.comm.getMessage()` and returns only on
 * `COMMAND_APP_STOP`. Three ways in were available:
 *
 *   1. **Extract a `poll()` seam.** Rejected for the same reason SleepLab
 *      rejected it: the loop is one of the things under suspicion. Its interval
 *      grid, its deadline arithmetic and its oversleep catch-up are exactly
 *      where a run gets quietly compressed, and a test that replaces the loop
 *      cannot find a bug in the loop.
 *
 *   2. **A different Service for tests.** Then the tests test the wrong class.
 *
 *   3. **Script the message queue.** `SDK::TestSupport::StubAppComm` is virtual
 *      and its `getMessage()` returns nothing. Overriding it lets the test *be*
 *      the kernel: it answers the sensor layer's resolve/list/describe/connect
 *      handshake, answers `RequestSystemInfo`, delivers batches on a schedule,
 *      advances the uptime clock by exactly the timeout the service asked to
 *      sleep for, and finally hands back an `APP_STOP`.
 *
 * **Option 3**, and the whole of `Service` is then under test unmodified.
 * Nothing in the app knows this file exists. The one thing the app does
 * differently from SleepLab is take its kernel by reference rather than through
 * `SDK::KernelProviderService::GetInstance()` -- which is also why it talks to
 * the sensor layer through `Probes::SensorBus` instead of
 * `SDK::Sensor::Connection`.
 *
 * ---------------------------------------------------------------------------
 * What the harness is *not* evidence about
 *
 * **Nothing about a sensor.** The streams are generated, so a scenario proves
 * that the code computes what it claims to compute from a stream of a known
 * shape, and that is all it proves. Every sensor claim in `Docs/LEDGER.md` comes
 * from hardware or does not exist. What this replaces is the desk-answerable
 * half -- the statistics, the promotion rules, the file formats, the burst
 * contract, the resume path -- which is most of the code and none of the
 * findings.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_TEST_RUNHARNESS_HPP
#define SENSORLAB_TEST_RUNHARNESS_HPP

#include <cstdint>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>

#include "KernelTestDoubles.hpp"

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/SensorLayer/SensorData.hpp"
#include "SDK/SensorLayer/SensorTypes.hpp"

#include "Catalogue/Catalogue.hpp"
#include "Commands.hpp"
#include "Service.hpp"

namespace Harness
{

/// How one modelled sensor behaves. A scenario is a list of these, and the list
/// reads as a description of a device rather than as a stream of samples.
struct Channel
{
    /// The sensor type value, e.g. 0x10.
    uint32_t type = 0;

    /// Handle the kernel hands out. Deliberately settable, so a scenario can put
    /// a handle **above 255** on the wire -- which is the case
    /// `SDK::Sensor::Connection` truncates silently and which this app claims to
    /// survive. A claim like that is worth exactly one test.
    uint32_t handle = 0;

    /// `RequestDefault` resolves. False models `SPO2` and `HEART_BEAT`, which do
    /// not (ledger rows S4 and S5): `connect()` is called and there is nothing
    /// to subscribe to.
    bool resolves = true;
    /// `RequestConnect` succeeds. False models a type that resolves a handle and
    /// then refuses a connection, which is a third distinct finding.
    bool connects = true;
    /// `RequestList` is answered, and with how many handles. Nobody has ever
    /// seen this answer on hardware.
    bool     listAnswered = true;
    uint32_t driverCount  = 1;
    /// `RequestGetDesc`'s answer, or empty for no answer.
    std::string descriptor;

    /// Delivered sample period in ms. Independent of what the app *asks* for:
    /// the accelerometer delivered ~48 Hz against a requested 25 (row S3), so a
    /// harness that fed back the requested rate would miss every consequence of
    /// the delivered one. Zero means an event sensor.
    uint32_t deliveredPeriodMs = 0;
    /// Delivered batch interval in ms. Also independent: 195 ms measured against
    /// a requested 5000 (row S17).
    uint32_t deliveredBatchMs = 0;

    /// Fields per sample, as the *stride* implies. Set this different from the
    /// parser's `Field::COUNT` to model a frame that does not match the parser
    /// shipped to read it, which is the input the parsers were not written for.
    uint16_t fields = 3;

    /// Total events an event sensor emits over the whole run, spread evenly.
    /// One models `TOUCH_DETECT`, which produced a single sample in 507 minutes.
    uint32_t eventCount = 0;

    /// Value written into every field: `base + index * step`, then held.
    float valueBase = 0.0f;
    float valueStep = 0.0f;
    /// Quantise each value to this step, so a test can check that the recovered
    /// LSB is the one that was fed in. Zero for no quantisation.
    float quantum = 0.0f;
    /// Never change the value. Models `BATTERY_LEVEL` reading 100.0 % at both
    /// ends of a night the fuel gauge lost 10 mAh over (row S18).
    bool stuck = false;
    /// Emit a NaN in field 0 of this many samples, spread evenly. One of these
    /// once poisoned every subsequent SleepLab epoch to exactly zero.
    uint32_t nanCount = 0;

    /// Write `mTimeStampUs` above 999, breaking the invariant
    /// `DataView::getTimestampUs()` is built on. Nothing in either repository
    /// has ever checked it.
    bool usOver999 = false;
    /// Step the sample timestamp backwards once, this many ms, at the halfway
    /// point. A pipeline that reorders is a finding.
    uint32_t backwardsJumpMs = 0;
    /// Add this many ms of offset per second of uptime between the sensor's
    /// clock and the device's -- a measurable skew, so a test can assert the
    /// reported ppm.
    float skewMsPerSec = 0.0f;
};

/// A whole run: what the device looks like, and what happens to it.
struct Scenario
{
    /// Uptime at launch. Not zero by default: a service that only ever starts at
    /// uptime 0 never meets the arithmetic that matters, and the ~49.7-day wrap
    /// is reachable by setting this near 2^32.
    uint32_t startUptimeMs = 3600u * 1000u;

    /// How long the run lasts, in service-loop terms. `APP_STOP` is delivered
    /// at the end.
    uint32_t durationMs = 5u * 60u * 1000u;

    /// The kernel's answer to `RequestSystemInfo`. Empty firmware models a
    /// kernel that does not implement it, which is what makes a profile
    /// undiffable and which the app has to say out loud.
    std::string firmware = "1.4.0";
    std::string hardware = "HW_v2.1";
    bool        systemInfoAnswered = true;

    std::vector<Channel> channels;

    /// `settings.json` to seed, or empty for none.
    std::string settingsJson;

    /// Files to keep from a previous run -- how a resume is tested: run the
    /// first half for real, stop it the way the cable does, then run the second
    /// half against what the first one actually wrote, rather than against a
    /// hand-written state file that might not be one the app can produce.
    bool keepFilesystem = false;

    /// Extra files to place on the volume before the run, path -> contents.
    ///
    /// One case needs this and it is worth naming: a run that was left **open**.
    /// `Service::run()` returns only on `COMMAND_APP_STOP`, and on that path the
    /// service closes its run properly -- so a harness that always delivers
    /// `APP_STOP` can never produce a state file with `run_open` set. A process
    /// killed without one does happen, though: a crash, or the battery going
    /// flat mid-soak. The honest way to model it is to take the state file a
    /// real run *did* write and flip that one flag, which is what
    /// `Pipeline_test.cpp` does -- rather than hand-authoring a state file that
    /// might not be one the app can produce.
    std::map<std::string, std::string> seedFiles;

    /// Deliver a `SENSORLAB_COMMAND` at this many ms into the run, or -1.
    int32_t                commandAtMs = -1;
    CustomMessage::Command command     = CustomMessage::Command::None;

    /// Deliver a `SENSORLAB_REQUEST` -- somebody opening the app -- at this many
    /// ms in, or -1. The service publishes nothing at all with no GUI attached,
    /// deliberately, so a scenario that wants to see the roster has to open it.
    int32_t guiOpensAtMs = 0;

    /// Refuse every write once this many bytes have been written: a volume that
    /// fills mid-run.
    size_t failWritesAfterBytes = static_cast<size_t>(-1);

    const Channel *channelForType(uint32_t type) const
    {
        for (const Channel &c : channels) {
            if (c.type == type) { return &c; }
        }
        return nullptr;
    }
};

/// What a run left behind on the GUI side.
struct Observations
{
    /// The payload, not the message: `MessageBase` deletes copy-construction
    /// (ledger row P12), which is why `SensorLabStatusData` exists.
    std::vector<CustomMessage::SensorLabStatusData> statuses;
    /// Every roster row, keyed by type index, last write wins -- which is what
    /// the GUI itself would do.
    std::map<uint8_t, CustomMessage::RosterRow> roster;
    /// The bursts as they arrived, so a test can check the indices rather than
    /// only the contents.
    std::vector<std::pair<uint8_t, uint8_t>> bursts;   ///< (first, count)
    uint8_t rosterTotal = 0;

    bool haveStatus() const { return !statuses.empty(); }
    const CustomMessage::SensorLabStatusData &lastStatus() const
    {
        return statuses.back();
    }
};

// ---------------------------------------------------------------------------
// The scripted kernel
// ---------------------------------------------------------------------------

class ScriptedComm : public SDK::TestSupport::StubAppComm
{
public:
    void begin(const Scenario &s, SDK::TestSupport::StubSystem &sys,
               Observations &obs)
    {
        mScn = &s;
        mSys = &sys;
        mObs = &obs;

        mT0        = s.startUptimeMs;
        mStopAtMs  = mT0 + s.durationMs;
        mStopped   = false;
        mConnected.clear();
        mNextBatchAt.clear();
        mEmitted.clear();
        mSampleCursor.clear();

        mCommandSent = (s.commandAtMs < 0);
        mCommandAtMs = (s.commandAtMs >= 0)
                           ? mT0 + static_cast<uint32_t>(s.commandAtMs) : 0;
        mGuiSent     = (s.guiOpensAtMs < 0);
        mGuiAtMs     = (s.guiOpensAtMs >= 0)
                           ? mT0 + static_cast<uint32_t>(s.guiOpensAtMs) : 0;
    }

    // -- The kernel's half of every request ---------------------------------

    bool sendMessage(SDK::MessageBase *msg, uint32_t timeoutMs) override
    {
        (void)timeoutMs;
        if (msg == nullptr) { return false; }

        switch (msg->getType()) {
            case SDK::MessageType::REQUEST_SYSTEM_INFO: {
                auto *r = static_cast<SDK::Message::RequestSystemInfo *>(msg);
                if (!mScn->systemInfoAnswered) {
                    setFail(msg);
                    return true;
                }
                std::snprintf(r->firmwareVersion, sizeof(r->firmwareVersion),
                              "%s", mScn->firmware.c_str());
                std::snprintf(r->hardwareVersion, sizeof(r->hardwareVersion),
                              "%s", mScn->hardware.c_str());
                r->uptimeSeconds = mSys->nowMs / 1000u;
                setOk(msg);
                return true;
            }

            case SDK::MessageType::REQUEST_SENSOR_LAYER_GET_DEFAULT: {
                auto *r = static_cast<SDK::Message::Sensor::RequestDefault *>(msg);
                const Channel *c =
                    mScn->channelForType(static_cast<uint32_t>(r->id));
                if (c == nullptr || !c->resolves) {
                    r->handle = 0;
                    setFail(msg);
                    return true;
                }
                r->handle = c->handle;
                setOk(msg);
                return true;
            }

            case SDK::MessageType::REQUEST_SENSOR_LAYER_GET_LIST: {
                auto *r = static_cast<SDK::Message::Sensor::RequestList *>(msg);
                const Channel *c =
                    mScn->channelForType(static_cast<uint32_t>(r->id));
                if (c == nullptr || !c->listAnswered) {
                    setFail(msg);
                    return true;
                }
                r->handlesCount = c->driverCount;
                for (uint32_t i = 0; i < c->driverCount && i < 10; i++) {
                    r->handles[i] = c->handle + i;
                }
                setOk(msg);
                return true;
            }

            case SDK::MessageType::REQUEST_SENSOR_LAYER_GET_DESCRIPTOR: {
                auto *r = static_cast<SDK::Message::Sensor::RequestGetDesc *>(msg);
                const Channel *c = channelForHandle(r->handle);
                if (c == nullptr || c->descriptor.empty()) {
                    setFail(msg);
                    return true;
                }
                // Deliberately NOT terminated when it fills the field: `desc` is
                // `char[32]` with no guarantee of one, and an app that assumed a
                // terminator would read past the pool block. This is the case
                // that proves SensorBus copies bounded.
                std::memset(r->desc, 0, sizeof(r->desc));
                std::memcpy(r->desc, c->descriptor.data(),
                            c->descriptor.size() < sizeof(r->desc)
                                ? c->descriptor.size() : sizeof(r->desc));
                setOk(msg);
                return true;
            }

            case SDK::MessageType::REQUEST_SENSOR_LAYER_CONNECT: {
                auto *r = static_cast<SDK::Message::Sensor::RequestConnect *>(msg);
                const Channel *c = channelForHandle(r->handle);
                if (c == nullptr || !c->connects) {
                    setFail(msg);
                    return true;
                }
                mConnected[r->handle] = true;
                mNextBatchAt[r->handle] =
                    mSys->nowMs + (c->deliveredBatchMs > 0 ? c->deliveredBatchMs
                                                           : 1000u);
                setOk(msg);
                return true;
            }

            case SDK::MessageType::REQUEST_SENSOR_LAYER_DISCONNECT: {
                auto *r = static_cast<SDK::Message::Sensor::RequestDisconnect *>(msg);
                mConnected[r->handle] = false;
                setOk(msg);
                return true;
            }

            case CustomMessage::SENSORLAB_STATUS: {
                auto *r = static_cast<CustomMessage::SensorLabStatus *>(msg);
                mObs->statuses.push_back(r->data);
                setOk(msg);
                return true;
            }

            case CustomMessage::SENSORLAB_ROSTER: {
                auto *r = static_cast<CustomMessage::SensorRoster *>(msg);
                mObs->bursts.emplace_back(r->first, r->count);
                mObs->rosterTotal = r->total;
                for (uint8_t i = 0; i < r->count
                                    && i < CustomMessage::kRosterRowsPerMsg; i++) {
                    mObs->roster[r->rows[i].typeIdx] = r->rows[i];
                }
                setOk(msg);
                return true;
            }

            default:
                setOk(msg);
                return true;
        }
    }

    // -- The loop's wait -----------------------------------------------------

    /**
     * @brief Sleep for at most @p timeoutMs, delivering whatever comes first.
     *
     * Advancing the clock by exactly the requested timeout is what makes the
     * interval grid and the oversleep catch-up testable; a harness that advanced
     * by a fixed tick would be testing a different loop.
     */
    bool getMessage(SDK::MessageBase *&msg, uint32_t timeoutMs) override
    {
        msg = nullptr;
        const uint32_t now      = mSys->nowMs;
        const uint32_t deadline = now + timeoutMs;

        if (!mGuiSent && static_cast<int32_t>(deadline - mGuiAtMs) >= 0) {
            mGuiSent = true;
            advanceTo(mGuiAtMs);
            msg = control(CustomMessage::SENSORLAB_REQUEST);
            return msg != nullptr;
        }

        if (!mCommandSent && static_cast<int32_t>(deadline - mCommandAtMs) >= 0) {
            mCommandSent = true;
            advanceTo(mCommandAtMs);
            void *raw = ::operator new(sizeof(CustomMessage::SensorLabCommand),
                                       std::nothrow);
            if (raw == nullptr) { return false; }
            auto *c = new (raw) CustomMessage::SensorLabCommand();
            c->command = static_cast<uint8_t>(mScn->command);
            msg = c;
            return true;
        }

        // APP_STOP takes priority over anything scheduled after it.
        if (static_cast<int32_t>(deadline - mStopAtMs) >= 0 && !mStopped) {
            mStopped = true;
            advanceTo(mStopAtMs);
            msg = control(SDK::MessageType::COMMAND_APP_STOP);
            return msg != nullptr;
        }

        // The earliest batch due at or before the deadline.
        uint32_t bestAt     = 0;
        uint32_t bestHandle = 0;
        for (const auto &kv : mNextBatchAt) {
            const uint32_t handle = kv.first;
            auto           it     = mConnected.find(handle);
            if (it == mConnected.end() || !it->second) { continue; }
            const Channel *c = channelForHandle(handle);
            if (c == nullptr) { continue; }
            if (static_cast<int32_t>(kv.second - deadline) > 0) { continue; }
            if (bestHandle == 0
                || static_cast<int32_t>(kv.second - bestAt) < 0) {
                bestAt     = kv.second;
                bestHandle = handle;
            }
        }

        if (bestHandle == 0) {
            advanceTo(deadline);
            return false;
        }

        advanceTo(bestAt);
        msg = buildBatch(bestHandle, bestAt);
        return msg != nullptr;
    }

private:
    static void setOk(SDK::MessageBase *m)
    {
        m->setResult(SDK::MessageResult::SUCCESS);
    }
    static void setFail(SDK::MessageBase *m)
    {
        m->setResult(SDK::MessageResult::FAIL);
    }

    SDK::MessageBase *control(SDK::MessageType::Type t)
    {
        void *raw = ::operator new(sizeof(SDK::MessageID), std::nothrow);
        if (raw == nullptr) { return nullptr; }
        auto *m = new (raw) SDK::MessageID();
        m->setType(t);
        return m;
    }

    void advanceTo(uint32_t t) { mSys->nowMs = t; }

    const Channel *channelForHandle(uint32_t handle) const
    {
        for (const Channel &c : mScn->channels) {
            if (c.handle == handle) { return &c; }
        }
        return nullptr;
    }

    SDK::Message::Sensor::EventData *newBatch(uint32_t handle, uint16_t count,
                                              uint16_t fields)
    {
        const size_t stride = sizeof(SDK::Sensor::Data)
                              + (fields - 1) * sizeof(SDK::Sensor::Data::Field);
        const size_t head = sizeof(SDK::Message::Sensor::EventData)
                            - sizeof(SDK::Sensor::Data);
        void *raw = ::operator new(head + stride * count, std::nothrow);
        if (raw == nullptr) { return nullptr; }
        std::memset(raw, 0, head + stride * count);
        auto *e = new (raw) SDK::Message::Sensor::EventData();
        e->handle = handle;
        e->count  = count;
        e->stride = static_cast<uint32_t>(stride);
        return e;
    }

    static SDK::Sensor::Data *sampleAt(SDK::Message::Sensor::EventData *e,
                                       uint16_t i)
    {
        auto *base = reinterpret_cast<uint8_t *>(e->data);
        return reinterpret_cast<SDK::Sensor::Data *>(base + i * e->stride);
    }

    SDK::MessageBase *buildBatch(uint32_t handle, uint32_t at)
    {
        const Channel *c = channelForHandle(handle);
        if (c == nullptr) { return nullptr; }

        const uint32_t batchMs = (c->deliveredBatchMs > 0)
                                     ? c->deliveredBatchMs : 1000u;
        mNextBatchAt[handle] = at + batchMs;

        uint16_t n = 1;
        if (c->deliveredPeriodMs > 0) {
            n = static_cast<uint16_t>(batchMs / c->deliveredPeriodMs);
            if (n == 0) { n = 1; }
        } else {
            // An event sensor: `eventCount` events over the whole run, spread
            // evenly. Zero means it resolved and then said nothing at all, which
            // is a distinct finding and one the app has to notice from the
            // absence.
            if (c->eventCount == 0) { return nullptr; }
            const uint32_t total   = mScn->durationMs / batchMs + 1u;
            const uint32_t emitted = mEmitted[handle];
            const uint32_t due     = (static_cast<uint64_t>(c->eventCount)
                                      * ((at - mT0) / batchMs + 1u)) / total;
            if (due <= emitted) { return nullptr; }
            mEmitted[handle] = emitted + 1u;
            n = 1;
        }

        auto *e = newBatch(handle, n, c->fields);
        if (e == nullptr) { return nullptr; }

        const uint32_t period = (c->deliveredPeriodMs > 0)
                                    ? c->deliveredPeriodMs : batchMs;
        const uint32_t first  = at - period * n;

        for (uint16_t i = 0; i < n; i++) {
            SDK::Sensor::Data *d = sampleAt(e, i);
            uint32_t ts = first + i * period;

            // A measurable skew between the sensor's clock and the device's, so
            // a test can assert the reported ppm rather than only its sign.
            if (c->skewMsPerSec != 0.0f) {
                const float secs = static_cast<float>(ts - mT0) / 1000.0f;
                ts -= static_cast<uint32_t>(secs * c->skewMsPerSec);
            }
            // One step backwards at the halfway point. Counted, never
            // corrected: sorting it would hide it.
            if (c->backwardsJumpMs > 0 && (at - mT0) > mScn->durationMs / 2) {
                ts -= c->backwardsJumpMs;
            }

            d->mTimeStamp   = ts;
            d->mTimeStampUs = c->usOver999 ? 1500u : 0u;

            const uint32_t idx = mSampleCursor[handle]++;
            float value = c->stuck
                              ? c->valueBase
                              : c->valueBase
                                    + static_cast<float>(idx) * c->valueStep;
            if (c->quantum > 0.0f) {
                // Quantise, so a test can check that the recovered LSB is the
                // step that was fed in. `round(v / q) * q` rather than a
                // truncation, because the truncation's error is one-sided and
                // would bias the recovered step.
                const float steps = value / c->quantum;
                value = static_cast<float>(
                            static_cast<long long>(steps + (steps < 0 ? -0.5f : 0.5f)))
                        * c->quantum;
            }

            for (uint16_t f = 0; f < c->fields; f++) {
                d->mValue[f].f = value;
            }

            // A NaN, spread evenly. One of these once poisoned every subsequent
            // SleepLab epoch to exactly zero.
            if (c->nanCount > 0) {
                const uint32_t every = (mScn->durationMs / period) / c->nanCount;
                if (every > 0 && (idx % every) == 0 && idx > 0) {
                    d->mValue[0].u32 = 0x7FC00000u;   // quiet NaN
                }
            }
        }
        return e;
    }

    const Scenario                     *mScn = nullptr;
    SDK::TestSupport::StubSystem       *mSys = nullptr;
    Observations                       *mObs = nullptr;

    uint32_t mT0       = 0;
    uint32_t mStopAtMs = 0;
    bool     mStopped  = false;

    bool     mGuiSent     = false;
    uint32_t mGuiAtMs     = 0;
    bool     mCommandSent = false;
    uint32_t mCommandAtMs = 0;

    std::map<uint32_t, bool>     mConnected;
    std::map<uint32_t, uint32_t> mNextBatchAt;
    std::map<uint32_t, uint32_t> mEmitted;
    std::map<uint32_t, uint32_t> mSampleCursor;
};

/**
 * @brief One run, start to finish, through the real Service.
 *
 * Reused across scenarios in one test binary, because `Service` is 136 KB and
 * constructing one per assertion would be slow enough to discourage writing
 * assertions.
 *
 * Named `Runner` rather than `Run`: GoogleTest's own `testing::Test::Run()` is
 * in scope inside a TEST body, and a type called `Run` there resolves to the
 * private member function instead. Found by the compiler, which is the right
 * place to find it.
 */
class Runner
{
public:
    Observations observations;

    /// Drive @p scn to completion. Returns when `Service::run()` does.
    void execute(const Scenario &scn)
    {
        if (!scn.keepFilesystem) {
            mFs.files.clear();
            mFs.flushCounts.clear();
            mFs.openHandles.clear();
            mFs.bytesWritten = 0;
        }
        mFs.failWritesAfterBytes = scn.failWritesAfterBytes;

        if (!scn.settingsJson.empty()) {
            mFs.seedFile(SensorLab::kSettingsPath, scn.settingsJson);
        }
        for (const auto &kv : scn.seedFiles) {
            mFs.seedFile(kv.first, kv.second);
        }

        mSys.nowMs = scn.startUptimeMs;
        observations = Observations {};
        mComm.begin(scn, mSys, observations);

        SDK::Kernel kernel(mSys, mLogger, mMemory, mComm, mFs);
        Service service(kernel);
        service.run();
    }

    /// The volume as the run left it, for asserting on file contents.
    SDK::TestSupport::InMemoryFileSystem &fs() { return mFs; }
    SDK::TestSupport::StubSystem         &sys() { return mSys; }

    /// Contents of a file, or "" if it does not exist.
    std::string file(const std::string &path) const
    {
        auto it = mFs.files.find(path);
        return (it == mFs.files.end()) ? std::string() : it->second.content;
    }

private:
    SDK::TestSupport::StubSystem         mSys;
    SDK::TestSupport::StubLogger         mLogger;
    SDK::TestSupport::StubAppMemory      mMemory;
    ScriptedComm                         mComm;
    SDK::TestSupport::InMemoryFileSystem mFs;
};

// ---------------------------------------------------------------------------
// Scenario helpers
// ---------------------------------------------------------------------------

/// A channel modelling the accelerometer as hardware actually delivers it:
/// ~48 Hz against a requested 25, in 195 ms batches against a requested 5000
/// (ledger rows S3 and S17). The defaults matter -- a harness that fed the
/// *requested* rate would miss every consequence of the delivered one.
inline Channel accelerometerAsMeasured(uint32_t handle = 1)
{
    Channel c;
    c.type              = 0x10;
    c.handle            = handle;
    c.descriptor        = "bmi270-accel";
    c.deliveredPeriodMs = 21;     // ~48 Hz
    c.deliveredBatchMs  = 195;
    c.fields            = 3;
    c.valueBase         = 0.0f;
    c.valueStep         = 0.001f;
    return c;
}

/// `TOUCH_DETECT` as measured: resolves, and produces one sample in 507 minutes
/// (row S7). An event sensor that was assumed to be streaming, which would have
/// suppressed every night SleepLab ever recorded (row S12).
inline Channel touchAsMeasured(uint32_t handle = 2)
{
    Channel c;
    c.type             = 0x140;
    c.handle           = handle;
    c.descriptor       = "touch";
    c.deliveredBatchMs = 60000;
    c.fields           = 1;
    c.eventCount       = 1;
    c.valueBase        = 1.0f;
    c.stuck            = true;
    return c;
}

/// A type with no producer: `RequestDefault` resolves nothing. `SPO2` (row S4)
/// and `HEART_BEAT` (row S5).
inline Channel noProducer(uint32_t type)
{
    Channel c;
    c.type     = type;
    c.handle   = 0;
    c.resolves = false;
    return c;
}

} // namespace Harness

#endif // SENSORLAB_TEST_RUNHARNESS_HPP
