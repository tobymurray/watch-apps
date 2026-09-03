/**
 ******************************************************************************
 * @file    SquashEngine.hpp
 * @brief   The C ABI of the Rust engine, and the profile file it is kept in.
 ******************************************************************************
 */

#ifndef SQUASH_ENGINE_HPP
#define SQUASH_ENGINE_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

/// One session's measurements, laid out by `squash_engine`'s Rust side.
///
/// Field order and size are asserted on both sides and hashed into
/// squash_engine_abi_fingerprint(), so a struct that drifts fails loudly rather
/// than reading one field as another.
extern "C" struct SquashSessionRecord {
    uint32_t startedUtc;
    uint32_t activeS;
    uint32_t hrCoveredS;
    uint32_t rallyCount;
    uint32_t rallyS;
    uint32_t restS;
    uint32_t offCourtS;
    float    hrMean;
    float    hrMax;
    float    recoveryShortMean;
    float    recoveryLongMean;
    uint16_t recoveryShortN;
    uint16_t recoveryLongN;
    uint8_t  hrSource;   ///< 0 unknown, 1 optical, 2 external
    uint8_t  segmented;  ///< 0 when no calibration existed
    uint16_t discardedWindows;
    uint32_t hrExternalReadings; ///< trusted readings that came from a strap
};

/// How one measurement sits against the wearer's own history.
extern "C" struct SquashComparison {
    float    median;
    float    mad;
    float    z;
    uint16_t sessions;
    uint8_t  hasZ;
    uint8_t  reserved;
};

static_assert(sizeof(SquashSessionRecord) == 56, "must match the Rust side's layout");
static_assert(sizeof(SquashComparison) == 16, "must match the Rust side's layout");

extern "C" {

void     squash_engine_begin(void);
void     squash_engine_on_imu(uint32_t tMs, const int16_t* axes);
void     squash_engine_on_hr(uint32_t tMs, float bpm, uint8_t trust, uint8_t source);
void     squash_engine_finish(uint32_t startedUtc, uint32_t activeS, SquashSessionRecord* out);
uint32_t squash_engine_calibration(void);
uint32_t squash_engine_abi_fingerprint(void);

uint32_t squash_profile_load(const uint8_t* bytes, uint32_t len);
void     squash_profile_record(const SquashSessionRecord* record);
int32_t  squash_profile_write(uint8_t* out, uint32_t cap);
uint32_t squash_profile_sessions(void);
uint32_t squash_profile_compare(uint8_t metric, float value, SquashComparison* out);

} // extern "C"

/// What squash_engine_abi_fingerprint() returns for the layout above.
///
/// Checked once at start-up rather than trusted: the two struct definitions are
/// written twice, in two languages, and nothing but this notices when one of
/// them moves. `squash_engine`'s own test prints the value.
static constexpr uint32_t kAbiFingerprint = 524638087u;

/// No recording has set any threshold, so nothing is segmented or measured.
static constexpr uint32_t kCalibrationAbsent = 0u;

/// Return codes of squash_profile_compare().
static constexpr uint32_t kCompareOk            = 0u;
static constexpr uint32_t kCompareWarmingUp     = 1u;
static constexpr uint32_t kCompareNotCalibrated = 2u;
static constexpr uint32_t kCompareNoData        = 3u;

/// Which baseline squash_profile_compare() places a value against. Must match
/// the Rust side's METRICS array, which the fingerprint covers by length only —
/// so reordering either list without the other is the one drift this cannot
/// catch, and both are written in the same order for that reason.
enum class SquashMetric : uint8_t {
    HR_MEAN = 0,
    HR_MAX,
    RALLY_COUNT,
    RALLY_RATE,
    WORK_REST_RATIO,
    RECOVERY_SHORT,
    RECOVERY_LONG,
};

/**
 * @class SquashProfileStore
 * @brief Reads and writes the wearer's profile, and cannot stop the app starting.
 *
 * The file is the app's own, distinct from `settings.json`, which the watch
 * rewrites whole whenever a setting changes, and from `input.json`, which is
 * written from the phone. Keeping it separate makes "this is ours, and it is
 * the only thing that writes it" a property of the filename.
 *
 * Committed through a temporary and a rename, which is the pattern the
 * firmware's own settings persistence uses: a file another session will read
 * cannot be left half-written by a battery pull mid-write.
 */
class SquashProfileStore {
public:
    /// What a load found. Mirrors the Rust side's `profile::Load`.
    enum class Load : uint8_t {
        OK = 0,         ///< A file of this schema, read completely.
        ABSENT,         ///< No file, or an empty one. The first-run case.
        UNKNOWN_SCHEMA, ///< A schema this build does not read; nothing was taken.
        MALFORMED,      ///< Did not parse as far as the session list.
        TRUNCATED,      ///< More sessions than fit; the newest were kept.
        READ_FAILED,    ///< The file exists and could not be read.
    };

    SquashProfileStore(const SDK::Kernel& kernel, const char* path);

    SquashProfileStore(const SquashProfileStore&)            = delete;
    SquashProfileStore& operator=(const SquashProfileStore&) = delete;

    /// Read the file into the engine. Never fails: every way of being wrong
    /// leaves an empty profile and a reason.
    Load load();

    /// Write the engine's profile back, through a temporary and a rename.
    /// @return false if the file on storage is not the profile just written.
    bool save();

private:
    /// Bytes the buffer holds, matching `effortkit::profile::MAX_BYTES`.
    static constexpr size_t skMaxBytes = 8192;

    const SDK::Kernel& mKernel;
    const char*        mPath;
    uint8_t            mBuf[skMaxBytes]{};
};

#endif // SQUASH_ENGINE_HPP
