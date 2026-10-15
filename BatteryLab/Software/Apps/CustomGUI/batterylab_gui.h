#ifndef BATTERYLAB_GUI_H
#define BATTERYLAB_GUI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A field no message has carried yet. */
#define BATTERYLAB_NONE INT32_MIN

/* `have_status` is 0 until the service first replies. */
typedef struct {
    int32_t  pct_x1000;
    int32_t  mv;
    int32_t  ma_x1000;
    int32_t  avg_ma_x1000;
    int32_t  mah_x1000;
    int32_t  design_mah_x1000;
    int32_t  connected;
    int32_t  charging;
    uint32_t lines;
    uint32_t write_fails;
    uint32_t service_up_min;
    uint8_t  have_status;
    uint8_t  _pad[3];
} batterylab_state;

/* FNV-1a over the layout as the linked Rust core sees it. */
uint32_t batterylab_abi_fingerprint(void);

/* Called by the Rust panic handler. Must not return normally. */
void panickit_host_panic(const uint8_t* msg, uint32_t len);

void batterylab_render(uint8_t* buf, uint32_t buf_len,
                       uint16_t width, uint16_t height,
                       const batterylab_state* state);

#ifdef __cplusplus
} // extern "C"

namespace batterylab_abi {

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
    h = fnv1a(h, sizeof(batterylab_state));
    h = fnv1a(h, alignof(batterylab_state));
    h = fnv1a(h, offsetof(batterylab_state, pct_x1000));
    h = fnv1a(h, offsetof(batterylab_state, mv));
    h = fnv1a(h, offsetof(batterylab_state, ma_x1000));
    h = fnv1a(h, offsetof(batterylab_state, avg_ma_x1000));
    h = fnv1a(h, offsetof(batterylab_state, mah_x1000));
    h = fnv1a(h, offsetof(batterylab_state, design_mah_x1000));
    h = fnv1a(h, offsetof(batterylab_state, connected));
    h = fnv1a(h, offsetof(batterylab_state, charging));
    h = fnv1a(h, offsetof(batterylab_state, lines));
    h = fnv1a(h, offsetof(batterylab_state, write_fails));
    h = fnv1a(h, offsetof(batterylab_state, service_up_min));
    h = fnv1a(h, offsetof(batterylab_state, have_status));
    h = fnv1a(h, offsetof(batterylab_state, _pad));
    return h;
}

} // namespace batterylab_abi

static_assert(sizeof(batterylab_state) == 48, "batterylab_state size changed");
static_assert(alignof(batterylab_state) == 4, "batterylab_state alignment changed");
static_assert(offsetof(batterylab_state, lines) == 32, "lines moved");
static_assert(offsetof(batterylab_state, have_status) == 44, "have_status moved");
#endif

#endif // BATTERYLAB_GUI_H
