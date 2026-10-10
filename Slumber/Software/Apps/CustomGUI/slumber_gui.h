#ifndef SLUMBER_GUI_H
#define SLUMBER_GUI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SLUMBER_NO_MARK     0xFFu
#define SLUMBER_NO_MARK_AGE 0xFFFFFFFFu

/* `have_status` is 0 until the service first replies. */
typedef struct {
    uint32_t service_up_min;
    uint32_t rows_today;
    uint32_t write_fails;
    uint32_t mark_ago_min;  /* SLUMBER_NO_MARK_AGE = none */
    uint8_t  have_status;   /* 0 or 1 */
    uint8_t  selected;      /* index of the highlighted label */
    uint8_t  last_mark;     /* SLUMBER_NO_MARK = none */
    uint8_t  acc_ok;        /* 0 or 1 */
    uint8_t  hr_ok;         /* 0 or 1 */
    uint8_t  just_logged;   /* 0 or 1: the last R1 was answered */
    uint8_t  _pad[2];
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

/* Copies label `index` as ASCII, NUL-terminated, into `out`; returns its length. */
uint32_t slumber_mark_label(uint32_t index, uint8_t* out, uint32_t cap);

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
    h = fnv1a(h, offsetof(slumber_state, _pad));
    return h;
}

} // namespace slumber_abi

static_assert(sizeof(slumber_state) == 24, "slumber_state size changed");
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
#endif

#endif // SLUMBER_GUI_H
