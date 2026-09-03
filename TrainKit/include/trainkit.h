#ifndef TRAINKIT_H
#define TRAINKIT_H

/* The boundary between an app's C++ Service and TrainKit's Rust.
 *
 * Rust owns the arithmetic: when a recovery measurement counts, what the shared
 * log's JSON looks like, and how it is bounded. C++ owns the clock, the sensor
 * and the filesystem, none of which this crate knows anything about.
 *
 * Every number restated below is asserted in tests/abi.rs against what the Rust
 * compiler actually produced, and the fingerprint catches a stale
 * libtrainkit.a at start-up rather than at the first wrong file. */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -- The shared log ------------------------------------------------------- */

/* Where a store file goes, relative to the app's own directory. This is the
   SDK's own convention -- SDK::Calibration::StrideLut::kDefaultPath is
   "../SharedData/stride.json" -- and the name is per-app so two apps never
   write the same file. A reader wanting every sport looks for the pattern. */
#define TRAINKIT_SHARED_DIR "../SharedData"
#define TRAINKIT_STORE_SUFFIX "_sessions.json"

#define TRAINKIT_MAX_ZONE_BUCKETS 9u
#define TRAINKIT_MAX_ZONES        8u
#define TRAINKIT_MAX_RECOVERIES   2u
#define TRAINKIT_CURVE_POINTS     7u

/* -- Why effort ceased ---------------------------------------------------- */

#define TRAINKIT_TRIGGER_PAUSE 1u
/* Reserved. A lap does not mean the wearer stopped pedalling, and the sensor is
   released inside stopTrack(), so nothing produces either of these today. */
#define TRAINKIT_TRIGGER_LAP  2u
#define TRAINKIT_TRIGGER_STOP 3u

/* -- Why a window produced nothing ---------------------------------------- */

#define TRAINKIT_DISCARD_NONE                0u
#define TRAINKIT_DISCARD_NO_MAX_HR           1u
#define TRAINKIT_DISCARD_TOO_SHORT           2u
#define TRAINKIT_DISCARD_TOO_EASY            3u
#define TRAINKIT_DISCARD_ALREADY_FALLING     4u
#define TRAINKIT_DISCARD_NO_BASELINE_HISTORY 5u
#define TRAINKIT_DISCARD_NO_BASELINE         6u
#define TRAINKIT_DISCARD_DROPOUT             7u
#define TRAINKIT_DISCARD_NO_ENDPOINT         8u
#define TRAINKIT_DISCARD_EFFORT_RESUMED      9u
#define TRAINKIT_DISCARD_RIDE_ENDED          10u

/* -- What one second did -------------------------------------------------- */

#define TRAINKIT_STEP_NOTHING   0u
#define TRAINKIT_STEP_COMPLETED 1u
#define TRAINKIT_STEP_DISCARDED 2u

/* -- What a load made of the file ----------------------------------------- */

#define TRAINKIT_LOAD_OK         0
#define TRAINKIT_LOAD_NEWER      1  /* a writer that knows more; do not write */
#define TRAINKIT_LOAD_UNREADABLE 2  /* keep it as evidence and start fresh */

/* One measurement, with the context that makes it comparable to another. */
typedef struct {
    uint32_t at_active_s;  /* active seconds into the ride effort ceased at */
    uint8_t  hr0;          /* bpm at cessation */
    uint8_t  hr_end;       /* bpm at the end of the window */
    uint8_t  window_s;     /* seconds actually spanned; not always 60 */
    uint8_t  trusted_s;    /* seconds inside it the sensor was believed */
    uint8_t  hr0_pct_max;  /* hr0 as a percentage of the watch's maximum */
    uint8_t  trigger;      /* one of TRAINKIT_TRIGGER_* */
    /* bpm at 0, 10, ... 60 s from hr0; 0 where no trusted reading landed. The
       input any later curve fit would need, so a derivation can change without
       orphaning the history. */
    uint8_t  curve[TRAINKIT_CURVE_POINTS];
    uint8_t  reserved[3];
} trainkit_recovery;

