/**
 ******************************************************************************
 * @file    NightHarness.hpp
 * @brief   A whole night through the real Service, at a desk, in milliseconds.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * Why this exists
 *
 * Before this file, nothing in the repository exercised `Service`'s own path.
 * The engine had unit tests over synthetic `ScoringInput`s; the store had
 * tests over synthetic `Epoch`s; the simulator had a screen and no sensors.
 * The stretch between a sample arriving and a summary being written -- the
 * epoch grid, the 30 s/60 s pairing, the pre-roll ring, the backdate, the
 * segmenter's state machine, the resume classification, the alarm, the files
 * -- was reachable only by wearing the watch for eight hours and looking in
 * the morning.
 *
 * That is the wrong feedback loop for the code most likely to be wrong. This
 * runs the same stretch in a few milliseconds.
 *
 * ---------------------------------------------------------------------------
 * How, and what was rejected
 *
 * `Service::run()` blocks on `mKernel.comm.getMessage()` and returns only on
 * `COMMAND_APP_STOP`. Three ways to get inside it were available:
 *
 *   1. **Extract a seam** -- add a `poll()` that performs one iteration, as
 *      `MapManager`'s service does, and drive that. Rejected: the loop is one
 *      of the things under suspicion. Its epoch-grid advance, its
 *      sleep-to-next-deadline arithmetic and its oversleep catch-up are
 *      exactly where a night gets quietly compressed, and a test that replaces
 *      the loop cannot find a bug in the loop.
 *
 *   2. **Restructure** into a pure function over an event stream. The right
 *      shape for a new app; a large rewrite of a working one, and the rewrite
 *      would be unreviewable against the very nights it is meant to validate.
 *
 *   3. **Script the message queue.** `SDK::TestSupport::StubAppComm` is a
 *      virtual class whose `getMessage()` returns nothing. Overriding it lets
 *      the test *be* the kernel: it answers the sensor layer's
 *      resolve/connect/disconnect handshake, delivers sensor batches on a
 *      schedule, advances the uptime clock by exactly the timeout the service
 *      asked to sleep for, and finally hands back an `APP_STOP`.
 *
 * **Option 3**, and the whole of `Service` is then under test unmodified.
 * Nothing in the app knows this file exists.
 *
 * The one concession the app makes is `SleepLab::setWallClockSource()`. The
 * uptime clock was already injectable through `SDK::Interface::ISystem`; the
 * wall clock was `std::time(nullptr)` at six call sites, which meant the
 * bedtime window, the window exit, the alarm and every time-of-day label could
 * only ever be tested at whatever o'clock the suite happened to run. See
 * `WallClock.hpp`.
 *
 * ---------------------------------------------------------------------------
 * What the harness is *not* evidence about
 *
 * Nothing here says anything about a person, about sleep, or about this
 * hardware. The sensor streams are generated, so a scenario proves that the
 * code computes what it claims to compute from a stream of a known shape -- and
 * that is all it proves. It replaces the desk-answerable half of the question,
 * which is most of it, and leaves ledger §2 exactly where it was.
 *
 ******************************************************************************
 */

#ifndef SLEEPLAB_TEST_NIGHTHARNESS_HPP
#define SLEEPLAB_TEST_NIGHTHARNESS_HPP

#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstdint>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>

#include "KernelTestDoubles.hpp"

#include "SDK/Kernel/KernelProviderService.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/SensorLayer/SensorData.hpp"
#include "SDK/SensorLayer/SensorTypes.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometer.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRateEx.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMotionDetect.hpp"

#include "Commands.hpp"
#include "NightStore.hpp"
#include "Engine/Epoch.hpp"
#include "Service.hpp"
#include "WallClock.hpp"

namespace Harness {

// ---------------------------------------------------------------------------
// The scenario language
// ---------------------------------------------------------------------------

/**
 * @brief One stretch of a night with unchanging character.
 *
 * A scenario is a list of these. Everything is stated per minute so a fixture
 * reads as a description of a night rather than as a sample stream: "fifteen
 * minutes settling, four hours still, ten minutes up".
 */
struct Phase
{
    int   minutes     = 0;
    /// Amplitude of the sinusoidal wrist movement, in g, on the x axis. Zero is
    /// a rigid object; ~0.02 g at 0.3 Hz is roughly respiration on a still
    /// wrist; 0.3 g at 1 Hz is somebody moving about.
    float amplitudeG  = 0.0f;
    float freqHz      = 1.0f;
    /// What TOUCH_DETECT would report. Only *changes* are delivered, because
    /// that is what the sensor does (ledger row S12).
    bool  worn        = true;
    /// Heart rate in bpm, or 0 for "the sensor produced nothing".
    int   hrBpm       = 55;
    int   stepsPerMin = 0;
    bool  charging    = false;
};

/// A scenario: a bedtime, a list of phases, and the knobs that make a night
/// hostile rather than typical.
struct Scenario
{
    /// Wall clock the run begins at. The default is 2026-08-18 21:45:00 UTC,
    /// inside the default 21:00-11:00 window, on a Tuesday.
    int64_t startUtc = 1755553500;

    /// Uptime at launch. Not zero by default: a service that only ever starts
    /// at uptime 0 never meets the arithmetic that matters, and the wrap is
    /// reachable by setting this near 2^32.
    uint32_t startUptimeMs = 3600u * 1000u;

    std::vector<Phase> phases;

