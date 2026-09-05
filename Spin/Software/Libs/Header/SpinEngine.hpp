#ifndef SPIN_ENGINE_HPP
#define SPIN_ENGINE_HPP

/* The boundary between Spin's C++ Service and EffortKit's Rust.
 *
 * Rust owns the arithmetic: when a recovery measurement counts, what the
 * shared log's JSON looks like, and how it is bounded. C++ owns the clock, the
 * sensor and the filesystem, none of which the crate knows anything about.
 *
 * The two structs below mirror `effortkit::record`'s, which are `#[repr(C)]`
 * and crossed field for field. Both sides hash their own layout into a
 * fingerprint, so a stale `libspin_engine.a` is a refused start-up rather than
 * a file whose numbers are in the wrong fields. */

#include <cstddef>
#include <cstdint>

/* Where a store file goes, relative to the app's own directory. This is the
   SDK's own convention -- SDK::Calibration::StrideLut::kDefaultPath is
   "../SharedData/stride.json" -- and the name is per-app so two apps never
   write the same file. A reader wanting every sport globs the pattern. */
#define SPIN_SHARED_DIR "../SharedData"
#define SPIN_STORE_SUFFIX "_sessions.json"

#define SPIN_MAX_ZONE_BUCKETS 9u
#define SPIN_MAX_ZONES        8u
#define SPIN_MAX_RECOVERIES   2u
#define SPIN_CURVE_POINTS     7u

/* -- What one second did -------------------------------------------------- */

#define SPIN_STEP_NOTHING   0u
#define SPIN_STEP_COMPLETED 1u
#define SPIN_STEP_DISCARDED 2u

/* -- What a load made of the file ----------------------------------------- */

#define SPIN_LOAD_OK         0
#define SPIN_LOAD_NEWER      1  /* a writer that knows more; do not write */
#define SPIN_LOAD_UNREADABLE 2  /* keep it as evidence and start fresh */

/* -- Which cessation opened a window -------------------------------------- */

#define SPIN_KIND_PAUSE 1u
/* Reserved. A lap does not mean the wearer stopped pedalling, and the sensor
   is released inside stopTrack(), so nothing produces either of these. */
#define SPIN_KIND_LAP   2u
#define SPIN_KIND_STOP  3u

/* SDK::SensorDataParser::HeartRateEx::Source, carried through unchanged. */
#define SPIN_HR_SOURCE_NONE     0u
#define SPIN_HR_SOURCE_OPTICAL  1u
#define SPIN_HR_SOURCE_EXTERNAL 2u

