#ifndef SLUMBER_GUI_H
#define SLUMBER_GUI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SLUMBER_NO_MARK     0xFFu
#define SLUMBER_NO_MARK_AGE 0xFFFFFFFFu
#define SLUMBER_UNKNOWN     0xFFFFu

#define SLUMBER_SCREEN_NIGHT  0u
#define SLUMBER_SCREEN_PICKER 1u

#define SLUMBER_BUTTON_L1    0u
#define SLUMBER_BUTTON_L2    1u
#define SLUMBER_BUTTON_OTHER 2u

/* `have_status` is 0 until the service first replies. */
typedef struct {
    uint32_t service_up_min;
    uint32_t rows_today;
    uint32_t write_fails;
    uint32_t mark_ago_min;  /* SLUMBER_NO_MARK_AGE = none */
    uint8_t  have_status;   /* 0 or 1 */
    uint8_t  selected;      /* picker position of the highlighted label */
    uint8_t  last_mark;     /* SLUMBER_NO_MARK = none */
    uint8_t  acc_ok;        /* 0 or 1 */
    uint8_t  hr_ok;         /* 0 or 1 */
    uint8_t  just_logged;   /* 0 or 1: the last R1 was answered */
    uint8_t  screen;        /* SLUMBER_SCREEN_* */
    uint8_t  night_kind;    /* CustomMessage::NightKind */
    uint16_t rest_min;
    uint16_t onset_min;     /* minute of day */
    uint16_t wake_min;      /* minute of day */
    uint16_t rest_fill;     /* per mille; past 1000 is a second lap */
    uint16_t rhythm_fill;   /* per mille; SLUMBER_UNKNOWN = no usual */
    int16_t  rhythm_offset_min;
    uint16_t usual_bed_min; /* minute of day; SLUMBER_UNKNOWN = none */
    uint8_t  restless;
    uint8_t  _pad[1];
} slumber_state;

/* FNV-1a over the layout as the linked Rust core sees it. */
uint32_t slumber_abi_fingerprint(void);

/* Called by the Rust panic handler. Must not return normally. */
void panickit_host_panic(const uint8_t* msg, uint32_t len);

void slumber_render(uint8_t* buf, uint32_t buf_len,
                    uint16_t width, uint16_t height,
                    const slumber_state* state);

/* How many labels the picker offers. */
uint32_t slumber_mark_count(void);

/* The mark code at picker position `index`. */
uint32_t slumber_mark_code(uint32_t index);

/* Copies the label of mark code `code` as ASCII, NUL-terminated, into `out`; returns its length. */
uint32_t slumber_mark_label(uint32_t code, uint8_t* out, uint32_t cap);

/* How much of the picker's press sequence ends with `button`, `gap_ms` after the press before. */
uint8_t slumber_sequence_step(uint8_t progress, uint8_t button, uint32_t gap_ms);

/* The press sequence's length: a step returning it opens the picker. */
uint8_t slumber_sequence_len(void);

#ifdef __cplusplus
} // extern "C"

namespace slumber_abi {

constexpr uint32_t kFnvOffsetBasis = 0x811C9DC5u;
constexpr uint32_t kFnvPrime       = 0x01000193u;

constexpr uint32_t fnv1a(uint32_t hash, size_t byte)
{
    return (hash ^ (static_cast<uint32_t>(byte) & 0xFFu)) * kFnvPrime;
}

/// The same walk as `abi_fingerprint()` in lib.rs.
constexpr uint32_t fingerprint()
{
    uint32_t h = kFnvOffsetBasis;
    h = fnv1a(h, sizeof(slumber_state));
    h = fnv1a(h, alignof(slumber_state));
    h = fnv1a(h, offsetof(slumber_state, service_up_min));
    h = fnv1a(h, offsetof(slumber_state, rows_today));
    h = fnv1a(h, offsetof(slumber_state, write_fails));
    h = fnv1a(h, offsetof(slumber_state, mark_ago_min));
    h = fnv1a(h, offsetof(slumber_state, have_status));
    h = fnv1a(h, offsetof(slumber_state, selected));
    h = fnv1a(h, offsetof(slumber_state, last_mark));
    h = fnv1a(h, offsetof(slumber_state, acc_ok));
    h = fnv1a(h, offsetof(slumber_state, hr_ok));
    h = fnv1a(h, offsetof(slumber_state, just_logged));
    h = fnv1a(h, offsetof(slumber_state, screen));
    h = fnv1a(h, offsetof(slumber_state, night_kind));
    h = fnv1a(h, offsetof(slumber_state, rest_min));
    h = fnv1a(h, offsetof(slumber_state, onset_min));
    h = fnv1a(h, offsetof(slumber_state, wake_min));
    h = fnv1a(h, offsetof(slumber_state, rest_fill));
    h = fnv1a(h, offsetof(slumber_state, rhythm_fill));
    h = fnv1a(h, offsetof(slumber_state, rhythm_offset_min));
    h = fnv1a(h, offsetof(slumber_state, usual_bed_min));
    h = fnv1a(h, offsetof(slumber_state, restless));
    h = fnv1a(h, offsetof(slumber_state, _pad));
    return h;
}

} // namespace slumber_abi

static_assert(sizeof(slumber_state) == 40, "slumber_state size changed");
static_assert(alignof(slumber_state) == 4, "slumber_state alignment changed");
static_assert(offsetof(slumber_state, rows_today) == 4, "rows_today moved");
static_assert(offsetof(slumber_state, write_fails) == 8, "write_fails moved");
static_assert(offsetof(slumber_state, mark_ago_min) == 12, "mark_ago_min moved");
static_assert(offsetof(slumber_state, have_status) == 16, "have_status moved");
static_assert(offsetof(slumber_state, selected) == 17, "selected moved");
static_assert(offsetof(slumber_state, last_mark) == 18, "last_mark moved");
static_assert(offsetof(slumber_state, acc_ok) == 19, "acc_ok moved");
static_assert(offsetof(slumber_state, hr_ok) == 20, "hr_ok moved");
static_assert(offsetof(slumber_state, just_logged) == 21, "just_logged moved");
static_assert(offsetof(slumber_state, screen) == 22, "screen moved");
static_assert(offsetof(slumber_state, night_kind) == 23, "night_kind moved");
static_assert(offsetof(slumber_state, rest_min) == 24, "rest_min moved");
static_assert(offsetof(slumber_state, rest_fill) == 30, "rest_fill moved");
static_assert(offsetof(slumber_state, rhythm_offset_min) == 34, "rhythm_offset_min moved");
static_assert(offsetof(slumber_state, usual_bed_min) == 36, "usual_bed_min moved");
static_assert(offsetof(slumber_state, restless) == 38, "restless moved");
#endif

#endif // SLUMBER_GUI_H
