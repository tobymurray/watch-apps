#ifndef SPIN_GUI_H
#define SPIN_GUI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One render-time struct describes any screen this app draws, so the renderer
   is a pure function of it. */

#define SPIN_GUI_SCREEN_READY  0u  /* pre-ride: strap status, waiting to start */
#define SPIN_GUI_SCREEN_RIDING 1u  /* clock running */
#define SPIN_GUI_SCREEN_PAUSED 2u  /* clock held */
#define SPIN_GUI_SCREEN_SAVED  3u  /* finished: what was written */
#define SPIN_GUI_SCREEN_CONFIRM_DISCARD 4u
#define SPIN_GUI_SCREEN_DISCARDED       5u
#define SPIN_GUI_SCREEN_ENTER_WORK      6u

/* The strap's BLE link, collapsed from SDK::Accessory::State. */
#define SPIN_GUI_STRAP_ABSENT    0u  /* UNAVAILABLE / IDLE */
#define SPIN_GUI_STRAP_SEARCHING 1u  /* SEARCHING / CONNECTING / LOST */
#define SPIN_GUI_STRAP_CONNECTED 2u  /* CONNECTED */

/* SDK::SensorDataParser::HeartRateEx::Source, carried through unchanged. */
#define SPIN_GUI_HR_NONE     0u
#define SPIN_GUI_HR_OPTICAL  1u
#define SPIN_GUI_HR_EXTERNAL 2u

#define SPIN_GUI_MAX_ZONES 8u

/* Widest fields first, so nothing pays for padding. Not load-bearing: the
   fingerprint below checks whatever the compiler actually produced. */
typedef struct {
    uint32_t elapsed_s;      /* active ride seconds; the number on the screen */
    uint16_t hr_bpm;         /* current bpm, 0 = nothing believable right now */
    uint16_t avg_hr_bpm;     /* SAVED only: bpm over the whole ride, 0 = never measured */
    uint16_t target_minutes; /* the configured target, 0 = none set */
    uint16_t energy;         /* already in the display unit below, rounded */
    uint16_t work_kj;          /* kJ built so far, 0 = nothing said */
    uint16_t work_estimate_kj; /* kJ the calorie model suggests, 0 = no row */
    uint8_t  screen;         /* one of the SPIN_GUI_SCREEN_* values above */
    uint8_t  strap;          /* one of the SPIN_GUI_STRAP_* values above */
    uint8_t  hr_source;      /* one of the SPIN_GUI_HR_* values above */
    uint8_t  saved_ok;       /* SAVED only: 1 the .fit is on disk, 0 it is not */
    uint8_t  target_reached; /* 1 once the target is passed; the flag that buzzed */
    uint8_t  hr_zone;        /* 0 = below zone 1 or no zones set, else 1..zone_count */
    uint8_t  zone_count;     /* segments on the dial, 2..8; 0 = no zones set */
    uint8_t  has_zones;      /* 1 when the wearer has thresholds set on the watch */
    uint8_t  energy_is_kj;   /* 1 = `energy` is kJ, 0 = kcal */
    /* Where the current heart rate sits WITHIN hr_zone, 0..255 across that
       zone's own span. Sent as a fraction of the zone rather than of the whole
       scale so the needle always lands inside the segment that is lit: the
       segments are equal angular slices with gaps between them, and a position
       measured across the whole ring drifts out of its own segment by the
       fifth one. Meaningless unless has_zones and hr_zone >= 1. */
    uint8_t  hr_zone_fraction;
} spin_gui_frame;

uint32_t spin_gui_abi_fingerprint(void);

/* The kilojoule entry model, as pure functions of the current value: Gui.cpp
   holds one uint16_t, so there is no entry state on the C++ side. See work.rs. */
uint16_t spin_gui_work_add_hundreds(uint16_t kj);   /* L1 */
uint16_t spin_gui_work_add_tens(uint16_t kj);       /* L2 */
/* A reference to draw beside the field, never a value to pre-fill it with.
   0 = nothing worth showing. */
uint16_t spin_gui_work_estimate_kj(float active_kcal);
void     spin_gui_host_panic(const uint8_t *msg, uint32_t len);
void     spin_gui_render(uint8_t *buf, uint32_t buf_len,
                         uint16_t width, uint16_t height,
                         const spin_gui_frame *frame);

