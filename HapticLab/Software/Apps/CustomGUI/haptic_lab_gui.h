#ifndef HAPTIC_LAB_GUI_H
#define HAPTIC_LAB_GUI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One render-time struct describes every screen.
 *
 * It carries no prose. Every instruction the wearer reads is a literal in
 * lib.rs, selected here by id, so the wrap test and the round-screen test
 * enumerate the strings the watch actually draws rather than a second copy of
 * them that could drift.
 *
 * It also carries nothing that could say which pattern was played. That is not
 * an accident of the field list: FrameBuilder.hpp is where the rule is
 * enforced and `the_answer_screen_carries_nothing_about_which_was_played` is
 * where it is held. */

#define HAPTIC_LAB_SCREEN_MENU          0u
#define HAPTIC_LAB_SCREEN_CALIBRATION   1u
#define HAPTIC_LAB_SCREEN_LEARN         2u
#define HAPTIC_LAB_SCREEN_WAIT          3u
#define HAPTIC_LAB_SCREEN_PLAYING       4u
#define HAPTIC_LAB_SCREEN_ANSWER        5u
#define HAPTIC_LAB_SCREEN_FEEDBACK      6u
#define HAPTIC_LAB_SCREEN_REST          7u
#define HAPTIC_LAB_SCREEN_DONE          8u
#define HAPTIC_LAB_SCREEN_ABORTED       9u
#define HAPTIC_LAB_SCREEN_PROBE        10u
#define HAPTIC_LAB_SCREEN_PROBE_SENT   11u
#define HAPTIC_LAB_SCREEN_SWEEP        12u

#define HAPTIC_LAB_FEEDBACK_NONE    0u
#define HAPTIC_LAB_FEEDBACK_CORRECT 1u
#define HAPTIC_LAB_FEEDBACK_WRONG   2u
#define HAPTIC_LAB_FEEDBACK_MISS    3u
#define HAPTIC_LAB_FEEDBACK_TIMEOUT 4u

/* Which instruction to draw. The strings are in lib.rs. */
#define HAPTIC_LAB_MSG_NONE            0u
#define HAPTIC_LAB_MSG_MENU            1u
#define HAPTIC_LAB_MSG_CALIBRATE       2u
#define HAPTIC_LAB_MSG_CALIBRATE_DONE  3u
#define HAPTIC_LAB_MSG_WHICH_ONE       4u
#define HAPTIC_LAB_MSG_SWEEP           5u
#define HAPTIC_LAB_MSG_STORAGE_FULL    6u
#define HAPTIC_LAB_MSG_NO_FILES        7u
#define HAPTIC_LAB_MSG_COUNT           8u

#define HAPTIC_LAB_NO_BUTTON 0xFFu

/* Field order chosen so every field lands on its natural alignment with no
   padding: the 32-bit field first, then the 16-bit ones, then bytes. */
typedef struct {
    uint32_t modelled_duration_ms; /* LEARN only: what the app believes it lasts */
    uint16_t block_trials;
    uint16_t trials_total;
    uint16_t sweep_index;
    uint16_t sweep_count;
    uint8_t  screen;               /* one of HAPTIC_LAB_SCREEN_* */
    uint8_t  block;
    uint8_t  block_count;
    uint8_t  block_kind;           /* 0 training, 1 test */
    uint8_t  calibration_mask;     /* bit per EventButton::Id already pressed */
    uint8_t  last_button;          /* CALIBRATION: the one just pressed */
    uint8_t  feedback;             /* one of HAPTIC_LAB_FEEDBACK_* */
    uint8_t  learn_index;
    uint8_t  learn_count;
    uint8_t  probe_index;
    uint8_t  probe_count;          /* checked against the renderer's own list */
    uint8_t  pattern_count;
    uint8_t  message_id;           /* one of HAPTIC_LAB_MSG_* */
    uint8_t  contact;              /* 0 wrist, 1 table */
    uint8_t  masking;              /* 0 unmasked, 1 masked */
    char     alt[3][9];            /* ANSWER: the three, in the order drawn */
    char     name[9];              /* LEARN and FEEDBACK: a pattern's own name */
} haptic_lab_gui_frame;

