#ifndef NOTIFY_TOGGLE_GUI_H
#define NOTIFY_TOGGLE_GUI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Why the last read or write ended where it did. The renderer draws a
   different screen for each, because every one of them has a different answer
   to "what happens to my setting now?" and a switch that just moves cannot
   tell them apart. */
enum {
    /* The live flag was read, and any write was also saved to settings.json. */
    NOTIFY_TOGGLE_STATUS_OK = 0,
    /* This watch's firmware is not in SettingsAddresses' table, so nothing is
       read or written at all. */
    NOTIFY_TOGGLE_STATUS_UNSUPPORTED = 1,
    /* The live flag could not be confirmed, so `enabled` means nothing. */
    NOTIFY_TOGGLE_STATUS_UNREADABLE = 2,
    /* The live flag changed and took effect, but settings.json was not
       written: `enabled` is true now and will revert on the next reboot. */
    NOTIFY_TOGGLE_STATUS_NOT_SAVED = 3,
    /* Saving is switched off in the app's settings, so the flag is live only
       and reverts on the next reboot. Nothing went wrong. */
    NOTIFY_TOGGLE_STATUS_LIVE_ONLY = 4
};

/* Everything the renderer needs to draw one frame. This is a read-only view
   of the real watch-wide notifications flag (settings.json), not app state:
   `known` is 0 whenever the last attempt to read that flag did not confirm a
   value, and `status` says what to tell the wearer about it. */
typedef struct {
    uint8_t enabled;   /* 0 or 1: last confirmed value of the real flag */
    uint8_t known;     /* 0 or 1: whether `enabled` is actually trustworthy */
    uint8_t status;    /* one of NOTIFY_TOGGLE_STATUS_* */
    uint8_t _pad[1];
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
    h = fnv1a(h, offsetof(notify_toggle_state, status));
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
static_assert(offsetof(notify_toggle_state, status) == 2, "status moved");
static_assert(offsetof(notify_toggle_state, _pad) == 3, "_pad moved");
#endif

#endif // NOTIFY_TOGGLE_GUI_H