extern "C" {

/* One measurement, with the context that makes it comparable to another. */
struct SpinRecovery {
    uint32_t at_active_s;  /* active seconds into the ride effort ceased at */
    uint8_t  hr0;          /* bpm at cessation */
    uint8_t  hr_end;       /* bpm at the end of the window */
    uint8_t  window_s;     /* seconds actually spanned; not always 60 */
    uint8_t  trusted_s;    /* seconds inside it that met the confidence floor */
    uint8_t  hr0_pct_max;  /* hr0 as a percentage of the watch's maximum */
    uint8_t  kind;         /* one of SPIN_KIND_* */
    /* bpm at 0, 10, ... 60 s from hr0; 0 where no trusted reading landed. The
       input any later curve fit would need, so a derivation can change without
       orphaning the history. */
    uint8_t  curve[SPIN_CURVE_POINTS];
    /* Which sensor every reading came from; constant across the window by
       construction, because a switch discards the measurement. */
    uint8_t  source;
    uint8_t  reserved[2];
};

/* Why windows produced nothing, by reason. Part of the record rather than a
   diagnostic: a session that measured nothing should be able to say why a year
   later, and a text log the field test tells you to delete cannot. */
struct SpinDiscardCounts {
    uint16_t not_calibrated;
    uint16_t not_measurable;
    uint16_t no_max_hr;
    uint16_t too_short;
    uint16_t too_easy;
    uint16_t already_falling;
    uint16_t no_baseline_history;
    uint16_t no_baseline;
    uint16_t dropout;
    uint16_t no_endpoint;
    uint16_t effort_resumed;
    uint16_t session_ended;
    uint16_t source_changed;
    uint16_t source_not_accepted;
};

/* One session, as the shared log records it. */
struct SpinSessionRecord {
    uint32_t start_utc;   /* the entry's identity */
    uint32_t active_s;    /* unpaused seconds */
    uint32_t elapsed_s;   /* wall clock, start to stop */
    uint16_t kcal;        /* kcal, active */
    uint16_t work_kj;     /* kJ; 0 = nobody said, and the file omits it */
    uint16_t zone_s[SPIN_MAX_ZONE_BUCKETS];  /* [0] is below zone 1 */
    uint8_t  zone_floor[SPIN_MAX_ZONES];     /* bpm; [i] floors zone i+1 */
    uint8_t  hr_avg;              /* bpm */
    uint8_t  hr_max;              /* bpm */
    uint8_t  hr_max_setting;      /* bpm the watch calls the maximum; 0 = none */
    uint8_t  weight_kg;           /* kg the calorie model used */
    uint8_t  zone_count;          /* 0 = no zones set */
    uint8_t  recovery_count;      /* filled entries of `recoveries` */
    uint8_t  recoveries_dropped;  /* measured, but did not fit */
    uint8_t  reserved[3];
    SpinRecovery recoveries[SPIN_MAX_RECOVERIES];
    SpinDiscardCounts discarded;
};

uint32_t spin_engine_abi_fingerprint(void);

/* -- Recovery -------------------------------------------------------------- */

/* Begin a ride. `max_hr` is the top of the watch's own threshold ladder, which
   is its maximum heart rate and not a zone floor; 0 when it has none. */
void    spin_engine_start(uint8_t max_hr);
/* One second of the ride, keyed on UTC rather than on the number of calls: a
   tick the Service was too busy to serve is a second that still went past.
   `trust` is the kernel's own 0-3 confidence, passed through rather than
   collapsed, so the calibration decides how far the sensor is believed. */
uint8_t spin_engine_second(int64_t utc, float bpm, uint8_t trust,
                           uint8_t source, uint32_t active_s);
uint8_t spin_engine_cease(void);
uint8_t spin_engine_resume(void);
uint8_t spin_engine_end(void);
/* Hand back a completed measurement, once. */
uint8_t spin_engine_take(SpinRecovery* out);
uint8_t spin_engine_last_discard(void);
void    spin_engine_discarded(SpinDiscardCounts* out);
/* The name of a discard reason, NUL-terminated and static. Here so a reason
   and its spelling cannot drift apart. */
const char* spin_engine_discard_name(uint8_t reason);

/* -- The shared log -------------------------------------------------------- */

void    spin_history_init(const uint8_t* app, uint32_t appLen,
                          const uint8_t* sport, uint32_t sportLen);
int32_t spin_history_load(const uint8_t* buf, uint32_t len);
void    spin_history_add(const SpinSessionRecord* s);
/* Serialise, dropping the oldest entries until the result fits `cap`; returns
   the bytes written, or -1 when not even one session fits. */
int32_t spin_history_save(uint8_t* buf, uint32_t cap);
uint32_t spin_history_max_bytes(void);

/* -- Load arithmetic ------------------------------------------------------- */

/* Edwards' TRIMP in minute-weights, or -1 for a ladder the weights are not
   defined over. See EffortKit/src/load.rs for the citation. */
int32_t spin_edwards_trimp(const SpinSessionRecord* s);

/* Provided by the host so a Rust panic is visible rather than silent. */
void spin_engine_host_panic(const uint8_t* msg, uint32_t len);

} // extern "C"