#ifdef __cplusplus
} // extern "C"

namespace spin_gui_abi {

constexpr uint32_t kFnvOffsetBasis = 0x811C9DC5u;
constexpr uint32_t kFnvPrime       = 0x01000193u;

constexpr uint32_t fnv1a(uint32_t hash, size_t byte)
{
    return (hash ^ (static_cast<uint32_t>(byte) & 0xFFu)) * kFnvPrime;
}

/// The same walk in the same order as `abi_fingerprint()` in lib.rs. Gui::run()
/// refuses to start on a disagreement, which is the direction this should fail
/// in: a stale libspin_gui.a is otherwise silent until it draws garbage.
constexpr uint32_t fingerprint()
{
    uint32_t h = kFnvOffsetBasis;
    h = fnv1a(h, sizeof(spin_gui_frame));
    h = fnv1a(h, alignof(spin_gui_frame));
    h = fnv1a(h, offsetof(spin_gui_frame, elapsed_s));
    h = fnv1a(h, offsetof(spin_gui_frame, hr_bpm));
    h = fnv1a(h, offsetof(spin_gui_frame, avg_hr_bpm));
    h = fnv1a(h, offsetof(spin_gui_frame, target_minutes));
    h = fnv1a(h, offsetof(spin_gui_frame, energy));
    h = fnv1a(h, offsetof(spin_gui_frame, work_kj));
    h = fnv1a(h, offsetof(spin_gui_frame, work_estimate_kj));
    h = fnv1a(h, offsetof(spin_gui_frame, screen));
    h = fnv1a(h, offsetof(spin_gui_frame, strap));
    h = fnv1a(h, offsetof(spin_gui_frame, hr_source));
    h = fnv1a(h, offsetof(spin_gui_frame, saved_ok));
    h = fnv1a(h, offsetof(spin_gui_frame, target_reached));
    h = fnv1a(h, offsetof(spin_gui_frame, hr_zone));
    h = fnv1a(h, offsetof(spin_gui_frame, zone_count));
    h = fnv1a(h, offsetof(spin_gui_frame, has_zones));
    h = fnv1a(h, offsetof(spin_gui_frame, energy_is_kj));
    return fnv1a(h, offsetof(spin_gui_frame, hr_zone_fraction));
}

} // namespace spin_gui_abi

static_assert(sizeof(spin_gui_frame) == 28, "spin_gui_frame size changed");
static_assert(alignof(spin_gui_frame) == 4, "spin_gui_frame alignment changed");
static_assert(offsetof(spin_gui_frame, elapsed_s) == 0, "elapsed_s moved");
static_assert(offsetof(spin_gui_frame, hr_bpm) == 4, "hr_bpm moved");
static_assert(offsetof(spin_gui_frame, avg_hr_bpm) == 6, "avg_hr_bpm moved");
static_assert(offsetof(spin_gui_frame, target_minutes) == 8, "target_minutes moved");
static_assert(offsetof(spin_gui_frame, energy) == 10, "energy moved");
static_assert(offsetof(spin_gui_frame, work_kj) == 12, "work_kj moved");
static_assert(offsetof(spin_gui_frame, work_estimate_kj) == 14, "work_estimate_kj moved");
static_assert(offsetof(spin_gui_frame, screen) == 16, "screen moved");
static_assert(offsetof(spin_gui_frame, strap) == 17, "strap moved");
static_assert(offsetof(spin_gui_frame, hr_source) == 18, "hr_source moved");
static_assert(offsetof(spin_gui_frame, saved_ok) == 19, "saved_ok moved");
static_assert(offsetof(spin_gui_frame, target_reached) == 20, "target_reached moved");
static_assert(offsetof(spin_gui_frame, hr_zone) == 21, "hr_zone moved");
static_assert(offsetof(spin_gui_frame, zone_count) == 22, "zone_count moved");
static_assert(offsetof(spin_gui_frame, has_zones) == 23, "has_zones moved");
static_assert(offsetof(spin_gui_frame, energy_is_kj) == 24, "energy_is_kj moved");
static_assert(offsetof(spin_gui_frame, hr_zone_fraction) == 25, "hr_zone_fraction moved");
#endif

#endif // SPIN_GUI_H