/* One session, as the shared log records it. */
typedef struct {
    uint32_t start_utc;   /* the entry's identity */
    uint32_t active_s;    /* unpaused seconds */
    uint32_t elapsed_s;   /* wall clock, start to stop */
    uint16_t kcal;        /* kcal, active */
    uint16_t work_kj;     /* kJ; 0 = nobody said, and the file omits it */
    uint16_t zone_s[TRAINKIT_MAX_ZONE_BUCKETS];  /* [0] is below zone 1 */
    uint8_t  zone_floor[TRAINKIT_MAX_ZONES];     /* bpm; [i] floors zone i+1 */
    uint8_t  hr_avg;              /* bpm */
    uint8_t  hr_max;              /* bpm */
    uint8_t  hr_max_setting;      /* bpm the watch calls the maximum; 0 = none */
    uint8_t  weight_kg;           /* kg the calorie model used */
    uint8_t  zone_count;          /* 0 = no zones set */
    uint8_t  recovery_count;      /* filled entries of `recoveries` */
    uint8_t  recoveries_dropped;  /* measured, but did not fit */
    uint8_t  reserved[3];
    trainkit_recovery recoveries[TRAINKIT_MAX_RECOVERIES];
} trainkit_session;

/* Storage for a detector, which is opaque: nothing in it is a C++ Service's
   business. Sized in uint64_t so the alignment is right by construction, and
   checked at run time against trainkit_detector_bytes(). */
#define TRAINKIT_DETECTOR_WORDS 24u
typedef struct {
    uint64_t opaque[TRAINKIT_DETECTOR_WORDS];
} trainkit_detector;

uint32_t trainkit_abi_fingerprint(void);
uint32_t trainkit_max_store_bytes(void);
uint32_t trainkit_detector_bytes(void);
uint32_t trainkit_detector_align(void);
/* A log is allocated at the size this reports rather than reserved in a struct:
   it is only alive while a ride is being recorded. */
uint32_t trainkit_history_bytes(void);
uint32_t trainkit_history_align(void);

/* -- Recovery -------------------------------------------------------------- */

/* Begin a ride. `max_hr` is the top of the watch's own threshold ladder, which
   is its maximum heart rate and not a zone floor; 0 when it has none. */
void    trainkit_recovery_start(trainkit_detector *d, uint8_t max_hr);
/* One second of the ride, keyed on UTC rather than on the number of calls: a
   tick the Service was too busy to serve is a second that still went past. */
uint8_t trainkit_recovery_second(trainkit_detector *d, int64_t utc, float bpm,
                                 uint8_t trusted, uint32_t active_s);
uint8_t trainkit_recovery_cease(trainkit_detector *d, uint8_t trigger);
uint8_t trainkit_recovery_resume(trainkit_detector *d);
uint8_t trainkit_recovery_end(trainkit_detector *d);
/* Hand back a completed measurement, once. */
uint8_t trainkit_recovery_take(trainkit_detector *d, trainkit_recovery *out);
uint8_t trainkit_recovery_last_discard(const trainkit_detector *d);
/* The name of a TRAINKIT_DISCARD_* value, NUL-terminated and static. Here so a
   reason and its spelling cannot drift apart. */
const char *trainkit_discard_name(uint8_t reason);

/* -- The shared log -------------------------------------------------------- */

void    trainkit_history_init(void *h, const char *app, const char *sport);
int32_t trainkit_history_load(void *h, const uint8_t *buf, uint32_t len);
void    trainkit_history_add(void *h, const trainkit_session *s);
/* Serialise, dropping the oldest entries until the result fits `cap`; returns
   the bytes written, or -1 when not even one session fits. */
int32_t trainkit_history_save(void *h, uint8_t *buf, uint32_t cap);

/* -- Load arithmetic ------------------------------------------------------- */

/* Edwards' TRIMP in minute-weights, or -1 for a ladder the weights are not
   defined over. See TrainKit/src/load.rs for the citation. */
int32_t  trainkit_edwards_trimp(const trainkit_session *s);
uint16_t trainkit_avg_power_w(uint16_t work_kj, uint32_t active_s);
uint32_t trainkit_efficiency_factor_x1000(uint16_t work_kj, uint32_t active_s,
                                          uint8_t hr_avg);

/* Provided by the host so a Rust panic is visible rather than silent. */
void trainkit_host_panic(const uint8_t *msg, uint32_t len);

#ifdef __cplusplus
} // extern "C"

