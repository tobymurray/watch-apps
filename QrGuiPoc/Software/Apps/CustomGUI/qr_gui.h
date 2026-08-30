#ifndef QR_GUI_H
#define QR_GUI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mirrors Barcode::Matrix's layout exactly (Barcode/Software/Libs/Header/Matrix.hpp):
   row-major, one bit a module, 1 is dark; size in modules, excluding the quiet
   zone. Gui.cpp copies a real Barcode::Matrix into this rather than the Rust
   side depending on Barcode's header directly -- the C ABI is the only thing
   crossing the language boundary. */
typedef struct {
    uint8_t bits[79];
    uint8_t size;
} qr_gui_state;

/* FNV-1a over the layout as the linked Rust core sees it. A size alone passes
   when two fields of equal width are swapped; this does not. */
uint32_t qr_gui_abi_fingerprint(void);

/* Called by the Rust panic handler. Must not return normally. */
void qr_gui_host_panic(const uint8_t* msg, uint32_t len);

void qr_gui_render(uint8_t* buf, uint32_t buf_len,
                    uint16_t width, uint16_t height,
                    const qr_gui_state* state);

#ifdef __cplusplus
} // extern "C"

namespace qr_gui_abi {

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
    h = fnv1a(h, sizeof(qr_gui_state));
    h = fnv1a(h, alignof(qr_gui_state));
    h = fnv1a(h, offsetof(qr_gui_state, bits));
    h = fnv1a(h, offsetof(qr_gui_state, size));
    return h;
}

} // namespace qr_gui_abi

/* Per field, because a size check passes when two fields are swapped. These fail
   at compile time here; lib.rs asserts the same offsets on the other side, so a
   hand edit to either declaration has to break one of the two builds. */
static_assert(sizeof(qr_gui_state) == 80, "qr_gui_state size changed");
static_assert(alignof(qr_gui_state) == 1, "qr_gui_state alignment changed");
static_assert(offsetof(qr_gui_state, bits) == 0, "bits moved");
static_assert(offsetof(qr_gui_state, size) == 79, "size moved");
#endif

#endif // QR_GUI_H