uint32_t haptic_lab_gui_abi_fingerprint(void);
void     haptic_lab_gui_host_panic(const uint8_t *msg, uint32_t len);
void     haptic_lab_gui_render(uint8_t *buf, uint32_t buf_len,
                               uint16_t width, uint16_t height,
                               const haptic_lab_gui_frame *frame);

#ifdef __cplusplus
} /* extern "C" */

namespace haptic_lab_gui_abi {

constexpr uint32_t kFnvOffsetBasis = 0x811C9DC5u;
constexpr uint32_t kFnvPrime       = 0x01000193u;

constexpr uint32_t fnv1a(uint32_t hash, size_t byte)
{
    return (hash ^ (static_cast<uint32_t>(byte) & 0xFFu)) * kFnvPrime;
}

/// The same walk over the same values in the same order as `abi_fingerprint()`
/// in lib.rs, so a stale archive reports itself at startup rather than drawing
/// the wrong screen.
constexpr uint32_t fingerprint()
{
    uint32_t h = kFnvOffsetBasis;
    h = fnv1a(h, sizeof(haptic_lab_gui_frame));
    h = fnv1a(h, alignof(haptic_lab_gui_frame));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, modelled_duration_ms));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, block_trials));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, trials_total));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, sweep_index));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, sweep_count));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, screen));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, block));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, block_count));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, block_kind));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, calibration_mask));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, last_button));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, feedback));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, learn_index));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, learn_count));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, probe_index));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, probe_count));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, pattern_count));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, message_id));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, contact));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, masking));
    h = fnv1a(h, offsetof(haptic_lab_gui_frame, alt));
    return fnv1a(h, offsetof(haptic_lab_gui_frame, name));
}

} // namespace haptic_lab_gui_abi

static_assert(sizeof(haptic_lab_gui_frame) == 64, "haptic_lab_gui_frame size changed");
static_assert(alignof(haptic_lab_gui_frame) == 4, "haptic_lab_gui_frame alignment changed");
static_assert(offsetof(haptic_lab_gui_frame, modelled_duration_ms) == 0, "modelled_duration_ms moved");
static_assert(offsetof(haptic_lab_gui_frame, block_trials) == 4, "block_trials moved");
static_assert(offsetof(haptic_lab_gui_frame, trials_total) == 6, "trials_total moved");
static_assert(offsetof(haptic_lab_gui_frame, sweep_index) == 8, "sweep_index moved");
static_assert(offsetof(haptic_lab_gui_frame, sweep_count) == 10, "sweep_count moved");
static_assert(offsetof(haptic_lab_gui_frame, screen) == 12, "screen moved");
static_assert(offsetof(haptic_lab_gui_frame, block) == 13, "block moved");
static_assert(offsetof(haptic_lab_gui_frame, block_count) == 14, "block_count moved");
static_assert(offsetof(haptic_lab_gui_frame, block_kind) == 15, "block_kind moved");
static_assert(offsetof(haptic_lab_gui_frame, calibration_mask) == 16, "calibration_mask moved");
static_assert(offsetof(haptic_lab_gui_frame, last_button) == 17, "last_button moved");
static_assert(offsetof(haptic_lab_gui_frame, feedback) == 18, "feedback moved");
static_assert(offsetof(haptic_lab_gui_frame, learn_index) == 19, "learn_index moved");
static_assert(offsetof(haptic_lab_gui_frame, learn_count) == 20, "learn_count moved");
static_assert(offsetof(haptic_lab_gui_frame, probe_index) == 21, "probe_index moved");
static_assert(offsetof(haptic_lab_gui_frame, probe_count) == 22, "probe_count moved");
static_assert(offsetof(haptic_lab_gui_frame, pattern_count) == 23, "pattern_count moved");
static_assert(offsetof(haptic_lab_gui_frame, message_id) == 24, "message_id moved");
static_assert(offsetof(haptic_lab_gui_frame, contact) == 25, "contact moved");
static_assert(offsetof(haptic_lab_gui_frame, masking) == 26, "masking moved");
static_assert(offsetof(haptic_lab_gui_frame, alt) == 27, "alt moved");
static_assert(offsetof(haptic_lab_gui_frame, name) == 54, "name moved");
#endif

#endif /* HAPTIC_LAB_GUI_H */
