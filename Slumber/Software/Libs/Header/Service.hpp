#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"

#include "ActivityCount.hpp"
#include "Commands.hpp"
#include "DayLog.hpp"
#include "EpochHistory.hpp"
#include "EpochRow.hpp"
#include "LastNight.hpp"
#include "Lifecycle.hpp"
#include "NightLog.hpp"
#include "SlumberCard.hpp"
#include "Tilt.hpp"

/// Records a row every 30 s for the device's whole life, and the wearer's
/// marks beside it, and reports the last night on the card;
/// `Slumber/README.md` says what each is for.
class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    virtual ~Service();

    void run();

private:
    /// Counted between two rows and reset wholesale.
    struct Accum {
        uint16_t accBatches  = 0;
        int32_t  accMaxGapMs = -1;
        uint16_t motion      = 0;
        uint16_t sigMotion   = 0;
        uint32_t hrSumX10    = 0;
        uint16_t hrMinX10    = 0;
        uint16_t hrMaxX10    = 0;
        uint16_t hrN         = 0;
        float    trustSum    = 0.0f;
        uint16_t trustN      = 0;
        float    tempSum     = 0.0f;
        uint16_t tempN       = 0;
        uint16_t wakes       = 0;
        uint16_t msgs        = 0;
        int32_t  mark        = -1;
    };

    void connectSensors();
    void disconnectSensors();
    void logSensors();
    void onSensorData(uint16_t handle, SDK::Sensor::DataBatch &batch);

    void closeEpoch(uint32_t now, uint32_t lateMs, uint32_t slotsLost);
    void onMark(const CustomMessage::Mark &mark);
    void publishCard();
    void restoreHistory(uint32_t utc);
    void fillNight(SlumberCard::Card &card, uint32_t utc);
    void logNights(size_t n, int32_t offsetMin);
    void publishStatus();
    uint32_t upMinutes() const;

    static constexpr uint32_t kEpochMs = 30000;
    static constexpr uint32_t kCardEveryEpochs = SlumberCard::kHeartbeatS * 1000u / kEpochMs;

    SDK::Kernel        &mKernel;
    Slumber::Lifecycle  mLog;
    Slumber::DayLog     mDays;
    Slumber::EpochHistory mHistory;
    Slumber::NightLog   mNights;
    /// Every period holds at least 30 min of rest, so 36 h holds fewer than 72.
    SleepPeriods::Period  mPeriods[72];

    Slumber::ActivityCount mCount;
    Slumber::Tilt          mTilt;
    Accum                  mAcc;

    SDK::Sensor::Connection mAccel;
    SDK::Sensor::Connection mHr;
    SDK::Sensor::Connection mSteps;
    SDK::Sensor::Connection mMotion;
    SDK::Sensor::Connection mTemp;
    SDK::Sensor::Connection mBattLevel;
    SDK::Sensor::Connection mBattMetrics;
    SDK::Sensor::Connection mBattCharge;

    // Carried forward between rows: these channels publish on change or on
    // their own period, not on the epoch grid.
    uint32_t mAccLastTs      = 0;
    bool     mAccLastTsValid = false;
    int64_t  mStepTotal      = -1;
    int64_t  mStepAtEpoch    = -1;
    int32_t  mBattPctX10     = -1;
    int32_t  mBattMv         = -1;
    int32_t  mBattMaX10      = -1;
    int32_t  mBattAvgMaX10   = -1;
    int32_t  mBattMah        = -1;
    int32_t  mCharging       = -1;

    uint32_t mStartMs        = 0;
    uint32_t mStartUtc       = 0;  ///< s; 0 = clock unknown at start
    uint32_t mLaunchNo       = 0;
    uint32_t mCardSeq        = 0;
    uint32_t mEpochs         = 0;
    uint32_t mEpochOpenedAt  = 0;
    uint32_t mNextEpochAt    = 0;
    uint32_t mWriteFails     = 0;
    bool     mCardOk         = false;
    bool     mGuiAttached    = false;
    bool     mLastAccOk      = false;
    bool     mLastHrOk       = false;
    bool     mHistoryRestored = false;
    uint32_t mReportedStartUtc = 0;
    SlumberCard::State mReportedState = SlumberCard::State::None;
    int64_t  mLastRowUtc     = -1;
    uint32_t mLastRowUptime  = 0;

    uint8_t  mLastMark       = CustomMessage::kNoMark;
    uint32_t mLastMarkMs     = 0;
};

#endif // SERVICE_HPP
