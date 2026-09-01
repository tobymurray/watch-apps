#ifndef SPIN_GUI_H
#define SPIN_GUI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One render-time struct describes any screen this app draws. Gui.cpp builds
   it fresh from the last Track::Data / Track::State / accessory status the
   Service published, so the renderer is a pure function of it: the same frame
   always draws the same pixels, on the watch and in the host simulator. */

#define SPIN_GUI_SCREEN_READY  0u  /* pre-ride: strap status, waiting to start */
#define SPIN_GUI_SCREEN_RIDING 1u  /* clock running */
#define SPIN_GUI_SCREEN_PAUSED 2u  /* clock held */
#define SPIN_GUI_SCREEN_SAVED  3u  /* finished: what was written */
/* Asking before throwing a ride away, and the acknowledgement after. Two
   deliberate presses on two screens, not one tap -- this is the one action
   here that destroys data. See Gui.cpp for why it is not a press-and-hold. */
#define SPIN_GUI_SCREEN_CONFIRM_DISCARD 4u
#define SPIN_GUI_SCREEN_DISCARDED       5u

/* The strap's BLE link, collapsed from SDK::Accessory::State the same way
   SDK::Gui::SensorStatusRow::hrState() collapses it. */
#define SPIN_GUI_STRAP_ABSENT    0u  /* UNAVAILABLE / IDLE */
#define SPIN_GUI_STRAP_SEARCHING 1u  /* SEARCHING / CONNECTING / LOST */
#define SPIN_GUI_STRAP_CONNECTED 2u  /* CONNECTED */

/* Which source the kernel's arbiter actually believed this second --
   SDK::SensorDataParser::HeartRateEx::Source, carried through unchanged. */
#define SPIN_GUI_HR_NONE     0u
#define SPIN_GUI_HR_OPTICAL  1u
#define SPIN_GUI_HR_EXTERNAL 2u

/* Heart-rate zones. hr_zone 0 means below zone 1 -- or that no zones are set
   at all, in which case there are none to be in and the dial is not drawn.
   How many there are is zone_count, which comes from the app's configuration
   or the watch's own settings; this is only the ceiling. */
#define SPIN_GUI_MAX_ZONES 8u

/* Field order puts the 32-bit field first and the 16-bit pair next, so every
   field lands on its natural alignment with no padding. Not load-bearing --
   the ABI fingerprint below checks whatever the compiler actually produced --
   but there is no reason to pay for padding that is easy to avoid. */
typedef struct {
    uint32_t elapsed_s;      /* active ride seconds; the number on the screen */
    uint16_t hr_bpm;         /* current bpm, 0 = nothing believable right now */
    uint16_t avg_hr_bpm;     /* SAVED only: bpm over the whole ride, 0 = never measured */
    uint16_t target_minutes; /* the configured target, 0 = none set */
    uint16_t energy;         /* already in the display unit below, rounded */
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

/// The same walk over the same values in the same order as `abi_fingerprint()`
/// in lib.rs. Two implementations that have to agree -- but a disagreement
/// reports itself at startup, which is the direction an ABI check should fail in:
/// a stale libspin_gui.a linked against a changed struct is otherwise silent
/// until it draws garbage.
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

static_assert(sizeof(spin_gui_frame) == 24, "spin_gui_frame size changed");
static_assert(alignof(spin_gui_frame) == 4, "spin_gui_frame alignment changed");
static_assert(offsetof(spin_gui_frame, elapsed_s) == 0, "elapsed_s moved");
static_assert(offsetof(spin_gui_frame, hr_bpm) == 4, "hr_bpm moved");
static_assert(offsetof(spin_gui_frame, avg_hr_bpm) == 6, "avg_hr_bpm moved");
static_assert(offsetof(spin_gui_frame, target_minutes) == 8, "target_minutes moved");
static_assert(offsetof(spin_gui_frame, energy) == 10, "energy moved");
static_assert(offsetof(spin_gui_frame, screen) == 12, "screen moved");
static_assert(offsetof(spin_gui_frame, strap) == 13, "strap moved");
static_assert(offsetof(spin_gui_frame, hr_source) == 14, "hr_source moved");
static_assert(offsetof(spin_gui_frame, saved_ok) == 15, "saved_ok moved");
static_assert(offsetof(spin_gui_frame, target_reached) == 16, "target_reached moved");
static_assert(offsetof(spin_gui_frame, hr_zone) == 17, "hr_zone moved");
static_assert(offsetof(spin_gui_frame, zone_count) == 18, "zone_count moved");
static_assert(offsetof(spin_gui_frame, has_zones) == 19, "has_zones moved");
static_assert(offsetof(spin_gui_frame, energy_is_kj) == 20, "energy_is_kj moved");
static_assert(offsetof(spin_gui_frame, hr_zone_fraction) == 21, "hr_zone_fraction moved");
#endif

#endif // SPIN_GUI_H