    /// Index the phase list from this many minutes in. A second launch of the
    /// *same* night carries on where the first stopped rather than starting the
    /// sleeper's evening again, and the gap is however long the watch was on the
    /// charger.
    int phaseOffsetMin = 0;

    /// Delivered accelerometer period, ms. The default is the ~48 Hz measured
    /// on hardware (ledger row S3), NOT the 40 ms requested -- a harness that
    /// fed the requested rate would miss every consequence of the delivered one.
    float accelPeriodMs = 1000.0f / 48.0f;
    /// Delivered batch latency, ms. Batches arrive late and in bursts.
    uint32_t accelLatencyMs = 5000;

    /// Whether TOUCH_DETECT reports its initial state at all. False models the
    /// sensor that resolved and then said nothing for a whole minute on
    /// hardware, which is what row S12 was about.
    bool touchReportsInitialState = true;

    /// Deliver no accelerometer samples at all between these two minutes of the
    /// run, inclusive. Models delivery stopping without the app restarting.
    int accelGapFromMin = -1;
    int accelGapToMin   = -1;

    /// Stamp accelerometer samples with timestamps that run backwards from this
    /// minute: a sensor whose own clock was reset under the app. Every duration in
    /// EpochCounter comes from these, and it uses unsigned differences, so a jump
    /// backwards presents as an enormous forward gap rather than as a negative
    /// one -- which is the behaviour worth pinning either way.
    int accelTimestampJumpAtMin = -1;
    /// How far back, in ms.
    uint32_t accelTimestampJumpBackMs = 0;

    /// Deliver accelerometer samples this many times thinner than the nominal
    /// rate, i.e. one sample every `accelPeriodMs * this`. A delivery that
    /// degrades rather than stopping.
    int accelThinning = 1;

    /// Deliver no TOUCH_DETECT samples ever, after the initial one. Models a
    /// sensor that goes silent mid-night.
    int touchSilentFromMin = -1;

    /// Add this many seconds to the wall clock from this minute onwards,
    /// without touching uptime. Models a timezone change, a host sync or DST.
    int     clockJumpAtMin  = -1;
    int64_t clockJumpSec    = 0;

    /// Stall the service loop at this minute: uptime advances by this much
    /// while `getMessage()` is inside one call, which is what an overslept loop
    /// looks like from in here.
    int      oversleepAtMin = -1;
    uint32_t oversleepMs    = 0;

    /// Return from run() as though the kernel had sent COMMAND_APP_STOP at this
    /// minute. The USB cable going in.
    int stopAtMin = -1;

    /// Deliver a SLEEP_REQUEST at this minute -- somebody opening the app. The
    /// service publishes nothing at all with no GUI attached, deliberately, so
    /// a scenario that wants to see what the screen would show has to open it.
    int guiOpensAtMin = -1;

    /// Open the app one minute before the run ends: the morning glance at the
    /// report, which is the case the report exists for.
    bool guiOpensAtEnd = true;

    /// Deliver EVENT_GLANCE_START at this minute, and a TICK every minute after.
    int glanceOpensAtMin = -1;

    /// settings.json to seed, or empty for none.
    std::string settingsJson;

    // -- Fault injection ----------------------------------------------------
    //
    // Applied after the volume is reset, so they survive into the run. All three
    // hooks already existed on the SDK's InMemoryFileSystem and nothing in this
    // repository used them.

    /// Refuse every write once this many bytes have been written. A volume that
    /// fills at 03:00.
    size_t failWritesAfterBytes = static_cast<size_t>(-1);
    /// Refuse a write-mode open of any path ending with this. ".json" fails the
    /// summary while leaving the index row alone.
    std::string failWriteOpenSuffix;
    /// Fail `close()` on any path containing this, leaving the handle open --
    /// FatFs keeps the FIL and its lock-table entry when the sync fails.
    std::string failCloseContaining;

    /// Keep whatever the previous run left on the volume, including
    /// `night_state.txt`. This is how a resumed night is tested: run the first
    /// half for real, stop it the way the USB cable does, then run the second
    /// half against what the first one actually wrote -- rather than against a
    /// hand-written state file that might not be a state file the app can
    /// produce.
    bool keepFilesystem = false;

    int totalMinutes() const
    {
        int m = 0;
        for (const Phase &p : phases) { m += p.minutes; }
        return m;
    }

    /// The phase covering minute @p m of the *night*, which is @p m plus
    /// `phaseOffsetMin` minutes into the phase list.
    const Phase &at(int m) const
    {
        const int want = m + phaseOffsetMin;
        int acc = 0;
        for (const Phase &p : phases) {
            acc += p.minutes;
            if (want < acc) { return p; }
        }
        return phases.back();
    }
};

// ---------------------------------------------------------------------------
// What a run leaves behind
// ---------------------------------------------------------------------------

/// One thing the service asked the kernel to do, with the wall time it asked
/// at. The alarm's whole value is *when*, so an observation that did not carry
/// a time would not be able to check the one thing that matters.
struct Ask
{
    SDK::MessageType::Type type = 0;
    int64_t wallUtc  = 0;
    uint32_t uptimeMs = 0;
    /// Local minutes past midnight, for readability in a failure message.
    int16_t localMin = -1;
};

struct Observations
{
    /// Every SLEEP_REPORT the service published, in order. This is exactly what
    /// the screen would have drawn, so an assertion here is an assertion about
    /// what a person would have been shown.
    std::vector<CustomMessage::SleepReportData> reports;
    /// Every history burst row set, flattened.
    std::vector<CustomMessage::SleepHistory::Row> historyRows;
    /// Backlight/vibro/buzzer requests, which together are the alarm.
    std::vector<Ask> alarms;
    /// Widget start/stop/update.
    std::vector<Ask> widget;
    /// Glance config and update requests.
    std::vector<Ask> glance;

