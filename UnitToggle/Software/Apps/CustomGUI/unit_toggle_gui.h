#ifndef UNIT_TOGGLE_GUI_H
#define UNIT_TOGGLE_GUI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Why the last read or write ended where it did. The renderer draws a
   different screen for each, because every one of them has a different answer
   to "what happens to my setting now?" and a control that just moves cannot
   tell them apart. */
enum {
    /* The units were read, and any change also reached settings.json. */
    UNIT_TOGGLE_STATUS_OK = 0,
    /* The firmware gate refused, so R1 cannot move the choice. `imperial` is
       still trustworthy: it comes from RequestSystemSettings, which needs no
       gate. This is where it differs from NotifyToggle's UNSUPPORTED, where
       nothing at all could be read. */
    UNIT_TOGGLE_STATUS_UNSUPPORTED = 1,
    /* The units could not be confirmed at all, so `imperial` means nothing. */
    UNIT_TOGGLE_STATUS_UNREADABLE = 2,
    /* The units changed and took effect, but settings.json was not written:
       the change is real now and reverts on the next reboot. */
    UNIT_TOGGLE_STATUS_NOT_SAVED = 3,
    /* Saving is switched off in the app's settings, so the change is live only
       and reverts on the next reboot. Nothing went wrong. */
    UNIT_TOGGLE_STATUS_LIVE_ONLY = 4,
    /* The firmware is one this app knows, but `2:/settings.json` could not be
       read or is not the shape it understands. Nothing was touched. */
    UNIT_TOGGLE_STATUS_NO_SETTINGS = 5
};

/* Everything the renderer needs to draw one frame. This is a read-only view of
   the real watch-wide units setting, not app state: `known` is 0 whenever the
   last attempt to establish the units did not confirm a value, and `status`
   says what to tell the wearer about it. */
typedef struct {
    uint8_t imperial;  /* 0 = metric, 1 = imperial */
    uint8_t known;     /* 0 or 1: whether `imperial` is actually trustworthy */
    uint8_t status;    /* one of UNIT_TOGGLE_STATUS_* */
    uint8_t _pad[1];
} unit_toggle_state;

/* FNV-1a over the layout as the linked Rust core sees it. A size alone passes
   when two fields of equal width are swapped; this does not. */
uint32_t unit_toggle_abi_fingerprint(void);

/* Called by the Rust panic handler. Must not return normally. */
void unit_toggle_host_panic(const uint8_t* msg, uint32_t len);

void unit_toggle_render(uint8_t* buf, uint32_t buf_len,
                        uint16_t width, uint16_t height,
                        const unit_toggle_state* state);

#ifdef __cplusplus
} // extern "C"

namespace unit_toggle_abi {

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
    h = fnv1a(h, sizeof(unit_toggle_state));
    h = fnv1a(h, alignof(unit_toggle_state));
    h = fnv1a(h, offsetof(unit_toggle_state, imperial));
    h = fnv1a(h, offsetof(unit_toggle_state, known));
    h = fnv1a(h, offsetof(unit_toggle_state, status));
    h = fnv1a(h, offsetof(unit_toggle_state, _pad));
    return h;
}

} // namespace unit_toggle_abi

/* Per field, because a size check passes when two fields are swapped. These fail
   at compile time here; lib.rs asserts the same offsets on the other side, so a
   hand edit to either declaration has to break one of the two builds. */
static_assert(sizeof(unit_toggle_state) == 4, "unit_toggle_state size changed");
static_assert(alignof(unit_toggle_state) == 1, "unit_toggle_state alignment changed");
static_assert(offsetof(unit_toggle_state, imperial) == 0, "imperial moved");
static_assert(offsetof(unit_toggle_state, known) == 1, "known moved");
static_assert(offsetof(unit_toggle_state, status) == 2, "status moved");
static_assert(offsetof(unit_toggle_state, _pad) == 3, "_pad moved");
#endif

#endif // UNIT_TOGGLE_GUI_H
