#ifndef SQUASH_GUI_H
#define SQUASH_GUI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One render-time struct describes any screen this app draws, so the renderer
   is a pure function of it. */

#define SQUASH_GUI_SCREEN_READY     0u /* pre-session: armed, caps, free space */
#define SQUASH_GUI_SCREEN_PROFILE   1u /* recording: the instrument */
#define SQUASH_GUI_SCREEN_LABEL     2u /* picking the state being recorded */
#define SQUASH_GUI_SCREEN_PAUSED    3u /* clock held, save or discard */
#define SQUASH_GUI_SCREEN_SAVED     4u
#define SQUASH_GUI_SCREEN_DISCARDED 5u

/* These values ARE the `kind` column of `imu_<stamp>_events.csv`, so they are
   a wire format, not a display detail. Kind 0 stayed MANUAL: every recording
   made before on-watch labelling reads back as unlabelled markers rather than
   as rallies. */
#define SQUASH_GUI_LABEL_NONE      0u
#define SQUASH_GUI_LABEL_RALLY     1u
#define SQUASH_GUI_LABEL_REST      2u
#define SQUASH_GUI_LABEL_OFF_COURT 3u
#define SQUASH_GUI_LABEL_WARMUP    4u
#define SQUASH_GUI_LABEL_DRILL     5u
#define SQUASH_GUI_LABEL_IDLE      6u
#define SQUASH_GUI_LABEL_GAME      7u
#define SQUASH_GUI_LABEL_COUNT     8u

/* ImuCsvRecorder::Stop, carried through unchanged. */
#define SQUASH_GUI_REC_NONE           0u
#define SQUASH_GUI_REC_REQUESTED      1u
#define SQUASH_GUI_REC_SIZE_LIMIT     2u
#define SQUASH_GUI_REC_DURATION_LIMIT 3u
#define SQUASH_GUI_REC_SINK_ERROR     4u

/* SDK::SensorDataParser::HeartRateEx::Source, carried through unchanged. */
#define SQUASH_GUI_HR_NONE     0u
#define SQUASH_GUI_HR_OPTICAL  1u
#define SQUASH_GUI_HR_EXTERNAL 2u

/* Widest fields first, so nothing pays for padding. Not load-bearing: the
   fingerprint below checks whatever the compiler actually produced. */
typedef struct {
    uint32_t elapsed_s;   /* active session seconds; the clock the wearer started */
    /* Seconds of IMU actually written. Diverges from elapsed_s the moment a cap
       trips, which is the divergence the PROFILE screen exists to show. */
    uint32_t rec_s;
    uint32_t rec_cap_s;   /* the duration cap in force, from input.json */
    uint32_t rec_kb;      /* KiB written so far */
    uint32_t rec_cap_kb;  /* the size cap in force, KiB */
    uint32_t gyro_mag;    /* mean gyroscope vector magnitude last epoch, raw LSB */
    uint32_t accel_var_k; /* accelerometer magnitude variance last epoch, LSB^2/1000 */
    uint32_t rally_s;     /* seconds the wearer said they were in a rally */
    uint32_t game_s;      /* seconds inside a game; what this app's button marks */
    uint32_t rest_s;      /* seconds resting on court; in threes, sitting a rally out */
    uint32_t off_court_s; /* seconds off court entirely */
    uint16_t markers;     /* markers written, label changes included */
    uint16_t hr_bpm;      /* current bpm, 0 = nothing believable right now */
    uint16_t label_s;     /* seconds held in the current label */
    uint8_t  sat_accel_pct; /* % of last epoch's samples with an accel axis railed */
    uint8_t  sat_gyro_pct;  /* % of last epoch's samples with a gyro axis railed */
    uint8_t  screen;      /* one of the SQUASH_GUI_SCREEN_* values above */
    uint8_t  label;       /* one of the SQUASH_GUI_LABEL_* values above */
    uint8_t  label_pick;  /* LABEL screen only: the entry the wearer is sitting on */
    uint8_t  hr_trust;    /* the kernel's 0..3 confidence in hr_bpm */
    uint8_t  hr_source;   /* one of the SQUASH_GUI_HR_* values above */
    uint8_t  recording;   /* 1 = samples are being written this second */
    uint8_t  rec_stop;    /* one of the SQUASH_GUI_REC_* values above */
    uint8_t  armed;       /* 1 = recordImu was true, so starting will record */
    uint8_t  saved_ok;    /* SAVED only: 1 the files are on disk, 0 they are not */
} squash_gui_frame;

