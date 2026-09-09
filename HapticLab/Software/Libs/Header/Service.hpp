#ifndef HAPTICLAB_SERVICE_HPP
#define HAPTICLAB_SERVICE_HPP

#include <cstdint>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/Kernel.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"

#include "SweepStats.hpp"

/// Phase 1, and nothing else. A GUI process cannot reach a sensor, so the
/// effect sweep lives here where the play command and the accelerometer samples
/// share one process and one clock; the protocol, the probe and every file a
/// session writes are the GUI half's.
class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    ~Service();

    void run();

private:
    enum class Phase : uint8_t {
        Idle = 0,
        Settle,  ///< Measuring this wrist's quiet baseline.
        Capture, ///< The effect is playing, or has just been asked to.
        Count,
    };

    void handleSensorData(uint16_t handle, SDK::Sensor::DataBatch &batch);
    void startSweep(const char *runId, uint8_t firstEffect, uint8_t lastEffect, uint8_t repeats);
    void stopSweep(const char *reason);
    void tick(uint32_t nowMs);
    void beginSettle(uint32_t nowMs);
    void beginCapture();
    void finishCapture();
    void advance();
    void publishProgress(uint16_t samples, bool finished);
    bool openRawFile();
    void closeRawFile();
    void writeEffectRow();
    void writeRawRow(const Haptic::ImuSample &sample, const char *phase);

    /// FUSION_RAW at a 10 ms request, which is what Squash's recorder asks for
    /// and what its six pulled captures deliver: 99.52 Hz, a 10 ms modal gap
    /// and a 36 ms worst gap over about 35 minutes of real wrist data. A 100 ms
    /// request would be snapped to 80 ms, so the ladder matters
    /// (`RustGuiPoc/Docs/FINDINGS.md`).
    ///
    /// Nyquist is under 50 Hz, so an LRA carrier aliases and nothing here is a
    /// waveform. Onset, offset and a relative energy proxy are what it gives.
    static constexpr float    skSamplePeriodMs = 10.0f;
    static constexpr uint32_t skSampleLatency  = 0;

    /// Long enough for the wrist to be quiet again after the last effect, and
    /// long enough to have a baseline worth thresholding against. Samples
    /// arrive several hundred milliseconds after they were taken, so this is a
    /// stretch of *timestamps* rather than of deliveries: the window is what
    /// sorts them out.
    static constexpr uint32_t skSettleMs = 600;
    /// Comfortably past the longest duration any effect's own name claims, so
    /// an effect that runs to its stated 1000 ms still has an offset inside the
    /// window. Like the settle stretch this is a span of *timestamps*: the
    /// window closes when a sample this far past the play has arrived, not when
    /// this much wall time has passed.
    static constexpr uint32_t skCaptureMs = 2500;
    /// What closes the window when the samples never come. Without it a sensor
    /// that stops delivering stops the sweep instead of costing it one row.
    static constexpr uint32_t skCaptureDeadlineMs = 6000;

    /// A cap rather than device awareness: the SDK exposes no free-space query.
    /// One repeat of one effect is 3.1 s at the delivered rate, so about 18 KB
    /// of raw rows; the whole effect space at six repeats is near 14 MB, and at
    /// eight near 18 MB. The first cap was 8 MB, which a six-repeat sweep would
    /// have hit around effect 60 and reported as `cap-reached` rather than
    /// finishing. 1.6 GB was free on the card this was measured against.
    static constexpr uint32_t skRawByteCap = 64u * 1024u * 1024u;

    SDK::Kernel            &mKernel;
    SDK::Sensor::Connection mImu;
    bool                    mSensorConnected = false;

    Phase    mPhase       = Phase::Idle;
    uint32_t mPhaseStart  = 0;
    uint8_t  mEffect      = 0;
    uint8_t  mFirstEffect = 1;
    uint8_t  mLastEffect  = Haptic::kMaxEffect;
    uint8_t  mRepeat      = 0;
    uint8_t  mRepeats     = 1;

    char mRunId[20] = {};
    char mImuDir[64] = {};

    std::unique_ptr<SDK::Interface::IFile> mRawFile;
    uint32_t mRawBytes = 0;
    bool     mCapHit   = false;

    Haptic::SweepWindow mWindow;
    uint32_t            mPlayTick = 0;
    uint32_t            mAcceptedTick = 0;
    bool                mSendOk = false;
    bool                mResultSuccess = false;
};

#endif // HAPTICLAB_SERVICE_HPP
