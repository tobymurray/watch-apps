/**
 ******************************************************************************
 * @file    Service.cpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   SensorLab's service. Rationale is in Service.hpp.
 ******************************************************************************
 */

#include "Service.hpp"

#include <ctime>

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryCharging.hpp"

#define LOG_MODULE_PRX      "SensorLab"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

// `Service` is in the global namespace; see the note in Service.hpp. Everything
// it collaborates with is under `SensorLab::`, so the whole file works inside
// these using-directives rather than qualifying two hundred call sites.
using namespace SensorLab;

namespace
{

using Catalogue::Layer;
using Catalogue::Metric;
using Catalogue::FieldMetric;

/// Idle wait when there is no scheduled work at all.
///
/// Five seconds. Not `0xFFFFFFFF`: the idle phase still wants to notice the
/// cable and still wants to answer a GUI that has just opened, and a service
/// blocked for ever is one that has to be woken by a message it might not get.
/// Five seconds of idle wake costs nothing measurable and it is the same
/// deadline-bounded-sleep idiom `Timer` uses.
constexpr uint32_t kIdleWaitMs = 5000;

/// Minimum gap between publishes to the GUI, milliseconds.
///
/// `SDK::TouchGFXCommandProcessor` holds app-specific messages in a ten-deep
/// queue and **discards the oldest** when it is full. One publish is four
/// messages -- a status plus three roster bursts -- so two publishes inside one
/// frame overflow it, and a simulator run produced exactly that when the GUI's
/// `onStart` and `onResume` both asked for an update.
///
/// 250 ms: four publishes a second is far more than a screen a person is reading
/// needs, and it is well inside one frame at any frame rate the panel runs at,
/// so a request that arrives during the gap is answered by the *next* publish
/// rather than dropped. A request is never simply ignored -- `mPublishPending`
/// carries it forward.
constexpr uint32_t kPublishMinGapMs = 250;

/// Signed difference of two uptimes, correct across the ~49.7-day wrap.
int32_t diff(uint32_t a, uint32_t b)
{
    return static_cast<int32_t>(b - a);
}

/// `time(nullptr)`, or -1 when the clock is unreadable. Read for labelling only:
/// no duration anywhere in this app is derived from two wall-clock readings.
int64_t wallNow()
{
    const std::time_t t = std::time(nullptr);
    return (t > 0) ? static_cast<int64_t>(t) : -1;
}

/// A measurement a probe is asserting, with n and a value.
Evidence::Measurement measured(float value, uint32_t n,
                               Evidence::Verdict v = Evidence::Verdict::Confirmed)
{
    Evidence::Measurement m {};
    m.hasValue = true;
    m.value    = value;
    m.n        = n;
    m.verdict  = v;
    return m;
}

/// A boolean claim. Recorded as 1.0 or 0.0 with n = 1: **a negative result is a
/// result**, so "SPO2 resolves no driver" is a CONFIRMED row with value 0 rather
/// than an absent one. Ledger row S4 is exactly this and it closed a design
/// question permanently.
Evidence::Measurement measuredBool(bool value, uint32_t n = 1)
{
    return measured(value ? 1.0f : 0.0f, n);
}

} // namespace

// ---------------------------------------------------------------------------

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mBus(kernel)
    , mLog(kernel)
    , mRaw(kernel)
    , mProfile(kernel)
{
    // Flat field-statistics array, carved up by the catalogue's own prefix sums.
    // Done once here rather than looked up per sample.
    for (size_t t = 0; t < Catalogue::kTypeCount; t++) {
        mLive[t].fieldBase  = static_cast<uint16_t>(Catalogue::fieldBase(t));
        mLive[t].fieldSlots = Catalogue::fieldSlots(t);
    }
}