    bool haveReport() const { return !reports.empty(); }
    const CustomMessage::SleepReportData &lastReport() const
    {
        return reports.back();
    }
    /// The last report that carried a closed night rather than a live one.
    const CustomMessage::SleepReportData *lastReportedNight() const
    {
        for (size_t i = reports.size(); i-- > 0;) {
            if (reports[i].phase ==
                static_cast<uint8_t>(CustomMessage::Phase::Reported)) {
                return &reports[i];
            }
        }
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// The scripted kernel
// ---------------------------------------------------------------------------

/// Sensors the harness can deliver, in the order handles are assigned.
enum class Chan : uint8_t { Accel, Touch, Motion, Activity, Hr, HrEx, Steps,
                            BattLevel, BattCharge, BattMetrics, Count };

class ScriptedComm : public SDK::TestSupport::StubAppComm
{
public:
    ScriptedComm() = default;

    void begin(const Scenario &s, SDK::TestSupport::StubSystem &sys,
               Observations &obs)
    {
        mScn  = &s;
        mSys  = &sys;
        mObs  = &obs;

        mT0        = s.startUptimeMs;
        mStopAtMs  = (s.stopAtMin >= 0)
                         ? mT0 + static_cast<uint32_t>(s.stopAtMin) * 60000u
                         : mT0 + static_cast<uint32_t>(s.totalMinutes()) * 60000u;
        mStopped   = false;
        mHandles.clear();
        mConnected.clear();
        mNextAccelAt = mT0 + s.accelLatencyMs;
        mNextHrAt    = mT0 + 1000;
        mNextHrExAt  = mT0 + 1000;
        mNextBattAt  = mT0 + 30000;
        mNextMetricsAt = mT0 + 30000;
        mNextMinuteAt = mT0 + 60000;
        mTouchPrimed = false;
        mLastWorn    = false;
        mLastCharging = false;
        mStepTotal   = 0;
        mOverslept   = false;
        mMinuteQueue.clear();

        mGuiAtMs = 0;
        if (s.guiOpensAtMin >= 0) {
            mGuiAtMs = mT0 + static_cast<uint32_t>(s.guiOpensAtMin) * 60000u;
        } else if (s.guiOpensAtEnd) {
            mGuiAtMs = (mStopAtMs > mT0 + 60000u) ? mStopAtMs - 30000u : mT0;
        }
        mGuiSent = (mGuiAtMs == 0);
        mGlanceAtMs = (s.glanceOpensAtMin >= 0)
                          ? mT0 + static_cast<uint32_t>(s.glanceOpensAtMin) * 60000u
                          : 0;
        mGlanceSent = (mGlanceAtMs == 0);
    }

    /// Uptime -> wall clock. The pair advances together, so a duration derived
    /// from uptime and a time of day read from the wall clock agree -- except
    /// where a scenario deliberately makes them disagree.
    int64_t wallAt(uint32_t uptimeMs) const
    {
        const int64_t elapsed = static_cast<int64_t>(uptimeMs - mT0) / 1000;
        int64_t w = mScn->startUtc + elapsed;
        if (mScn->clockJumpAtMin >= 0 &&
            minuteOf(uptimeMs) >= mScn->clockJumpAtMin) {
            w += mScn->clockJumpSec;
        }
        return w;
    }

    int minuteOf(uint32_t uptimeMs) const
    {
        return static_cast<int>((uptimeMs - mT0) / 60000u);
    }

    // -- The kernel's half of the sensor-layer handshake ---------------------

    bool sendMessage(SDK::MessageBase *msg, uint32_t timeoutMs) override
    {
        (void)timeoutMs;
        if (msg == nullptr) { return false; }

        switch (msg->getType()) {
            case SDK::MessageType::REQUEST_SENSOR_LAYER_GET_DEFAULT: {
                auto *r = static_cast<SDK::Message::Sensor::RequestDefault *>(msg);
                const Chan c = chanFor(r->id);
                if (c == Chan::Count) {
                    // No driver resolved. This is what SPO2 and HEART_BEAT did
                    // on hardware (rows S4, S5): connect() is called and comes
                    // back with nothing to subscribe to.
                    r->handle = 0;
                    setFail(msg);
                    return true;
                }
                r->handle = static_cast<uint32_t>(c) + 1;
                mHandles[r->handle] = c;
                setOk(msg);
                return true;
            }
            case SDK::MessageType::REQUEST_SENSOR_LAYER_CONNECT: {
                auto *r = static_cast<SDK::Message::Sensor::RequestConnect *>(msg);
                mConnected[r->handle] = true;
                setOk(msg);
                return true;
            }
            case SDK::MessageType::REQUEST_SENSOR_LAYER_DISCONNECT: {
                auto *r = static_cast<SDK::Message::Sensor::RequestDisconnect *>(msg);
                mConnected[r->handle] = false;
                setOk(msg);
                return true;
            }

            case CustomMessage::SLEEP_REPORT: {
                auto *r = static_cast<CustomMessage::SleepReport *>(msg);
                mObs->reports.push_back(r->data);
                setOk(msg);
                return true;
            }
            case CustomMessage::SLEEP_HISTORY: {
                auto *r = static_cast<CustomMessage::SleepHistory *>(msg);
                for (uint8_t i = 0; i < r->count && i < CustomMessage::kHistoryRowsPerMsg; i++) {
                    mObs->historyRows.push_back(r->rows[i]);
                }
                setOk(msg);
                return true;
            }

            case SDK::MessageType::REQUEST_BACKLIGHT_SET:
            case SDK::MessageType::REQUEST_VIBRO_PLAY:
            case SDK::MessageType::REQUEST_BUZZER_PLAY:
                mObs->alarms.push_back(stamp(msg->getType()));
                setOk(msg);
                return true;

            case SDK::MessageType::REQUEST_WIDGET_START:
            case SDK::MessageType::REQUEST_WIDGET_STOP:
            case SDK::MessageType::REQUEST_WIDGET_UPDATE:
                mObs->widget.push_back(stamp(msg->getType()));
                setOk(msg);
                return true;

            case SDK::MessageType::REQUEST_GLANCE_CONFIG: {
                auto *r = static_cast<SDK::Message::RequestGlanceConfig *>(msg);
                r->width       = 240;
                r->height      = 90;
                r->maxControls = 8;
                mObs->glance.push_back(stamp(msg->getType()));
                setOk(msg);
                return true;
            }
            case SDK::MessageType::REQUEST_GLANCE_UPDATE:
                mObs->glance.push_back(stamp(msg->getType()));
                setOk(msg);
                return true;

            default:
                setOk(msg);
                return true;
        }
    }

    // -- The loop's wait -----------------------------------------------------

    /**
     * @brief Sleep for at most @p timeoutMs, delivering whatever comes first.
     *
     * This is where the harness's model of time lives. The service asked to
     * sleep until its next due work; the kernel's job is to wake it either at
     * that deadline or earlier with a message. Advancing the clock by exactly
     * the requested timeout is what makes the epoch grid, the duty cycle and
     * the oversleep catch-up testable -- a harness that advanced time by a
     * fixed tick would test a different loop.
     */
    bool getMessage(SDK::MessageBase *&msg, uint32_t timeoutMs) override
    {
        msg = nullptr;
        const uint32_t now = mSys->nowMs;

        // A deliberate stall: the loop was descheduled for longer than an
        // epoch. Delivered by advancing straight past the deadline.
        if (!mOverslept && mScn->oversleepAtMin >= 0 &&
            minuteOf(now) >= mScn->oversleepAtMin) {
            mOverslept = true;
            advanceTo(now + mScn->oversleepMs);
            return false;
        }

        const uint32_t deadline = now + timeoutMs;

        // Somebody opening the app. Delivered as SLEEP_REQUEST rather than
        // COMMAND_APP_NOTIF_GUI_RUN because that is what the simulator was
        // found to do and what the service now relies on (ledger row T5).
        if (!mGuiSent && static_cast<int32_t>(deadline - mGuiAtMs) >= 0) {
            mGuiSent = true;
            advanceTo(mGuiAtMs);
            msg = control(CustomMessage::SLEEP_REQUEST);
            if (msg != nullptr) { return true; }
        }

        if (!mGlanceSent && static_cast<int32_t>(deadline - mGlanceAtMs) >= 0) {
            mGlanceSent  = true;
            mGlanceTickAt = mGlanceAtMs + kGlanceTickMs;
            advanceTo(mGlanceAtMs);
            msg = control(SDK::MessageType::EVENT_GLANCE_START);
            if (msg != nullptr) { return true; }
        }

        // The carousel ticks while the glance is on screen, and the tick is the
        // only thing that sends content: a service that never sees one publishes
        // nothing, and a service that mishandles one publishes nothing either.
        //
        // Never earlier than now -- a tick whose due time has already gone past
        // is late, not a reason to move the clock backwards.
        if (mGlanceSent && mGlanceAtMs != 0) {
            if (static_cast<int32_t>(mGlanceTickAt - now) < 0) {
                mGlanceTickAt = now;
            }
            if (static_cast<int32_t>(deadline - mGlanceTickAt) >= 0) {
                advanceTo(mGlanceTickAt);
                mGlanceTickAt += kGlanceTickMs;
                msg = control(SDK::MessageType::EVENT_GLANCE_TICK);
                if (msg != nullptr) { return true; }
            }
        }

        // APP_STOP takes priority over anything scheduled after it.
        if (static_cast<int32_t>(deadline - mStopAtMs) >= 0 && !mStopped) {
            mStopped = true;
            advanceTo(mStopAtMs);
            msg = control(SDK::MessageType::COMMAND_APP_STOP);
            return msg != nullptr;
        }

        // The earliest sensor delivery due at or before the deadline.
        uint32_t bestAt = 0;
        Chan     bestCh = Chan::Count;
        auto consider = [&](uint32_t at, Chan c) {
            if (!isConnected(c)) { return; }
            if (static_cast<int32_t>(at - deadline) > 0) { return; }
            if (bestCh == Chan::Count || static_cast<int32_t>(at - bestAt) < 0) {
                bestAt = at;
                bestCh = c;
            }
        };
        consider(mNextAccelAt, Chan::Accel);
        consider(mNextHrAt,    Chan::Hr);
        consider(mNextHrExAt,  Chan::HrEx);
        consider(mNextBattAt,  Chan::BattLevel);
        consider(mNextMetricsAt, Chan::BattMetrics);
        // The event sensors all fire on the minute boundary, which is when the
        // scenario's phase can change.
        consider(mNextMinuteAt, Chan::Touch);

        if (bestCh == Chan::Count) {
            advanceTo(deadline);
            return false;
        }

        advanceTo(bestAt);
        msg = build(bestCh, bestAt);
        return msg != nullptr;
    }

private:
    /// A bare typed message, for the lifecycle events the kernel would send.
    SDK::MessageBase *control(SDK::MessageType::Type t)
    {
        void *raw = ::operator new(sizeof(SDK::MessageID), std::nothrow);
        if (raw == nullptr) { return nullptr; }
        auto *m = new (raw) SDK::MessageID();
        m->setType(t);
        return m;
    }

    void advanceTo(uint32_t t)
    {
        mSys->nowMs = t;
        gWallNow    = wallAt(t);
    }

    Ask stamp(SDK::MessageType::Type t) const
    {
        Ask a;
        a.type     = t;
        a.uptimeMs = mSys->nowMs;
        a.wallUtc  = wallAt(mSys->nowMs);
        std::time_t tt = static_cast<std::time_t>(a.wallUtc);
        std::tm g {};
        if (gmtime_r(&tt, &g) != nullptr) {
            a.localMin = static_cast<int16_t>(g.tm_hour * 60 + g.tm_min);
        }
        return a;
    }

    static void setOk(SDK::MessageBase *m)   { m->setResult(SDK::MessageResult::SUCCESS); }
    static void setFail(SDK::MessageBase *m) { m->setResult(SDK::MessageResult::FAIL); }

    static Chan chanFor(SDK::Sensor::Type t)
    {
        switch (t) {
            case SDK::Sensor::Type::ACCELEROMETER:        return Chan::Accel;
            case SDK::Sensor::Type::TOUCH_DETECT:         return Chan::Touch;
            case SDK::Sensor::Type::MOTION_DETECT:        return Chan::Motion;
            case SDK::Sensor::Type::ACTIVITY_RECOGNITION: return Chan::Activity;
            case SDK::Sensor::Type::HEART_RATE:           return Chan::Hr;
            case SDK::Sensor::Type::HEART_RATE_EX:        return Chan::HrEx;
            case SDK::Sensor::Type::STEP_COUNTER:         return Chan::Steps;
            case SDK::Sensor::Type::BATTERY_LEVEL:        return Chan::BattLevel;
            case SDK::Sensor::Type::BATTERY_CHARGING:     return Chan::BattCharge;
            case SDK::Sensor::Type::BATTERY_METRICS:      return Chan::BattMetrics;
            default:                                      return Chan::Count;
        }
    }

    bool isConnected(Chan c) const
    {
        const uint32_t h = static_cast<uint32_t>(c) + 1;
        auto it = mConnected.find(h);
        return it != mConnected.end() && it->second;
    }

    /// Allocate an EventData with room for @p count samples of @p fields
    /// fields each, and fill in the header. Freed by the service through
    /// `releaseMessage`, exactly as a pooled message would be.
    SDK::Message::Sensor::EventData *
    newBatch(Chan c, uint16_t count, uint16_t fields)
    {
        const size_t stride = sizeof(SDK::Sensor::Data) +
                              (fields - 1) * sizeof(SDK::Sensor::Data::Field);
        const size_t head   = sizeof(SDK::Message::Sensor::EventData) -
                              sizeof(SDK::Sensor::Data);
        void *raw = ::operator new(head + stride * count, std::nothrow);
        if (raw == nullptr) { return nullptr; }
        std::memset(raw, 0, head + stride * count);
        auto *e = new (raw) SDK::Message::Sensor::EventData();
        e->handle = static_cast<uint32_t>(c) + 1;
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

    SDK::MessageBase *build(Chan c, uint32_t at)
    {
        switch (c) {
            case Chan::Accel:     return buildAccel(at);
            case Chan::Hr:        return buildHr(at);
            case Chan::HrEx:      return buildHrEx(at);
            case Chan::BattLevel:   return buildBatt(at);
            case Chan::BattMetrics: return buildMetrics(at);
            case Chan::Touch:     return buildMinuteEvent(at);
            default:              return nullptr;
        }
    }

    SDK::MessageBase *buildAccel(uint32_t at)
    {
        const uint32_t period = static_cast<uint32_t>(mScn->accelPeriodMs + 0.5f);
        mNextAccelAt = at + mScn->accelLatencyMs;

        const int minute = minuteOf(at);
        if (mScn->accelGapFromMin >= 0 && minute >= mScn->accelGapFromMin &&
            minute <= mScn->accelGapToMin) {
            // Delivery stopped. Nothing at all rather than rows of zeroes: a
            // stalled sensor layer sends no batch, which is the case the
            // recorder has to notice from the absence.
            return nullptr;
        }

        // The batch covers the interval that has just elapsed, stamped with the
        // sensor's own timestamps -- late and in a burst, which is what the
        // recorder has to attribute correctly rather than to the instant of
        // delivery.
        const uint32_t thinning =
            (mScn->accelThinning > 0) ? static_cast<uint32_t>(mScn->accelThinning) : 1;
        const uint16_t n =
            static_cast<uint16_t>(mScn->accelLatencyMs / (period * thinning));
        if (n == 0) { return nullptr; }
        auto *e = newBatch(Chan::Accel, n,
                           SDK::SensorDataParser::Accelerometer::Field::COUNT);
        if (e == nullptr) { return nullptr; }

        const uint32_t first = at - mScn->accelLatencyMs;
        for (uint16_t i = 0; i < n; i++) {
            const uint32_t realTs = first + i * period * thinning;
            // The phase comes from the real instant, and only the *stamp* is
            // shifted: a sensor whose clock was reset is wrong about the time, not
            // about the wrist.
            const Phase &p = mScn->at(minuteOf(realTs));
            uint32_t ts = realTs;
            if (mScn->accelTimestampJumpAtMin >= 0 &&
                minute >= mScn->accelTimestampJumpAtMin) {
                ts -= mScn->accelTimestampJumpBackMs;
            }
            SDK::Sensor::Data *d = sampleAt(e, i);
            d->mTimeStamp   = ts;
            d->mTimeStampUs = 0;
            const float t = static_cast<float>(ts) * 0.001f;
            d->mValue[0].f = p.amplitudeG *
                             std::sin(6.28318530718f * p.freqHz * t);
            d->mValue[1].f = 0.0f;
            // Gravity on z. A constant on one axis is what the high-pass is
            // there to remove, and putting it in is the only way to find out
            // whether it does.
            d->mValue[2].f = 1.0f;
        }
        return e;
    }

    SDK::MessageBase *buildHr(uint32_t at)
    {
        mNextHrAt = at + 1000;
        const Phase &p = mScn->at(minuteOf(at));
        if (p.hrBpm <= 0) {
            return nullptr;   // the sensor produced nothing this second
        }

        auto *e = newBatch(Chan::Hr, 1,
                           SDK::SensorDataParser::HeartRate::Field::COUNT);
        if (e == nullptr) { return nullptr; }
        SDK::Sensor::Data *d = sampleAt(e, 0);
        d->mTimeStamp  = at;
        d->mValue[0].f = static_cast<float>(p.hrBpm);
        // Trust tracks movement on real hardware: measured across the 2026-08-19
        // night it sat at ~2.8 of 3.0 while settled and fell to 1.2 once the wearer
        // was up (ledger row S10's night). Modelled the same way, so a test can
        // tell a struggling sensor from a low heart rate.
        d->mValue[1].f = (p.amplitudeG > 0.01f) ? 1.2f : 2.8f;
        return e;
    }

    /// Voltage, current, average current, remaining and design capacity -- the
    /// channel the diagnostics setting adds. The numbers follow the shape measured
    /// on the 2026-08-19 night: capacity falling ~1.2 mAh an hour off a 216 mAh
    /// pack, while the percent gauge would not have moved at all (ledger row S18).
    SDK::MessageBase *buildMetrics(uint32_t at)
    {
        mNextMetricsAt = at + 30000;
        auto *e = newBatch(Chan::BattMetrics, 1, 5);
        if (e == nullptr) { return nullptr; }
        SDK::Sensor::Data *d = sampleAt(e, 0);
        d->mTimeStamp  = at;
        const float hours = static_cast<float>(minuteOf(at)) / 60.0f;
        d->mValue[0].f = 4.05f;                       // volts
        d->mValue[1].f = -1.33f;                      // mA, sign per firmware
        d->mValue[2].f = -1.20f;                      // averaged mA
        d->mValue[3].f = 216.0f - hours * 1.2f;       // remaining mAh
        d->mValue[4].f = 250.0f;                      // design mAh
        return e;
    }

    /// The arbitrated heart rate and, crucially, *which source won*. Hardware
    /// delivers one of these per HR sample -- measured, 30 169 against 30 168 --
    /// and every one of them was optical with none external and none unattributed
    /// (ledger row S8). Modelled the same way, because the counts are what would
    /// show a strap and the wrist trading places.
    SDK::MessageBase *buildHrEx(uint32_t at)
    {
        mNextHrExAt = at + 1000;
        const Phase &p = mScn->at(minuteOf(at));
        if (p.hrBpm <= 0) {
            return nullptr;
        }
        auto *e = newBatch(Chan::HrEx, 1,
                           SDK::SensorDataParser::HeartRateEx::Field::COUNT);
        if (e == nullptr) { return nullptr; }
        SDK::Sensor::Data *d = sampleAt(e, 0);
        using F = SDK::SensorDataParser::HeartRateEx::Field;
        using Src = SDK::SensorDataParser::HeartRateEx::Source;
        d->mTimeStamp = at;
        d->mValue[F::BPM].f            = static_cast<float>(p.hrBpm);
        d->mValue[F::TRUST_LEVEL].f    = (p.amplitudeG > 0.01f) ? 1.2f : 2.8f;
        d->mValue[F::SOURCE].f         = static_cast<float>(Src::OPTICAL);
        d->mValue[F::OPTICAL_BPM].f    = static_cast<float>(p.hrBpm);
        d->mValue[F::OPTICAL_TRUST].f  = (p.amplitudeG > 0.01f) ? 1.2f : 2.8f;
        d->mValue[F::EXTERNAL_BPM].f   = 0.0f;
        d->mValue[F::EXTERNAL_TRUST].f = 0.0f;
        return e;
    }

    SDK::MessageBase *buildBatt(uint32_t at)
    {
        mNextBattAt = at + 30000;
        auto *e = newBatch(Chan::BattLevel, 1, 1);
        if (e == nullptr) { return nullptr; }
        SDK::Sensor::Data *d = sampleAt(e, 0);
        d->mTimeStamp  = at;
        // Falls a percent an hour, which is a shape rather than a measurement.
        d->mValue[0].f = 100.0f - static_cast<float>(minuteOf(at)) / 60.0f;
        return e;
    }

    /**
     * @brief The event sensors, which fire only on a change of state.
     *
     * Queued at the minute boundary and handed back one per call: touch, then
     * charging, then steps, then motion. The service reads them all through the
     * same handler so the order is not load-bearing -- but delivering them one
     * at a time is, because a real event sensor does.
     */
    SDK::MessageBase *buildMinuteEvent(uint32_t at)
    {
        const int minute = minuteOf(at);
        const Phase &p = mScn->at(minute);

        // Queue everything this minute changed, then hand back one per call.
        if (mMinuteQueue.empty()) {
            const bool silent = mScn->touchSilentFromMin >= 0 &&
                                minute >= mScn->touchSilentFromMin;
            if (!silent && (!mTouchPrimed || p.worn != mLastWorn)) {
                if (mTouchPrimed || mScn->touchReportsInitialState) {
                    mMinuteQueue.push_back(Chan::Touch);
                }
                mTouchPrimed = true;
                mLastWorn    = p.worn;
            }
            if (p.charging != mLastCharging || minute == 0) {
                mMinuteQueue.push_back(Chan::BattCharge);
                mLastCharging = p.charging;
            }
            if (p.stepsPerMin > 0) {
                mStepTotal += static_cast<uint32_t>(p.stepsPerMin);
                mMinuteQueue.push_back(Chan::Steps);
            } else if (minute == 0) {
                mMinuteQueue.push_back(Chan::Steps);
            }
            if (p.amplitudeG > 0.05f) {
                mMinuteQueue.push_back(Chan::Motion);
            }
        }

        if (mMinuteQueue.empty()) {
            mNextMinuteAt = at + 60000;
            return nullptr;
        }

        const Chan c = mMinuteQueue.front();
        mMinuteQueue.erase(mMinuteQueue.begin());
        if (mMinuteQueue.empty()) {
            mNextMinuteAt = at + 60000;
        }

        switch (c) {
            case Chan::Touch: {
                auto *e = newBatch(Chan::Touch, 1, 1);
                if (e == nullptr) { return nullptr; }
                SDK::Sensor::Data *d = sampleAt(e, 0);
                d->mTimeStamp    = at;
                d->mValue[0].u32 = p.worn ? 1u : 0u;
                return e;
            }
            case Chan::BattCharge: {
                auto *e = newBatch(Chan::BattCharge, 1, 2);
                if (e == nullptr) { return nullptr; }
                SDK::Sensor::Data *d = sampleAt(e, 0);
                d->mTimeStamp    = at;
                d->mValue[0].u32 = p.charging ? 1u : 0u;
                d->mValue[1].u32 = p.charging ? 1u : 0u;
                return e;
            }
            case Chan::Steps: {
                auto *e = newBatch(Chan::Steps, 1, 1);
                if (e == nullptr) { return nullptr; }
                SDK::Sensor::Data *d = sampleAt(e, 0);
                d->mTimeStamp    = at;
                d->mValue[0].u32 = mStepTotal;
                return e;
            }
            case Chan::Motion: {
                auto *e = newBatch(Chan::Motion, 1, 1);
                if (e == nullptr) { return nullptr; }
                SDK::Sensor::Data *d = sampleAt(e, 0);
                d->mTimeStamp    = at;
                d->mValue[0].u32 = static_cast<uint32_t>(
                    SDK::SensorDataParser::MotionDetect::Motion::MOTION);
                return e;
            }
            default:
                return nullptr;
        }
    }

    const Scenario                     *mScn = nullptr;
    SDK::TestSupport::StubSystem       *mSys = nullptr;
    Observations                       *mObs = nullptr;

    uint32_t mT0 = 0;
    uint32_t mStopAtMs = 0;
    bool     mStopped  = false;

    std::map<uint32_t, Chan> mHandles;
    std::map<uint32_t, bool> mConnected;

    uint32_t mNextAccelAt  = 0;
    uint32_t mNextHrAt     = 0;
    uint32_t mNextHrExAt   = 0;
    uint32_t mNextBattAt   = 0;
    uint32_t mNextMetricsAt = 0;
    uint32_t mNextMinuteAt = 0;
    std::vector<Chan> mMinuteQueue;

    bool     mTouchPrimed  = false;
    bool     mLastWorn     = false;
    bool     mLastCharging = false;
    uint32_t mStepTotal    = 0;
    bool     mOverslept    = false;
    uint32_t mGuiAtMs      = 0;
    bool     mGuiSent      = true;
    uint32_t mGlanceAtMs   = 0;
    bool     mGlanceSent   = true;
    uint32_t mGlanceTickAt = 0;
    /// The real carousel ticks faster than this. A minute keeps a scenario to
    /// hundreds of tick messages rather than tens of thousands, and the contract
    /// under test -- that a tick is what sends content, and that the form's
    /// validity flag decides whether anything goes -- does not depend on the rate.
    static constexpr uint32_t kGlanceTickMs = 60000;

public:
    /// The wall clock the app sees. A plain global because the app's seam is a
    /// function pointer and there is one harness per process.
    inline static int64_t gWallNow = 0;
};

// ---------------------------------------------------------------------------
// The rig
// ---------------------------------------------------------------------------

/**
 * @brief One process-lifetime kernel, because the SDK's provider latches.
 *
 * `KernelProviderService::CreateInstance` remembers the first kernel it is
 * given for the life of the process, and `SDK::Sensor::Connection` resolves its
 * kernel from there in its constructor. So the interfaces have to outlive every
 * scenario and be reset between them rather than rebuilt.
 */
class Rig
{
public:
    static Rig &instance()
    {
        static Rig r;
        return r;
    }

    /// Run one scenario to completion and return what it left behind.
    Observations run(const Scenario &s)
    {
        if (!s.keepFilesystem) {
            reset();
        } else {
            fs.failWritesAfterBytes = static_cast<size_t>(-1);
            fs.failWriteOpenSuffix.clear();
            fs.closeGate = nullptr;
        }

        fs.failWritesAfterBytes = s.failWritesAfterBytes;
        fs.failWriteOpenSuffix  = s.failWriteOpenSuffix;
        if (!s.failCloseContaining.empty()) {
            const std::string needle = s.failCloseContaining;
            fs.closeGate = [needle](const std::string &path) {
                return path.find(needle) == std::string::npos;
            };
        }

        Observations obs;
        if (!s.settingsJson.empty()) {
            fs.seedFile("settings.json", s.settingsJson);
        }
        system.nowMs = s.startUptimeMs;
        comm.begin(s, system, obs);
        ScriptedComm::gWallNow = comm.wallAt(s.startUptimeMs);
        SleepLab::setWallClockSource(&wallSource);

        {
            Service svc(*mKernel);
            svc.run();
        }

        SleepLab::setWallClockSource(nullptr);
        return obs;
    }

    /// Seed a file before the run, for the resume and fault-injection cases.
    void seed(const std::string &path, const std::string &content)
    {
        fs.seedFile(path, content);
    }

    void reset()
    {
        fs.files.clear();
        fs.flushCounts.clear();
        fs.openHandles.clear();
        fs.bytesWritten = 0;
        fs.failWritesAfterBytes = static_cast<size_t>(-1);
        fs.failWriteOpenSuffix.clear();
        fs.closeGate = nullptr;
    }

    SDK::TestSupport::StubSystem       system;
    SDK::TestSupport::StubLogger       logger;
    SDK::TestSupport::StubAppMemory    memory;
    ScriptedComm                       comm;
    SDK::TestSupport::InMemoryFileSystem fs;

private:
    Rig()
    {
        mKernel = new SDK::Kernel(system, logger, memory, comm, fs);
        SDK::KernelProviderService::CreateInstance(mKernel);
    }

    static int64_t wallSource() { return ScriptedComm::gWallNow; }

    SDK::Kernel *mKernel = nullptr;
};

// ---------------------------------------------------------------------------
// Reading what a run wrote
// ---------------------------------------------------------------------------

/// One parsed row of an epoch CSV. Field for field with `kEpochHeader`.
struct EpochRow
{
    long long uptimeMs = 0, wallUtc = 0;
    long spanMs = 0, count = 0, peak = 0, samples = 0;
    long motion = 0, sigMotion = 0, stepDelta = 0;
    long hrMean = 0, hrMin = 0, hrSamples = 0, hrSource = 0;
    long wornPct = 0, wornEdges = 0, battPct = 0, charging = 0;
};

inline std::vector<std::string> lines(const std::string &s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        size_t nl = s.find('\n', i);
        if (nl == std::string::npos) { nl = s.size(); }
        if (nl > i) { out.push_back(s.substr(i, nl - i)); }
        i = nl + 1;
    }
    return out;
}

inline std::vector<EpochRow> parseEpochs(const std::string &csv)
{
    std::vector<EpochRow> out;
    for (const std::string &l : lines(csv)) {
        if (l.empty() || l[0] == '#' || l[0] == 'u') { continue; }
        EpochRow r;
        if (std::sscanf(l.c_str(),
                        "%lld,%lld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,"
                        "%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld",
                        &r.uptimeMs, &r.wallUtc, &r.spanMs, &r.count, &r.peak,
                        &r.samples, &r.motion, &r.sigMotion, &r.stepDelta,
                        &r.hrMean, &r.hrMin, &r.hrSamples, &r.hrSource,
                        &r.wornPct, &r.wornEdges, &r.battPct,
                        &r.charging) == 17) {
            out.push_back(r);
        }
    }
    return out;
}

/// The path of the single night the run wrote, or "".
///
/// `index.csv` is the history and `watching.csv` is the idle record -- both live in
/// `Nights/` and neither is a night. Two tests caught the second one the moment it
/// started being written, which is what they are for.
inline std::string theNightCsv(const SDK::TestSupport::InMemoryFileSystem &fs)
{
    for (const auto &kv : fs.files) {
        if (!kv.second.exists) { continue; }
        const std::string &p = kv.first;
        if (p.rfind("Nights/", 0) == 0 && p.size() > 4 &&
            p.compare(p.size() - 4, 4, ".csv") == 0 &&
            p != "Nights/index.csv" &&
            p != SleepLab::kWatchingPath) {
            return p;
        }
    }
    return "";
}

/// A very small JSON field reader: enough to assert on the summary without
/// pulling coreJSON into a test that is about the recorder.
inline std::string jsonField(const std::string &json, const std::string &key)
{
    const std::string k = "\"" + key + "\":";
    size_t i = json.find(k);
    if (i == std::string::npos) { return ""; }
    i += k.size();
    while (i < json.size() && (json[i] == ' ' || json[i] == '\n')) { ++i; }
    size_t j = i;
    if (j < json.size() && json[j] == '"') {
        ++j;
        while (j < json.size() && json[j] != '"') { ++j; }
        return json.substr(i + 1, j - i - 1);
    }
    while (j < json.size() && json[j] != ',' && json[j] != '}' &&
           json[j] != '\n') { ++j; }
    return json.substr(i, j - i);
}

} // namespace Harness

#endif // SLEEPLAB_TEST_NIGHTHARNESS_HPP
