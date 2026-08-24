#ifndef POC_GUI_H
#define POC_GUI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float    accel_x_g;
    float    accel_y_g;
    float    accel_z_g;
    /* Monotonic, from the kernel. Grounds anything that has to advance at a
       known rate rather than at whatever rate frames happen to arrive. */
    uint32_t uptime_ms;
    uint32_t sample_age_ms;
    uint32_t samples;
    uint32_t frames;
    uint8_t  valid;
    uint8_t  _pad[3];
} poc_gui_state;

uint32_t poc_gui_screen_count(void);

/* FNV-1a over the layout as the linked Rust core sees it. A size alone passes
   when two fields of equal width are swapped; this does not. */
uint32_t poc_gui_abi_fingerprint(void);

/* Called by the Rust panic handler. Must not return normally. */
void poc_gui_host_panic(const uint8_t* msg, uint32_t len);

void poc_gui_render(uint8_t* buf, uint32_t buf_len,
                    uint16_t width, uint16_t height,
                    uint32_t screen, const poc_gui_state* state);

#ifdef __cplusplus
} // extern "C"

namespace poc_gui_abi {

constexpr uint32_t kFnvOffsetBasis = 0x811C9DC5u;
constexpr uint32_t kFnvPrime       = 0x01000193u;

constexpr uint32_t fnv1a(uint32_t hash, size_t byte)
{
    return (hash ^ (static_cast<uint32_t>(byte) & 0xFFu)) * kFnvPrime;
}

/// The same walk over the same values in the same order as `abi_fingerprint()`
/// in lib.rs. Two implementations that have to agree -- but a disagreement
/// reports itself at startup, which is the direction an ABI check should fail in.
constexpr uint32_t fingerprint()
{
    uint32_t h = kFnvOffsetBasis;
    h = fnv1a(h, sizeof(poc_gui_state));
    h = fnv1a(h, alignof(poc_gui_state));
    h = fnv1a(h, offsetof(poc_gui_state, accel_x_g));
    h = fnv1a(h, offsetof(poc_gui_state, accel_y_g));
    h = fnv1a(h, offsetof(poc_gui_state, accel_z_g));
    h = fnv1a(h, offsetof(poc_gui_state, uptime_ms));
    h = fnv1a(h, offsetof(poc_gui_state, sample_age_ms));
    h = fnv1a(h, offsetof(poc_gui_state, samples));
    h = fnv1a(h, offsetof(poc_gui_state, frames));
    h = fnv1a(h, offsetof(poc_gui_state, valid));
    h = fnv1a(h, offsetof(poc_gui_state, _pad));
    return h;
}

} // namespace poc_gui_abi

/* Per field, because a size check passes when two fields are swapped. These fail
   at compile time here; lib.rs asserts the same offsets on the other side, so a
   hand edit to either declaration has to break one of the two builds. */
static_assert(sizeof(poc_gui_state) == 32, "poc_gui_state size changed");
static_assert(alignof(poc_gui_state) == 4, "poc_gui_state alignment changed");
static_assert(offsetof(poc_gui_state, accel_x_g) == 0, "accel_x_g moved");
static_assert(offsetof(poc_gui_state, accel_y_g) == 4, "accel_y_g moved");
static_assert(offsetof(poc_gui_state, accel_z_g) == 8, "accel_z_g moved");
static_assert(offsetof(poc_gui_state, uptime_ms) == 12, "uptime_ms moved");
static_assert(offsetof(poc_gui_state, sample_age_ms) == 16, "sample_age_ms moved");
static_assert(offsetof(poc_gui_state, samples) == 20, "samples moved");
static_assert(offsetof(poc_gui_state, frames) == 24, "frames moved");
static_assert(offsetof(poc_gui_state, valid) == 28, "valid moved");
static_assert(offsetof(poc_gui_state, _pad) == 29, "_pad moved");
#endif

#endif // POC_GUI_H