uint32_t squash_gui_abi_fingerprint(void);

/* The label model, as pure functions of the current value: Gui.cpp holds one
   uint8_t, so there is no picker state on the C++ side. */
uint8_t squash_gui_label_next(uint8_t pick);     /* L2 on the LABEL screen */
uint8_t squash_gui_label_toggle(uint8_t label);  /* L1 on PROFILE: the hot pair */

/* Defined by Gui.cpp and called by PanicKit's panic handler. */
void panickit_host_panic(const uint8_t *msg, uint32_t len);

void squash_gui_render(uint8_t *buf, uint32_t buf_len,
                       uint16_t width, uint16_t height,
                       const squash_gui_frame *frame);

#ifdef __cplusplus
} // extern "C"

namespace squash_gui_abi {

constexpr uint32_t kFnvOffsetBasis = 0x811C9DC5u;
constexpr uint32_t kFnvPrime       = 0x01000193u;

constexpr uint32_t fnv1a(uint32_t hash, size_t byte)
{
    return (hash ^ (static_cast<uint32_t>(byte) & 0xFFu)) * kFnvPrime;
}

/* Walks this compiler's own layout, in the same order as `abi_fingerprint()`
   in lib.rs. A literal copied from the Rust side would agree with it forever
   and catch nothing; this disagrees the moment either struct drifts. */
constexpr uint32_t fingerprint()
{
    uint32_t h = kFnvOffsetBasis;
    h = fnv1a(h, sizeof(squash_gui_frame));
    h = fnv1a(h, alignof(squash_gui_frame));
    h = fnv1a(h, offsetof(squash_gui_frame, elapsed_s));
    h = fnv1a(h, offsetof(squash_gui_frame, rec_s));
    h = fnv1a(h, offsetof(squash_gui_frame, rec_cap_s));
    h = fnv1a(h, offsetof(squash_gui_frame, rec_kb));
    h = fnv1a(h, offsetof(squash_gui_frame, rec_cap_kb));
    h = fnv1a(h, offsetof(squash_gui_frame, gyro_mag));
    h = fnv1a(h, offsetof(squash_gui_frame, accel_var_k));
    h = fnv1a(h, offsetof(squash_gui_frame, rally_s));
    h = fnv1a(h, offsetof(squash_gui_frame, game_s));
    h = fnv1a(h, offsetof(squash_gui_frame, rest_s));
    h = fnv1a(h, offsetof(squash_gui_frame, off_court_s));
    h = fnv1a(h, offsetof(squash_gui_frame, markers));
    h = fnv1a(h, offsetof(squash_gui_frame, hr_bpm));
    h = fnv1a(h, offsetof(squash_gui_frame, label_s));
    h = fnv1a(h, offsetof(squash_gui_frame, sat_accel_pct));
    h = fnv1a(h, offsetof(squash_gui_frame, sat_gyro_pct));
    h = fnv1a(h, offsetof(squash_gui_frame, screen));
    h = fnv1a(h, offsetof(squash_gui_frame, label));
    h = fnv1a(h, offsetof(squash_gui_frame, label_pick));
    h = fnv1a(h, offsetof(squash_gui_frame, hr_trust));
    h = fnv1a(h, offsetof(squash_gui_frame, hr_source));
    h = fnv1a(h, offsetof(squash_gui_frame, recording));
    h = fnv1a(h, offsetof(squash_gui_frame, rec_stop));
    h = fnv1a(h, offsetof(squash_gui_frame, armed));
    return fnv1a(h, offsetof(squash_gui_frame, saved_ok));
}

} // namespace squash_gui_abi
#endif // __cplusplus

#endif // SQUASH_GUI_H