namespace spin_abi
{

constexpr uint32_t kFnvOffsetBasis = 0x811C9DC5u;
constexpr uint32_t kFnvPrime       = 0x01000193u;

constexpr uint32_t fnv1a(uint32_t hash, size_t byte)
{
    return (hash ^ (static_cast<uint32_t>(byte) & 0xFFu)) * kFnvPrime;
}

/// The same walk in the same order as spin_engine_abi_fingerprint(), but over
/// the offsets *this* compiler produced.
///
/// Computed rather than copied. A literal transcribed from the Rust side's test
/// catches a struct that drifts in Rust and misses one that drifts here, which
/// is half a check: the two definitions are written twice, in two languages,
/// and only walking both notices when either moves.
constexpr uint32_t fingerprint()
{
    uint32_t h = fnv1a(kFnvOffsetBasis, sizeof(SpinRecovery));
    h = fnv1a(h, alignof(SpinRecovery));
    h = fnv1a(h, offsetof(SpinRecovery, at_active_s));
    h = fnv1a(h, offsetof(SpinRecovery, hr0));
    h = fnv1a(h, offsetof(SpinRecovery, hr_end));
    h = fnv1a(h, offsetof(SpinRecovery, window_s));
    h = fnv1a(h, offsetof(SpinRecovery, trusted_s));
    h = fnv1a(h, offsetof(SpinRecovery, hr0_pct_max));
    h = fnv1a(h, offsetof(SpinRecovery, kind));
    h = fnv1a(h, offsetof(SpinRecovery, curve));
    h = fnv1a(h, offsetof(SpinRecovery, source));
    h = fnv1a(h, sizeof(SpinSessionRecord));
    h = fnv1a(h, alignof(SpinSessionRecord));
    h = fnv1a(h, offsetof(SpinSessionRecord, start_utc));
    h = fnv1a(h, offsetof(SpinSessionRecord, active_s));
    h = fnv1a(h, offsetof(SpinSessionRecord, elapsed_s));
    h = fnv1a(h, offsetof(SpinSessionRecord, kcal));
    h = fnv1a(h, offsetof(SpinSessionRecord, work_kj));
    h = fnv1a(h, offsetof(SpinSessionRecord, zone_s));
    h = fnv1a(h, offsetof(SpinSessionRecord, zone_floor));
    h = fnv1a(h, offsetof(SpinSessionRecord, hr_avg));
    h = fnv1a(h, offsetof(SpinSessionRecord, hr_max));
    h = fnv1a(h, offsetof(SpinSessionRecord, hr_max_setting));
    h = fnv1a(h, offsetof(SpinSessionRecord, weight_kg));
    h = fnv1a(h, offsetof(SpinSessionRecord, zone_count));
    h = fnv1a(h, offsetof(SpinSessionRecord, recovery_count));
    h = fnv1a(h, offsetof(SpinSessionRecord, recoveries_dropped));
    h = fnv1a(h, offsetof(SpinSessionRecord, recoveries));
    h = fnv1a(h, offsetof(SpinSessionRecord, discarded));
    return fnv1a(h, sizeof(SpinDiscardCounts));
}

} // namespace spin_abi

static_assert(sizeof(SpinRecovery) == 20, "SpinRecovery size changed");
static_assert(alignof(SpinRecovery) == 4, "SpinRecovery alignment changed");
static_assert(offsetof(SpinRecovery, curve) == 10, "curve moved");
static_assert(offsetof(SpinRecovery, source) == 17, "source moved");

static_assert(sizeof(SpinSessionRecord) == 120, "SpinSessionRecord size changed");
static_assert(alignof(SpinSessionRecord) == 4, "SpinSessionRecord alignment changed");
static_assert(offsetof(SpinSessionRecord, zone_s) == 16, "zone_s moved");
static_assert(offsetof(SpinSessionRecord, zone_floor) == 34, "zone_floor moved");
static_assert(offsetof(SpinSessionRecord, recoveries) == 52, "recoveries moved");
static_assert(offsetof(SpinSessionRecord, discarded) == 92, "discarded moved");
static_assert(sizeof(SpinDiscardCounts) == 28, "SpinDiscardCounts size changed");

#endif // SPIN_ENGINE_HPP
