#ifndef NOTIFY_TOGGLE_GUI_H
#define NOTIFY_TOGGLE_GUI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Everything the renderer needs to draw one frame. This is a read-only view
   of the real watch-wide notifications flag (settings.json), not app state:
   `known` is 0 whenever the last attempt to read or write that file did not
   confirm a value (missing file, unexpected shape, a write that failed) --
   the renderer must show an explicit "unknown" state rather than guess, the
   same fail-closed rule LiveSettings.cpp and SettingsPersist.cpp apply to
   the live struct and the file itself. */
typedef struct {
    uint8_t enabled;   /* 0 or 1: last confirmed value of the real flag */
    uint8_t known;     /* 0 or 1: whether `enabled` is actually trustworthy */
    uint8_t _pad[2];
} notify_toggle_state;

/* FNV-1a over the layout as the linked Rust core sees it. A size alone passes
   when two fields of equal width are swapped; this does not. */
uint32_t notify_toggle_abi_fingerprint(void);

/* Called by the Rust panic handler. Must not return normally. */
void notify_toggle_host_panic(const uint8_t* msg, uint32_t len);

void notify_toggle_render(uint8_t* buf, uint32_t buf_len,
                           uint16_t width, uint16_t height,
                           const notify_toggle_state* state);

#ifdef __cplusplus
} // extern "C"

namespace notify_toggle_abi {

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
    h = fnv1a(h, sizeof(notify_toggle_state));
    h = fnv1a(h, alignof(notify_toggle_state));
    h = fnv1a(h, offsetof(notify_toggle_state, enabled));
    h = fnv1a(h, offsetof(notify_toggle_state, known));
    h = fnv1a(h, offsetof(notify_toggle_state, _pad));
    return h;
}

} // namespace notify_toggle_abi

/* Per field, because a size check passes when two fields are swapped. These fail
   at compile time here; lib.rs asserts the same offsets on the other side, so a
   hand edit to either declaration has to break one of the two builds. */
static_assert(sizeof(notify_toggle_state) == 4, "notify_toggle_state size changed");
static_assert(alignof(notify_toggle_state) == 1, "notify_toggle_state alignment changed");
static_assert(offsetof(notify_toggle_state, enabled) == 0, "enabled moved");
static_assert(offsetof(notify_toggle_state, known) == 1, "known moved");
static_assert(offsetof(notify_toggle_state, _pad) == 2, "_pad moved");
#endif

#endif // NOTIFY_TOGGLE_GUI_H