namespace trainkit_abi {

constexpr uint32_t kFnvOffsetBasis = 0x811C9DC5u;
constexpr uint32_t kFnvPrime       = 0x01000193u;

constexpr uint32_t fnv1a(uint32_t hash, size_t byte)
{
    return (hash ^ (static_cast<uint32_t>(byte) & 0xFFu)) * kFnvPrime;
}

/// The same walk in the same order as `abi_fingerprint()` in lib.rs. A Service
/// refuses to start on a disagreement, which is the direction this should fail
/// in: a stale libtrainkit.a is otherwise silent until it writes a file whose
/// numbers are in the wrong fields.
constexpr uint32_t fingerprint()
{
    uint32_t h = kFnvOffsetBasis;
    h = fnv1a(h, sizeof(trainkit_recovery));
    h = fnv1a(h, alignof(trainkit_recovery));
    h = fnv1a(h, offsetof(trainkit_recovery, at_active_s));
    h = fnv1a(h, offsetof(trainkit_recovery, hr0));
    h = fnv1a(h, offsetof(trainkit_recovery, hr_end));
    h = fnv1a(h, offsetof(trainkit_recovery, window_s));
    h = fnv1a(h, offsetof(trainkit_recovery, trusted_s));
    h = fnv1a(h, offsetof(trainkit_recovery, hr0_pct_max));
    h = fnv1a(h, offsetof(trainkit_recovery, trigger));
    h = fnv1a(h, offsetof(trainkit_recovery, curve));
    h = fnv1a(h, sizeof(trainkit_session));
    h = fnv1a(h, alignof(trainkit_session));
    h = fnv1a(h, offsetof(trainkit_session, start_utc));
    h = fnv1a(h, offsetof(trainkit_session, active_s));
    h = fnv1a(h, offsetof(trainkit_session, elapsed_s));
    h = fnv1a(h, offsetof(trainkit_session, kcal));
    h = fnv1a(h, offsetof(trainkit_session, work_kj));
    h = fnv1a(h, offsetof(trainkit_session, zone_s));
    h = fnv1a(h, offsetof(trainkit_session, zone_floor));
    h = fnv1a(h, offsetof(trainkit_session, hr_avg));
    h = fnv1a(h, offsetof(trainkit_session, hr_max));
    h = fnv1a(h, offsetof(trainkit_session, hr_max_setting));
    h = fnv1a(h, offsetof(trainkit_session, weight_kg));
    h = fnv1a(h, offsetof(trainkit_session, zone_count));
    h = fnv1a(h, offsetof(trainkit_session, recovery_count));
    h = fnv1a(h, offsetof(trainkit_session, recoveries_dropped));
    return fnv1a(h, offsetof(trainkit_session, recoveries));
}

} // namespace trainkit_abi

static_assert(sizeof(trainkit_recovery) == 20, "trainkit_recovery size changed");
static_assert(alignof(trainkit_recovery) == 4, "trainkit_recovery alignment changed");
static_assert(offsetof(trainkit_recovery, at_active_s) == 0, "at_active_s moved");
static_assert(offsetof(trainkit_recovery, hr0) == 4, "hr0 moved");
static_assert(offsetof(trainkit_recovery, hr_end) == 5, "hr_end moved");
static_assert(offsetof(trainkit_recovery, window_s) == 6, "window_s moved");
static_assert(offsetof(trainkit_recovery, trusted_s) == 7, "trusted_s moved");
static_assert(offsetof(trainkit_recovery, hr0_pct_max) == 8, "hr0_pct_max moved");
static_assert(offsetof(trainkit_recovery, trigger) == 9, "trigger moved");
static_assert(offsetof(trainkit_recovery, curve) == 10, "curve moved");
static_assert(offsetof(trainkit_recovery, reserved) == 17, "reserved moved");

static_assert(sizeof(trainkit_session) == 92, "trainkit_session size changed");
static_assert(alignof(trainkit_session) == 4, "trainkit_session alignment changed");
static_assert(offsetof(trainkit_session, start_utc) == 0, "start_utc moved");
static_assert(offsetof(trainkit_session, active_s) == 4, "active_s moved");
static_assert(offsetof(trainkit_session, elapsed_s) == 8, "elapsed_s moved");
static_assert(offsetof(trainkit_session, kcal) == 12, "kcal moved");
static_assert(offsetof(trainkit_session, work_kj) == 14, "work_kj moved");
static_assert(offsetof(trainkit_session, zone_s) == 16, "zone_s moved");
static_assert(offsetof(trainkit_session, zone_floor) == 34, "zone_floor moved");
static_assert(offsetof(trainkit_session, hr_avg) == 42, "hr_avg moved");
static_assert(offsetof(trainkit_session, hr_max) == 43, "hr_max moved");
static_assert(offsetof(trainkit_session, hr_max_setting) == 44, "hr_max_setting moved");
static_assert(offsetof(trainkit_session, weight_kg) == 45, "weight_kg moved");
static_assert(offsetof(trainkit_session, zone_count) == 46, "zone_count moved");
static_assert(offsetof(trainkit_session, recovery_count) == 47, "recovery_count moved");
static_assert(offsetof(trainkit_session, recoveries_dropped) == 48, "recoveries_dropped moved");
static_assert(offsetof(trainkit_session, reserved) == 49, "reserved moved");
static_assert(offsetof(trainkit_session, recoveries) == 52, "recoveries moved");
#endif

#endif // TRAINKIT_H