Service::~Service()
{
    unsubscribeAll();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Service::start()
{
    mStartedAt = mKernel.sys.getTimeMs();

    mSettingsStatus = loadSettings(mKernel, mSettings);
    LOG_INFO("%s: period=%ums latency=%ums interval=%us all=%d fields=%d\n",
             toString(mSettingsStatus),
             static_cast<unsigned>(mSettings.periodMs),
             static_cast<unsigned>(mSettings.latencyMs),
             static_cast<unsigned>(mSettings.intervalSec),
             mSettings.subscribeAll, mSettings.fieldStats);

    // The profile's primary key, read from the kernel rather than typed by a
    // human. This is what makes SleepLab's ledger row P1 answerable at all.
    Profile::stampBuild(mManifest, SENSORLAB_VERSION);
    if (!Profile::readSystemInfo(mKernel, mManifest)) {
        // Fall back to what settings.json declared, marked as declared. A
        // profile with no version cannot be diffed at all, so a declared one is
        // better than none -- but the manifest says which it has, and the report
        // repeats it, so nobody mistakes a memory for a measurement.
        const char *declared = mSettings.declaredFirmware;
        size_t      i        = 0;
        for (; i + 1 < sizeof(mManifest.firmware) && declared[i] != '\0'; i++) {
            mManifest.firmware[i] = declared[i];
        }
        mManifest.firmware[i] = '\0';
        mManifest.haveSystemInfo = false;
    }

    recoverPreviousRun();

    // The existence sweep is the app's own opening move: it takes seconds, it
    // costs nothing lasting, and without it the roster has nothing to show. Run
    // unconditionally, because a screen that opened blank and needed a button
    // press to say anything would be a worse instrument.
    runExistenceSweep();

    mPhase = CustomMessage::Phase::Idle;
}

void Service::recoverPreviousRun()
{
    if (!Profile::readState(mKernel, mState)) {
        // No state, or unreadable. Either way there is nothing to resume, and
        // the run counter starts at 1.
        mState = Profile::RunState {};
        return;
    }

    if (!mState.runOpen || mState.runId == 0) {
        return;
    }

    // Exactly two outcomes, and the log says which. Never silently completed.
    const uint32_t now = mKernel.sys.getTimeMs();
    Profile::RunManifest stale {};
    stale.runId = mState.runId;
    Profile::stampBuild(stale, SENSORLAB_VERSION);
    // The primary key as this launch reads it, which is the honest thing to put
    // on a manifest being closed after the fact: the firmware the previous run
    // ran under is not recoverable from here, and claiming it would be worse
    // than recording the one that closed it.
    for (size_t i = 0; i < sizeof(stale.firmware); i++) {
        stale.firmware[i] = mManifest.firmware[i];
    }
    stale.haveSystemInfo    = mManifest.haveSystemInfo;
    stale.started.uptimeMs  = mState.lastUptimeMs;
    stale.started.wallUtc   = mState.lastWallUtc;
    stale.ended.uptimeMs    = now;
    stale.ended.wallUtc     = wallNow();
    stale.rowsWritten       = mState.rowsWritten;

    if (diff(mState.lastUptimeMs, now) < 0) {
        // Uptime went backwards: the device rebooted. A different finding from
        // an app restart, because every since-boot counter reset too and no
        // distribution can span it.
        stale.end = Profile::RunEnd::TruncatedByReboot;
        LOG_INFO("run %lu was open and uptime went backwards: closing it as "
                 "truncated by a reboot\n",
                 static_cast<unsigned long>(mState.runId));
    } else {
        stale.end = Profile::RunEnd::TruncatedByUsb;
        LOG_INFO("run %lu was open and uptime climbed: closing it as truncated, "
                 "almost certainly the USB cable\n",
                 static_cast<unsigned long>(mState.runId));
    }

    Profile::writeRunManifest(mKernel, stale);

    mState.runOpen = false;
    mState.runId   = 0;
    Profile::writeState(mKernel, mState);
}

bool Service::openRun(CustomMessage::Phase phase)
{
    const uint32_t now = mKernel.sys.getTimeMs();

    mManifest.runId              = mState.nextRunId;
    mManifest.started.uptimeMs   = now;
    mManifest.started.wallUtc    = wallNow();
    mManifest.ended               = Profile::Clocks {};
    mManifest.end                 = Profile::RunEnd::InProgress;
    mManifest.requestedPeriodMs   = static_cast<float>(mSettings.periodMs);
    mManifest.requestedLatencyMs  = mSettings.latencyMs;
    mManifest.typesAsked          = 0;
    mManifest.typesResolved       = 0;
    mManifest.typesDelivered      = 0;
    mManifest.guiAttached         = mGuiStarted;
    mManifest.sawCharging         = (mCharging == 1) || (mUsb == 1);
    mManifest.rowsWritten         = 0;
    mManifest.rowFailures         = 0;
    mManifest.bytesWritten        = 0;

    // No claim without a run: the store refuses every `record()` until this.
    mClaims.setRunId(static_cast<uint16_t>(mManifest.runId));

    if (!mLog.begin(mManifest)) {
        LOG_WARNING("run %lu could not open its log\n",
                    static_cast<unsigned long>(mManifest.runId));
    }

    // Raw capture, for a soak only. The existence sweep produces no samples --
    // it connects and immediately lets go -- so a chunk file for it would be a
    // 32-byte header and nothing else.
    const bool wantRaw = mSettings.rawCapture
                         && (phase == CustomMessage::Phase::Soak);
    mManifest.rawCapture = wantRaw;
    mRaw.begin(mManifest.runId,
               wantRaw ? static_cast<uint64_t>(mSettings.rawMaxMb) * 1024ull * 1024ull
                       : 0ull,
               mSettings.rawChunkKb * 1024u,
               now, mManifest.started.wallUtc);

    Profile::writeRunManifest(mKernel, mManifest);

    mState.runId        = mManifest.runId;
    mState.nextRunId    = mManifest.runId + 1;
    mState.runOpen      = true;
    mState.lastUptimeMs = now;
    mState.lastWallUtc  = mManifest.started.wallUtc;
    mState.rowsWritten  = 0;
    mState.phase        = static_cast<uint8_t>(phase);
    Profile::writeState(mKernel, mState);

    mPhase = phase;
    return true;
}

void Service::closeRun(Profile::RunEnd end)
{
    if (mState.runId == 0) {
        return;
    }

    // The raw log first: it has a buffer to push, and the manifest has to carry
    // its final counts. `rawDropped` is the field that matters -- non-zero means
    // the raw log does not hold everything the run saw, and a file that still
    // parses cleanly would give no sign of it.
    mRaw.end();
    mManifest.rawChunks     = mRaw.chunks();
    mManifest.rawBytes      = mRaw.bytes();
    mManifest.rawBatches    = mRaw.batches();
    mManifest.rawSamples    = mRaw.samples();
    mManifest.rawDropped    = mRaw.dropped();
    mManifest.rawFailures   = mRaw.failures();
    mManifest.rawCapReached = mRaw.capReached();

    mManifest.ended.uptimeMs = mKernel.sys.getTimeMs();
    mManifest.ended.wallUtc  = wallNow();
    mManifest.end            = end;
    mManifest.rowsWritten    = mLog.rows();
    mManifest.rowFailures    = mLog.failures();
    mManifest.bytesWritten   = mLog.bytes();
    mManifest.guiAttached    = mManifest.guiAttached || mGuiStarted;

    mLog.end(mManifest);
    Profile::writeRunManifest(mKernel, mManifest);

    // The profile last, so it carries the closed manifest rather than an open
    // one. It is derived -- everything in it is in the claim store -- so losing
    // it to a cable at the wrong moment loses nothing the next write does not
    // restore.
    mProfile.write(mClaims, mManifest);

    mState.runOpen = false;
    mState.runId   = 0;
    Profile::writeState(mKernel, mState);

    LOG_INFO("run %lu %s: %lu rows, %lu failures, %llu bytes\n",
             static_cast<unsigned long>(mManifest.runId),
             Profile::toString(end),
             static_cast<unsigned long>(mLog.rows()),
             static_cast<unsigned long>(mLog.failures()),
             static_cast<unsigned long long>(mLog.bytes()));
}

// ---------------------------------------------------------------------------
// Layer 1: existence and identity
// ---------------------------------------------------------------------------

void Service::runExistenceSweep()
{
    openRun(CustomMessage::Phase::Existence);

    uint16_t resolved = 0;
    for (size_t t = 0; t < Catalogue::kTypeCount; t++) {
        const auto type = static_cast<SDK::Sensor::Type>(Catalogue::kTypes[t].value);
        const Probes::Identity id = mBus.probe(type);

        mLive[t].handle    = id.handle;
        mLive[t].resolved  = id.resolved;
        mLive[t].connected = false;   // the sweep let go of its connection

        if (id.resolved) {
            resolved++;
            mManifest.typesResolved |= (1ull << t);
        }

        recordIdentity(t, id);

        Profile::ExistenceRecord rec {};
        rec.typeValue   = Catalogue::kTypes[t].value;
        rec.handle      = id.handle;
        rec.resolved    = id.resolved;
        rec.connected   = id.connected;
        rec.driverCount = id.listAnswered
                              ? static_cast<int32_t>(id.handleCount) : -1;
        rec.descriptor  = id.descriptor;
        mLog.writeExistence(mManifest, rec);
    }

    LOG_INFO("existence sweep: %u of %u types resolved a driver\n",
             static_cast<unsigned>(resolved),
             static_cast<unsigned>(Catalogue::kTypeCount));

    closeRun(Profile::RunEnd::Completed);
}

void Service::recordIdentity(size_t typeIdx, const Probes::Identity &id)
{
    const uint32_t now  = mKernel.sys.getTimeMs();
    const int64_t  wall = wallNow();

    // Whether a driver resolved. Absent and silent are different findings, and
    // this is the row that carries the first of them.
    mClaims.record(Catalogue::claimIndex(typeIdx, Metric::DefaultResolves),
                   measuredBool(id.resolved), now, wall);

    // `RequestList`: never sent by any app before this one. Not answered leaves
    // the claim UNVERIFIED rather than recording a count of zero -- "the kernel
    // does not implement this request" and "this type has no drivers" are
    // different findings.
    if (id.listAnswered) {
        mClaims.record(Catalogue::claimIndex(typeIdx, Metric::DriverCount),
                       measured(static_cast<float>(id.handleCount), 1), now, wall);
    }

    // `RequestGetDesc`: the kernel naming its own driver. No numeric value --
    // the string goes in the descriptor table, which the profile writes per
    // sensor -- so the claim records only that an answer came back.
    if (id.descriptorAnswered) {
        mClaims.setDescriptor(typeIdx, id.descriptor);
        Evidence::Measurement m {};
        m.n       = 1;
        m.verdict = Evidence::Verdict::Confirmed;
        mClaims.record(Catalogue::claimIndex(typeIdx, Metric::Descriptor),
                       m, now, wall);
    }

    mClaims.record(Catalogue::claimIndex(typeIdx, Metric::ConnectSucceeds),
                   measuredBool(id.connected), now, wall);

    // Every claim about this sensor that cannot be answered while no driver
    // resolves. Marked INAPPLICABLE with a stated reason rather than left
    // UNVERIFIED, so the report does not count them as gaps somebody could go
    // and fill -- and cleared again the moment a driver appears, which is what
    // makes a firmware update turn a whole protocol back on by itself.
    const size_t total = Catalogue::claimCount();
    for (size_t i = 0; i < total; i++) {
        const Catalogue::ClaimRef r = Catalogue::describe(i);
        if (r.typeIdx != typeIdx) {
            continue;
        }
        // Layer 1's own claims are the ones that establish the condition, so
        // they are never gated on it.
        if (r.layer == Layer::Existence) {
            continue;
        }
        if (!id.resolved) {
            mClaims.markInapplicable(i, Evidence::Note::NoProducer, now, wall);
        } else {
            mClaims.clearInapplicable(i);
        }
    }
}

// ---------------------------------------------------------------------------
// Soak
// ---------------------------------------------------------------------------

void Service::startSoak()
{
    if (mPhase == CustomMessage::Phase::Soak) {
        return;
    }

    openRun(CustomMessage::Phase::Soak);
    subscribeAll();

    const uint32_t now = mKernel.sys.getTimeMs();
    mIntervalOpened = now;
    mNextIntervalAt = now + mSettings.intervalSec * 1000u;
    mSoakDeadline   = (mSettings.soakMaxMinutes > 0)
                          ? now + mSettings.soakMaxMinutes * 60u * 1000u
                          : 0u;
    mSamplesSeen = 0;
    mBatchesSeen = 0;

    mPhase = CustomMessage::Phase::Soak;
}

void Service::stopSoak(Profile::RunEnd end)
{
    if (mPhase != CustomMessage::Phase::Soak) {
        return;
    }

    // Close the partial interval before letting go of the sensors: where a run
    // stopped is the finding, and an interval that never reached storage cannot
    // say where that was.
    const uint32_t now = mKernel.sys.getTimeMs();
    closeInterval(now, static_cast<uint32_t>(diff(mIntervalOpened, now)));

    unsubscribeAll();
    closeRun(end);
    mPhase = (end == Profile::RunEnd::Completed) ? CustomMessage::Phase::Idle
                                                 : CustomMessage::Phase::Truncated;
}

void Service::subscribeAll()
{
    for (size_t t = 0; t < Catalogue::kTypeCount; t++) {
        Live &live = mLive[t];
        live.asked = false;

        if (!live.resolved) {
            continue;
        }
        if (!mSettings.subscribeAll
            && Catalogue::kTypes[t].value != mSettings.onlyType) {
            continue;
        }

        live.asked = true;
        mManifest.typesAsked |= (1ull << t);

        live.stream.reset();
        for (uint8_t f = 0; f < live.fieldSlots; f++) {
            // Enum-like fields get their distinct-value set tracked. Taken from
            // the generated table's own accessor kinds, which is the only place
            // the SDK states a field's type unambiguously -- the doc comments
            // contradict each other and `SensorsLayer.md` contradicts both.
            bool enumLike = false;
            const Catalogue::TypeSpec &spec = Catalogue::kTypes[t];
            if (spec.parser != Catalogue::kNoParser) {
                const Catalogue::ParserSpec &p = Catalogue::kParsers[spec.parser];
                if (f < p.fieldCount) {
                    enumLike = (p.fields[f].kind == Catalogue::FieldKind::U32)
                               || (p.fields[f].kind == Catalogue::FieldKind::I32);
                }
            } else {
                // No parser, so no declared kind. Tracked, because for these
                // five types the distinct-value set is part of the only frame
                // description that will exist anywhere.
                enumLike = true;
            }
            mFields[live.fieldBase + f].reset(enumLike);
        }

        live.connected = mBus.connect(live.handle,
                                      static_cast<float>(mSettings.periodMs),
                                      mSettings.latencyMs);
        if (live.connected) {
            live.stream.onConnected(mKernel.sys.getTimeMs());
        } else {
            // Resolved and refused. A distinct finding from either "no driver"
            // or "no samples", and it is already recorded by layer 1's
            // `connect_succeeds` -- re-recorded here because a connect that
            // worked during the sweep and fails under load is exactly the sort
            // of thing this app exists to notice.
            mClaims.record(Catalogue::claimIndex(t, Metric::ConnectSucceeds),
                           measuredBool(false), mKernel.sys.getTimeMs(),
                           wallNow());
        }
    }
}

void Service::unsubscribeAll()
{
    for (size_t t = 0; t < Catalogue::kTypeCount; t++) {
        if (mLive[t].connected) {
            mBus.disconnect(mLive[t].handle);
            mLive[t].connected = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Sample path
// ---------------------------------------------------------------------------

size_t Service::typeForHandle(uint32_t handle) const
{
    if (handle == 0) {
        return Catalogue::kTypeCount;
    }
    for (size_t t = 0; t < Catalogue::kTypeCount; t++) {
        // Full-width comparison. `SDK::Sensor::Connection::matchesDriver` takes
        // a `uint16_t` against a `uint8_t` member, so it would compare two
        // differently-truncated values; this app keeps both sides at 32 bits.
        if (mLive[t].connected && mLive[t].handle == handle) {
            return t;
        }
    }
    return Catalogue::kTypeCount;
}

void Service::onSensorData(uint32_t handle, uint32_t count, uint32_t stride,
                           const SDK::Sensor::Data *base)
{
    const size_t t = typeForHandle(handle);
    if (t >= Catalogue::kTypeCount || base == nullptr || count == 0) {
        return;
    }

    // Derive and validate the field count *before* constructing any parser.
    //
    // This is the whole of layer 2 and it is also a safety requirement.
    // `DataBatch::calcFieldCount` asserts the stride's divisibility, and an
    // assert is compiled out at -Os -- so a stride the kernel never intended
    // would index past the frame. `GpsLocation::isDataValid()` is worse: it
    // reads field 1 *before* checking the field count, so a one-field frame is
    // an out-of-bounds read in any shipped build. The profiler is the first
    // thing on this platform that will ever meet a frame that does not match
    // its parser, which is precisely the input the parsers were not written
    // for.
    if (stride < sizeof(SDK::Sensor::Data)) {
        return;
    }
    const uint32_t extra = stride - static_cast<uint32_t>(sizeof(SDK::Sensor::Data));
    if ((extra % sizeof(SDK::Sensor::Data::Field)) != 0) {
        // A stride that is not a whole number of fields. Recorded as a frame
        // finding rather than parsed: this is the case `DataBatch` asserts on
        // and then ignores.
        const uint32_t badAt = mKernel.sys.getTimeMs();
        mClaims.record(Catalogue::claimIndex(t, Metric::FieldCount),
                       measured(0.0f, 1, Evidence::Verdict::Refuted),
                       badAt, wallNow());
        // Recorded even though it cannot be parsed -- **especially** because it
        // cannot be parsed. A stride the kernel never intended is the single
        // most valuable frame this app could capture, and a profiler that threw
        // it away would be discarding its best finding to protect a statistic.
        mRaw.write(Catalogue::kTypes[t].value, handle, badAt,
                   static_cast<uint16_t>(count), static_cast<uint16_t>(stride),
                   base);
        return;
    }
    const uint16_t fields =
        static_cast<uint16_t>(extra / sizeof(SDK::Sensor::Data::Field) + 1);

    Live &live = mLive[t];
    const uint32_t now = mKernel.sys.getTimeMs();

    // **The frame goes to storage before anything interprets it.** Verbatim: the
    // stride that is not a whole number of fields, the timestamp that goes
    // backwards, the `mTimeStampUs` of 60000. This app's opinion of the frame
    // ends up in the profile, and the frame itself ends up here, and the two can
    // then be checked against each other rather than one being trusted.
    //
    // Deliberately after the stride validation above and before the statistics
    // below: a frame whose stride is not a whole number of fields is *rejected*
    // for parsing and still *recorded*, because it is the most interesting frame
    // this app will ever meet.
    mRaw.write(Catalogue::kTypes[t].value, handle, now,
               static_cast<uint16_t>(count), static_cast<uint16_t>(stride), base);

    live.stream.onBatch(now, static_cast<uint16_t>(count), fields);
    mBatchesSeen++;
    mManifest.typesDelivered |= (1ull << t);

    const bool wantFields = mSettings.fieldStats;
    const uint8_t slots   = live.fieldSlots;

    for (uint32_t i = 0; i < count; i++) {
        const auto *raw = reinterpret_cast<const uint8_t *>(base) + i * stride;
        const auto *d   = reinterpret_cast<const SDK::Sensor::Data *>(raw);

        // Raw timestamps, exactly as the frame carried them. `mTimeStampUs` is
        // not folded in here: the invariant `DataView::getTimestampUs()` assumes
        // -- that it stays under 1000 -- is what layer 4 is checking, and a
        // helper that had already folded them would have destroyed the evidence.
        live.stream.onSample(d->mTimeStamp, d->mTimeStampUs);
        mSamplesSeen++;

        if (!wantFields) {
            continue;
        }
        // Only the fields the catalogue reserved slots for. A frame wider than
        // that records the first `kAssumedFields` and the extra width shows up
        // in `frame.field_count`, which is the finding -- rather than
        // overflowing, which would be a crash.
        const uint16_t upto = (fields < slots) ? fields : slots;
        for (uint16_t f = 0; f < upto; f++) {
            const uint32_t bits = d->mValue[f].u32;
            mFields[live.fieldBase + f].add(bits, d->mValue[f].f);
        }
    }
}

// ---------------------------------------------------------------------------
// Promotion
// ---------------------------------------------------------------------------

void Service::promoteStreamClaims(size_t typeIdx, uint32_t now, int64_t wall)
{
    const Live &live = mLive[typeIdx];
    if (!live.asked) {
        return;
    }
    const Stats::StreamStats &s = live.stream;

    // -- Layer 2 -----------------------------------------------------------
    if (s.fieldCount() > 0) {
        // Compared against the shipped parser's `Field::COUNT` by the store,
        // from the catalogue's expectation. `conformance` therefore says MATCHES
        // or DIFFERS for the 32 types with a parser and NO_CLAIM for the five
        // without -- and for those five, this number is the only description of
        // the frame that exists anywhere.
        mClaims.record(Catalogue::claimIndex(typeIdx, Metric::FieldCount),
                       measured(static_cast<float>(s.fieldCount()), 1), now, wall);

        const Catalogue::TypeSpec &spec = Catalogue::kTypes[typeIdx];
        if (spec.parser != Catalogue::kNoParser) {
            const Catalogue::ParserSpec &p = Catalogue::kParsers[spec.parser];
            Evidence::Measurement m = measured(
                static_cast<float>(s.fieldCount()) - static_cast<float>(p.fieldCount), 1);
            if (s.fieldCount() < p.fieldCount) {
                m.note = Evidence::Note::ParserFrameShort;
            } else if (s.fieldCount() > p.fieldCount) {
                m.note = Evidence::Note::ParserFrameExtended;
            }
            mClaims.record(Catalogue::claimIndex(typeIdx, Metric::ParserAgreement),
                           m, now, wall);

            // Whether the shipped `isDataValid()` accepts the frame the device
            // actually sends. Computed rather than called: 28 of 29 parsers are
            // exact field-count equality, `HeartRateEx` is `>=`, and the eight
            // that also range-check a value cannot be answered from the width
            // alone -- so this claim is about the *width* test, and the report
            // says so.
            const bool accepts =
                (p.validity == Catalogue::Validity::AtLeast)
                    ? (s.fieldCount() >= p.fieldCount)
                    : (s.fieldCount() == p.fieldCount);
            mClaims.record(
                Catalogue::claimIndex(typeIdx, Metric::ParserAcceptsFrame),
                measuredBool(accepts), now, wall);
        }

        // `RUNNING_CADENCE`'s 4 -> 2 shrink between firmware lines says the
        // answer to "does the width ever change" is not automatically no.
        mClaims.record(Catalogue::claimIndex(typeIdx, Metric::StrideStable),
                       measuredBool(s.fieldCountStable(), s.batches()), now, wall);
    }

    // -- Layer 3 -----------------------------------------------------------
    if (s.hasFirstSample()) {
        mClaims.record(Catalogue::claimIndex(typeIdx, Metric::FirstSampleMs),
                       measured(static_cast<float>(s.firstSampleMs()), 1),
                       now, wall);
    }

    if (s.batches() > 0) {
        mClaims.record(Catalogue::claimIndex(typeIdx, Metric::BatchesPerMin),
                       measured(s.batchesPerMinute(), s.batches()), now, wall);
    }

    // A rate is never recorded without its longest gap. A sensor delivering its
    // nominal average in two bursts an hour apart is not delivering at that
    // rate, and an epoch-based consumer would be silently wrong -- SleepLab rows
    // S14 and S15 are the consequence of exactly this.
    if (s.samples() > 1) {
        mClaims.record(Catalogue::claimIndex(typeIdx, Metric::SamplesPerMin),
                       measured(s.samplesPerMinute(), s.samples()), now, wall);
        mClaims.record(Catalogue::claimIndex(typeIdx, Metric::LongestGapMs),
                       measured(static_cast<float>(s.longestGapMs()), s.samples()),
                       now, wall);
    } else if (s.samples() == 0 && live.connected) {
        // Resolved, connected, and silent. A third finding, distinct from both
        // "no driver" and "delivering slowly", and the one `TOUCH_DETECT` turned
        // out to be: one sample in 507 minutes (ledger row S7).
        Evidence::Measurement m = measured(0.0f, 1);
        m.note = Evidence::Note::ResolvedButSilent;
        mClaims.record(Catalogue::claimIndex(typeIdx, Metric::SamplesPerMin),
                       m, now, wall);
    }

    {
        Evidence::Measurement m = measured(
            static_cast<float>(s.batchSizes().quantile(0.5f)), s.batches());
        if (s.batches() > 0) {
            m.hasSpread  = true;
            m.spread.p05 = s.batchSizes().quantile(0.05f);
            m.spread.p50 = s.batchSizes().quantile(0.5f);
            m.spread.p95 = s.batchSizes().quantile(0.95f);
            mClaims.record(Catalogue::claimIndex(typeIdx, Metric::SamplesPerBatch),
                           m, now, wall);
        }
    }

    const Stats::Cadence cadence = s.cadence();
    if (cadence != Stats::Cadence::Unknown) {
        mClaims.record(Catalogue::claimIndex(typeIdx, Metric::Classification),
                       measured(static_cast<float>(cadence), s.samples()), now, wall);
    }

    // -- Layer 4 -----------------------------------------------------------
    //
    // For an event sensor, dt statistics do not apply. Marked INAPPLICABLE with
    // that reason rather than left UNVERIFIED for ever, so the completeness
    // denominator does not permanently count them as gaps. `TOUCH_DETECT` was
    // assumed streaming, delivered zero samples in a minute, and read as "not
    // worn" -- which would have suppressed every night SleepLab ever recorded
    // (row S12). The classification is measured here so nothing has to assume.
    static const Metric kTimingMetrics[] = {
        Metric::DtMs, Metric::DeliveredHz, Metric::ClockSkewPpm,
    };
    if (cadence == Stats::Cadence::Event) {
        for (Metric m : kTimingMetrics) {
            mClaims.markInapplicable(Catalogue::claimIndex(typeIdx, m),
                                     Evidence::Note::EventSensorNoDt, now, wall);
        }
    } else if (cadence == Stats::Cadence::Streaming) {
        for (Metric m : kTimingMetrics) {
            mClaims.clearInapplicable(Catalogue::claimIndex(typeIdx, m));
        }

        const Stats::DtHistogram &dt = s.dt();
        Evidence::Measurement m = measured(dt.quantile(0.5f), dt.count());
        m.hasSpread  = true;
        m.spread.p05 = dt.quantile(0.05f);
        m.spread.p50 = dt.quantile(0.5f);
        m.spread.p95 = dt.quantile(0.95f);
        mClaims.record(Catalogue::claimIndex(typeIdx, Metric::DtMs), m, now, wall);

        // Delivered rate is 1/p50, and it is recorded alongside its spread,
        // never alone.
        const float p50 = dt.quantile(0.5f);
        if (p50 > 0.0f) {
            Evidence::Measurement hz = measured(1000.0f / p50, dt.count());
            hz.hasSpread  = true;
            // Inverted, so p05 of the rate comes from p95 of the period.
            hz.spread.p05 = (dt.quantile(0.95f) > 0.0f)
                                ? 1000.0f / dt.quantile(0.95f) : 0.0f;
            hz.spread.p50 = 1000.0f / p50;
            hz.spread.p95 = (dt.quantile(0.05f) > 0.0f)
                                ? 1000.0f / dt.quantile(0.05f) : 0.0f;
            mClaims.record(Catalogue::claimIndex(typeIdx, Metric::DeliveredHz),
                           hz, now, wall);
        }

        if (s.hasSkew()) {
            mClaims.record(Catalogue::claimIndex(typeIdx, Metric::ClockSkewPpm),
                           measured(s.skewPpm(), s.samples()), now, wall);
        }
    }

    // These two apply whatever the cadence: a timestamp that goes backwards or a
    // microsecond field over 999 is a defect regardless of how often the sensor
    // speaks.
    if (s.samples() > 0) {
        // Expected zero. A non-zero value means every microsecond timestamp in
        // every app on this platform is wrong, because
        // `DataView::getTimestampUs()` computes `ms * 1000 + us` and nothing has
        // ever checked that `us` stays under a thousand.
        mClaims.record(Catalogue::claimIndex(typeIdx, Metric::TimestampUsOver999),
                       measured(static_cast<float>(s.usOver999()), s.samples()),
                       now, wall);
        mClaims.record(Catalogue::claimIndex(typeIdx, Metric::TimestampMonotonic),
                       measuredBool(s.nonMonotonic() == 0, s.samples()), now, wall);
    }

    if (s.batches() > 1) {
        const Stats::DtHistogram &bi = s.batchIntervals();
        Evidence::Measurement m = measured(bi.quantile(0.5f), bi.count());
        m.hasSpread  = true;
        m.spread.p05 = bi.quantile(0.05f);
        m.spread.p50 = bi.quantile(0.5f);
        m.spread.p95 = bi.quantile(0.95f);
        mClaims.record(Catalogue::claimIndex(typeIdx, Metric::BatchJitterMs),
                       m, now, wall);
    }
}

void Service::promoteFieldClaims(size_t typeIdx, uint32_t now, int64_t wall)
{
    if (!mSettings.fieldStats) {
        return;
    }
    const Live &live = mLive[typeIdx];
    if (!live.asked) {
        return;
    }

    // Only the fields that were actually delivered. A type whose frame is
    // narrower than its reserved slots leaves the unused slots UNVERIFIED, which
    // is correct: nothing was measured there.
    const uint16_t delivered = live.stream.fieldCount();
    const uint8_t  upto = (delivered > 0 && delivered < live.fieldSlots)
                              ? static_cast<uint8_t>(delivered)
                              : live.fieldSlots;

    for (uint8_t f = 0; f < upto; f++) {
        const Stats::FieldStats &fs = mFields[live.fieldBase + f];
        if (fs.count() == 0 && fs.nonFinite() == 0) {
            continue;
        }

        mClaims.record(Catalogue::claimIndex(typeIdx, f, FieldMetric::Min),
                       measured(fs.min(), fs.count()), now, wall);
        mClaims.record(Catalogue::claimIndex(typeIdx, f, FieldMetric::Max),
                       measured(fs.max(), fs.count()), now, wall);
        mClaims.record(Catalogue::claimIndex(typeIdx, f, FieldMetric::Mean),
                       measured(fs.mean(), fs.count()), now, wall);

        // Not hypothetical: one non-finite sample once poisoned every
        // subsequent SleepLab epoch to exactly zero. Recorded with n = 1 so a
        // count of zero is CONFIRMED on the first interval rather than waiting
        // for a minimum -- "no NaNs so far" is itself the useful answer.
        mClaims.record(Catalogue::claimIndex(typeIdx, f, FieldMetric::NonFinite),
                       measured(static_cast<float>(fs.nonFinite()),
                                fs.count() + fs.nonFinite()),
                       now, wall);

        // The row that catches `BATTERY_LEVEL` reading 100.0 % at both ends of
        // an 8.45 h night in which the fuel gauge lost 10 mAh (row S18): not
        // broken enough to be absent, not working enough to be usable.
        mClaims.record(Catalogue::claimIndex(typeIdx, f, FieldMetric::StuckMaxRun),
                       measured(static_cast<float>(fs.stuckMaxRun()), fs.count()),
                       now, wall);
        mClaims.record(Catalogue::claimIndex(typeIdx, f, FieldMetric::EverChanged),
                       measuredBool(fs.everChanged(), fs.count()), now, wall);

        // A lower bound on the quantisation step, and **only meaningful if the
        // value actually varied**.
        //
        // When it has not, the claim is INAPPLICABLE rather than a
        // below-minimum measurement. That distinction matters and it is not
        // pedantry: a quantisation step for a constant does not exist, so no
        // amount of further sampling would produce one, and leaving the row
        // UNVERIFIED would put it on the reader's to-do list for ever. It is
        // cleared the moment the field moves -- which is why the two branches
        // are symmetrical.
        const size_t lsbClaim =
            Catalogue::claimIndex(typeIdx, f, FieldMetric::Lsb);
        if (fs.hasLsb()) {
            mClaims.clearInapplicable(lsbClaim);
            mClaims.record(lsbClaim, measured(fs.lsb(), fs.count()), now, wall);
        } else if (fs.count() > 0) {
            mClaims.markInapplicable(lsbClaim, Evidence::Note::NeverVaried,
                                     now, wall);
        }

        // For an enum or a boolean-in-a-u32, how many distinct values were ever
        // seen. Overflowing the tracked set is itself the answer: a field
        // documented as a four-member enum that produced more than sixteen
        // distinct values is not an enum.
        Evidence::Measurement d =
            measured(static_cast<float>(fs.distinctCount()), fs.count());
        mClaims.record(
            Catalogue::claimIndex(typeIdx, f, FieldMetric::DistinctObserved),
            d, now, wall);
    }
}

void Service::closeInterval(uint32_t now, uint32_t spanMs)
{
    const int64_t wall = wallNow();

    for (size_t t = 0; t < Catalogue::kTypeCount; t++) {
        Live &live = mLive[t];
        if (!live.asked) {
            continue;
        }

        mLog.writeStream(now, wall, Catalogue::kTypes[t].value, spanMs,
                         live.stream);

        // The histograms the `S` row's quantiles came from. Five quantiles chosen
        // in advance cannot show a second mode, and a bursty stream has one.
        const uint32_t type = Catalogue::kTypes[t].value;
        mLog.writeBins(now, type, Profile::BinSeries::SampleDt,
                       live.stream.dt().view());
        mLog.writeBins(now, type, Profile::BinSeries::BatchInterval,
                       live.stream.batchIntervals().view());
        mLog.writeBins(now, type, Profile::BinSeries::BatchSize,
                       live.stream.batchSizes().view());

        promoteStreamClaims(t, now, wall);

        if (mSettings.fieldStats) {
            const uint16_t delivered = live.stream.fieldCount();
            const uint8_t  upto = (delivered > 0 && delivered < live.fieldSlots)
                                      ? static_cast<uint8_t>(delivered)
                                      : live.fieldSlots;
            for (uint8_t f = 0; f < upto; f++) {
                const Stats::FieldStats &fs = mFields[live.fieldBase + f];
                if (fs.count() == 0 && fs.nonFinite() == 0) {
                    continue;
                }
                mLog.writeValue(now, Catalogue::kTypes[t].value, f, fs);
            }
            promoteFieldClaims(t, now, wall);
        }
    }

    // The statistics are **not** reset. They accumulate across the run, because
    // the minimum-n rule is about the whole run rather than about a minute: ten
    // thousand dt samples is three and a half minutes at the accelerometer's
    // measured rate, and resetting every minute would mean no distribution ever
    // reached its minimum. The per-interval rows in the log carry the running
    // totals, so a reader can difference consecutive rows to recover the
    // interval and can also see the run-to-date figure the claim was promoted
    // from. That is why the `S` row carries `samples` and `ts_span_ms` rather
    // than a rate.

    // Push the raw buffer at the interval boundary, so a cable event loses at
    // most one interval's tail rather than the whole buffer.
    mRaw.flush();

    mState.lastUptimeMs = now;
    mState.lastWallUtc  = wall;
    mState.rowsWritten  = mLog.rows();
    Profile::writeState(mKernel, mState);
}

// ---------------------------------------------------------------------------
// GUI
// ---------------------------------------------------------------------------

void Service::publish()
{
    if (!mGuiStarted) {
        return;
    }

    // Rate-limited, because the GUI's queue is ten deep and drops the *oldest*
    // when full -- so an over-eager publisher loses the first roster burst,
    // which is precisely the silent-partial-data failure the burst's explicit
    // indices exist to prevent. A request arriving inside the gap is not
    // ignored: it is carried forward and answered by the next loop iteration.
    const uint32_t now = mKernel.sys.getTimeMs();
    if (mHavePublished && diff(mLastPublishAt, now)
                              < static_cast<int32_t>(kPublishMinGapMs)) {
        mPublishPending = true;
        return;
    }

    mHavePublished  = true;
    mPublishPending = false;
    mLastPublishAt  = now;

    publishStatus();
    publishRoster();
}

void Service::publishStatus()
{
    auto msg = SDK::make_msg<CustomMessage::SensorLabStatus>(mKernel);
    if (!msg) {
        return;
    }

    const Evidence::Completeness c = mClaims.completenessOverall();

    uint16_t asked = 0, resolved = 0, delivering = 0;
    for (size_t t = 0; t < Catalogue::kTypeCount; t++) {
        if (mLive[t].asked)    { asked++; }
        if (mLive[t].resolved) { resolved++; }
        if (mLive[t].stream.samples() > 0) { delivering++; }
    }

    msg->data.runId           = mManifest.runId;
    msg->data.runningMs       = static_cast<uint32_t>(diff(mStartedAt,
                                                      mKernel.sys.getTimeMs()));
    msg->data.rowsWritten     = mLog.rows();
    msg->data.rowFailures     = mLog.failures();
    msg->data.bytesWritten    = static_cast<uint32_t>(mLog.bytes());
    msg->data.samplesSeen     = mSamplesSeen;
    msg->data.batchesSeen     = mBatchesSeen;
    msg->data.typesAsked      = asked;
    msg->data.typesResolved   = resolved;
    msg->data.typesDelivering = delivering;
    msg->data.claimsApplicable = c.applicable;
    msg->data.claimsAnswered   = c.answered;
    msg->data.claimsConfirmed  = c.confirmed;
    msg->data.claimsRefuted    = c.refuted;
    msg->data.phase           = static_cast<uint8_t>(mPhase);
    msg->data.charging        = mCharging;
    msg->data.usb             = mUsb;
    msg->data.haveSystemInfo  = mManifest.haveSystemInfo ? 1 : 0;

    for (size_t i = 0; i < sizeof(msg->data.firmware) - 1; i++) {
        msg->data.firmware[i] = mManifest.firmware[i];
        if (mManifest.firmware[i] == '\0') { break; }
    }
    for (size_t i = 0; i < sizeof(msg->data.hardware) - 1; i++) {
        msg->data.hardware[i] = mManifest.hardware[i];
        if (mManifest.hardware[i] == '\0') { break; }
    }

    msg.send();
}

void Service::publishRoster()
{
    // Indexed bursts, so the GUI reassembles by position rather than by arrival
    // order -- a burst that lost its middle message must show a gap rather than
    // a shifted roster. SleepLab's ledger row T2 is what happens when a burst
    // contract relies on ordering.
    for (size_t first = 0; first < Catalogue::kTypeCount;
         first += CustomMessage::kRosterRowsPerMsg) {

        auto msg = SDK::make_msg<CustomMessage::SensorRoster>(mKernel);
        if (!msg) {
            return;
        }

        msg->first = static_cast<uint8_t>(first);
        msg->total = static_cast<uint8_t>(Catalogue::kTypeCount);

        uint8_t n = 0;
        for (size_t t = first;
             t < Catalogue::kTypeCount && n < CustomMessage::kRosterRowsPerMsg;
             t++, n++) {

            const Live &live = mLive[t];
            const Stats::StreamStats &s = live.stream;
            CustomMessage::RosterRow &row = msg->rows[n];

            row.typeIdx = static_cast<uint8_t>(t);
            row.flags   = 0;
            if (live.resolved)      { row.flags |= CustomMessage::RosterRow::kResolved; }
            if (live.connected)     { row.flags |= CustomMessage::RosterRow::kConnected; }
            if (s.samples() > 0)    { row.flags |= CustomMessage::RosterRow::kEverDelivered; }
            if (live.asked)         { row.flags |= CustomMessage::RosterRow::kAsked; }
            if (Catalogue::kTypes[t].parser == Catalogue::kNoParser) {
                row.flags |= CustomMessage::RosterRow::kNoParser;
            } else if (s.fieldCount() != 0
                       && s.fieldCount()
                              != Catalogue::kParsers[Catalogue::kTypes[t].parser].fieldCount) {
                row.flags |= CustomMessage::RosterRow::kFrameDiffers;
            }

            row.cadence    = static_cast<uint8_t>(s.cadence());
            row.fieldCount = static_cast<uint8_t>(
                (s.fieldCount() > 255) ? 255 : s.fieldCount());

            const float perMinX10 = s.samplesPerMinute() * 10.0f;
            row.samplesPerMinX10 = (perMinX10 >= 65535.0f)
                                       ? 65535u
                                       : static_cast<uint16_t>(perMinX10);
            const uint32_t gapS = s.longestGapMs() / 1000u;
            row.longestGapS = (gapS > 65535u) ? 65535u
                                              : static_cast<uint16_t>(gapS);

            const Evidence::Completeness c = mClaims.completenessForType(t);
            row.completePct  = c.percent();
            row.refutedCount = (c.refuted > 255) ? 255
                                                 : static_cast<uint8_t>(c.refuted);
            row.driverCount  = 0;
            const Evidence::Claim &dc =
                mClaims.at(Catalogue::claimIndex(t, Metric::DriverCount));
            if (dc.hasValue()) {
                row.driverCount = static_cast<uint8_t>(dc.value);
            }
        }

        msg->count = n;
        msg.send();
    }
}

void Service::handleCommand(CustomMessage::Command command)
{
    switch (command) {
        case CustomMessage::Command::RunExistenceSweep:
            if (mPhase == CustomMessage::Phase::Soak) {
                // Refused rather than queued. Re-running layer 1 mid-soak would
                // connect and disconnect every type underneath the soak's own
                // subscriptions, which is exactly the contention this app exists
                // to measure -- doing it by accident would corrupt the run.
                LOG_WARNING("a soak is running; stop it before sweeping\n");
                break;
            }
            runExistenceSweep();
            break;

        case CustomMessage::Command::StartSoak:
            startSoak();
            break;

        case CustomMessage::Command::StopRun:
            if (mPhase == CustomMessage::Phase::Soak) {
                stopSoak(Profile::RunEnd::Completed);
            }
            break;

        case CustomMessage::Command::None:
            break;
    }
    publish();
}

// ---------------------------------------------------------------------------
// The loop
// ---------------------------------------------------------------------------

void Service::run()
{
    start();

    while (true) {
        const uint32_t now = mKernel.sys.getTimeMs();

        // Compute the wait to the next due work and pass it to getMessage, the
        // way Timer and Alarm do. Never poll: this app's sample path is the
        // hottest in the repository, so every avoidable wake is multiplied by
        // however many streams are subscribed.
        uint32_t sleepMs = kIdleWaitMs;

        if (mPhase == CustomMessage::Phase::Soak) {
            int32_t toInterval = diff(now, mNextIntervalAt);
            if (toInterval <= 0) {
                closeInterval(now, static_cast<uint32_t>(diff(mIntervalOpened, now)));
                mIntervalOpened = now;
                // Advance the grid by exactly one interval rather than
                // re-basing on `now`. If the loop overslept by more than a whole
                // interval the catch-up would spin, so skip forward instead --
                // and the skipped intervals are themselves the finding, visible
                // as a jump in uptime_ms between consecutive rows.
                const uint32_t period = mSettings.intervalSec * 1000u;
                do {
                    mNextIntervalAt += period;
                } while (diff(now, mNextIntervalAt) <= 0);
                toInterval = diff(now, mNextIntervalAt);
                publish();
            }
            sleepMs = static_cast<uint32_t>(toInterval);

            // Both caps, because the two failure modes differ: a long run fills
            // the volume slowly and a misconfigured interval fills it fast.
            if (mSoakDeadline != 0 && diff(now, mSoakDeadline) <= 0) {
                LOG_INFO("soak reached its %u-minute limit\n",
                         static_cast<unsigned>(mSettings.soakMaxMinutes));
                stopSoak(Profile::RunEnd::Completed);
                continue;
            }
            if (mLog.bytes() >= static_cast<uint64_t>(mSettings.soakMaxKb) * 1024ull) {
                LOG_INFO("soak reached its %u KB limit\n",
                         static_cast<unsigned>(mSettings.soakMaxKb));
                stopSoak(Profile::RunEnd::Completed);
                continue;
            }
        }

        // A publish that was rate-limited away. Answered as soon as the gap has
        // passed, so a screen that opened during a busy moment still fills in.
        if (mPublishPending && mGuiStarted) {
            const int32_t toGap = static_cast<int32_t>(kPublishMinGapMs)
                                  - diff(mLastPublishAt, now);
            if (toGap <= 0) {
                publish();
            } else if (static_cast<uint32_t>(toGap) < sleepMs) {
                sleepMs = static_cast<uint32_t>(toGap);
            }
        }

        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, sleepMs)) {
            continue;
        }

        switch (msg->getType()) {
            case SDK::MessageType::COMMAND_APP_STOP: {
                // Almost always the USB cable, which terminates every running
                // app. Close the run as truncated rather than completed: a
                // truncated run's distributions are shorter than they look, and
                // a reader has to know.
                if (mPhase == CustomMessage::Phase::Soak) {
                    stopSoak(Profile::RunEnd::TruncatedByUsb);
                }
                unsubscribeAll();
                mKernel.comm.releaseMessage(msg);
                return;
            }

            case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
                const auto *e =
                    static_cast<SDK::Message::Sensor::EventData *>(msg);
                onSensorData(e->handle, e->count, e->stride, e->data);
                break;
            }

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                mGuiStarted          = true;
                mManifest.guiAttached = true;
                publish();
                break;

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                // A soak keeps going. An instrument that stopped measuring when
                // the screen closed would only ever measure watched sensors --
                // and a twelve-hour run is by definition unwatched.
                mGuiStarted = false;
                if (mPhase != CustomMessage::Phase::Soak) {
                    // Nothing is being measured, so there is no reason to hold
                    // the process. A Utility app that lingered would be one more
                    // thing competing for the sensors it exists to characterise.
                    mKernel.comm.releaseMessage(msg);
                    return;
                }
                break;

            case CustomMessage::SENSORLAB_REQUEST:
                // The simulator does not deliver COMMAND_APP_NOTIF_GUI_RUN to
                // the service (ledger row T5), so the GUI's first message is
                // treated as the evidence that a GUI is attached -- which only a
                // GUI can send, and which is better evidence than the
                // notification anyway.
                mGuiStarted           = true;
                mManifest.guiAttached = true;
                publish();
                break;

            case CustomMessage::SENSORLAB_COMMAND: {
                const auto *c = static_cast<CustomMessage::SensorLabCommand *>(msg);
                handleCommand(static_cast<CustomMessage::Command>(c->command));
                break;
            }

            default:
                break;
        }

        mKernel.comm.releaseMessage(msg);
    }
}

