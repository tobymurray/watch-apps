#ifndef BARCODE_GUI_H
#define BARCODE_GUI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One render-time struct describes any screen this app draws. It is built
   fresh in Gui.cpp from the current Barcode::State plus the on-screen index,
   so it carries no message-pool size constraint the way CustomMessage::
   BarcodeState does -- generous field sizes here cost nothing but stack. */

#define BARCODE_GUI_KIND_CODE128 0u
#define BARCODE_GUI_KIND_ITF     1u
#define BARCODE_GUI_KIND_QR      2u
#define BARCODE_GUI_KIND_PROMPT  3u

/* Field order chosen so every field lands on its natural alignment with zero
   padding: the one 16-bit field goes first, then bytes. Not load-bearing --
   the ABI check below verifies whatever the compiler actually produced -- but
   there is no reason to pay for padding that is easy to avoid. */
typedef struct {
    uint16_t total_modules;         /* Encoded::totalModules, linear kinds only */
    uint8_t  kind;                  /* one of the BARCODE_GUI_KIND_* values above */
    uint8_t  width_count;           /* Encoded::count, linear kinds only */
    uint8_t  widths[151];           /* Encoded::widths, Encoded::kMaxWidths */
    uint8_t  matrix_bits[79];       /* Barcode::Matrix::bits, QR only */
    uint8_t  matrix_size;           /* Barcode::Matrix::size, QR only */
    char     id[23];                /* Barcode::Code::id, kMaxIdLength+1 */
    char     name[13];              /* Barcode::Code::name, kMaxNameLength+1 */
    uint8_t  index;                 /* which code is on screen, for the pager */
    uint8_t  count;                 /* how many codes there are, for the pager */
    char     message[96];           /* PROMPT only: one flat, unwrapped string --
                                        Rust word-wraps it at the chosen font, so
                                        no line break is ever hand-transcribed */
} barcode_gui_frame;

uint32_t barcode_gui_abi_fingerprint(void);
void     barcode_gui_host_panic(const uint8_t *msg, uint32_t len);
void     barcode_gui_render(uint8_t *buf, uint32_t buf_len,
                             uint16_t width, uint16_t height,
                             const barcode_gui_frame *frame);

#ifdef __cplusplus
} // extern "C"

namespace barcode_gui_abi {

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
    h = fnv1a(h, sizeof(barcode_gui_frame));
    h = fnv1a(h, alignof(barcode_gui_frame));
    h = fnv1a(h, offsetof(barcode_gui_frame, total_modules));
    h = fnv1a(h, offsetof(barcode_gui_frame, kind));
    h = fnv1a(h, offsetof(barcode_gui_frame, width_count));
    h = fnv1a(h, offsetof(barcode_gui_frame, widths));
    h = fnv1a(h, offsetof(barcode_gui_frame, matrix_bits));
    h = fnv1a(h, offsetof(barcode_gui_frame, matrix_size));
    h = fnv1a(h, offsetof(barcode_gui_frame, id));
    h = fnv1a(h, offsetof(barcode_gui_frame, name));
    h = fnv1a(h, offsetof(barcode_gui_frame, index));
    h = fnv1a(h, offsetof(barcode_gui_frame, count));
    h = fnv1a(h, offsetof(barcode_gui_frame, message));
    return h;
}

} // namespace barcode_gui_abi

static_assert(sizeof(barcode_gui_frame) == 370, "barcode_gui_frame size changed");
static_assert(alignof(barcode_gui_frame) == 2, "barcode_gui_frame alignment changed");
static_assert(offsetof(barcode_gui_frame, total_modules) == 0, "total_modules moved");
static_assert(offsetof(barcode_gui_frame, kind) == 2, "kind moved");
static_assert(offsetof(barcode_gui_frame, width_count) == 3, "width_count moved");
static_assert(offsetof(barcode_gui_frame, widths) == 4, "widths moved");
static_assert(offsetof(barcode_gui_frame, matrix_bits) == 155, "matrix_bits moved");
static_assert(offsetof(barcode_gui_frame, matrix_size) == 234, "matrix_size moved");
static_assert(offsetof(barcode_gui_frame, id) == 235, "id moved");
static_assert(offsetof(barcode_gui_frame, name) == 258, "name moved");
static_assert(offsetof(barcode_gui_frame, index) == 271, "index moved");
static_assert(offsetof(barcode_gui_frame, count) == 272, "count moved");
static_assert(offsetof(barcode_gui_frame, message) == 273, "message moved");
#endif

#endif // BARCODE_GUI_H
