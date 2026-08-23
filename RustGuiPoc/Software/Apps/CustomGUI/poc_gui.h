/**
 ******************************************************************************
 * @file    poc_gui.h
 * @brief   C ABI between the C++ CustomGUI shim and the Rust rendering core.
 *
 * The Rust staticlib (poc_gui) owns rendering only; it draws into a
 * caller-provided 8bpp ABGR2222 framebuffer. Everything else — display config,
 * pushing the buffer over the kernel bus, input, lifecycle, and talking to the
 * Service — is the C++ shim.
 *
 * render() is a pure function of (buffer, geometry, screen, state). That is the
 * load-bearing property of this seam, not an accident of style: it means the
 * device, the host simulator and the unit tests all drive the *same* renderer
 * through the *same* struct, and nothing has a private path to the truth.
 ******************************************************************************
 */
#ifndef POC_GUI_H
#define POC_GUI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Everything the UI is allowed to know, as one plain struct.
 *
 * Layout must match `poc_gui::State` in rust/src/lib.rs. Kept to fixed-width
 * types with no padding surprises (3x4 + 3x4 + 1 + 3 pad = 28 bytes).
 */
typedef struct {
    float    accel_x;        /**< Acceleration, g. Meaningless unless `valid`. */
    float    accel_y;
    float    accel_z;
    uint32_t sample_age_ms;  /**< Since the newest sample arrived. */
    uint32_t samples;        /**< Samples received this session (diagnostics). */
    uint32_t frames;         /**< Frames rendered this session (diagnostics). */
    uint8_t  valid;          /**< 0 = never sampled, or too stale to show. */
    uint8_t  _pad[3];
} poc_gui_state;

/** Number of selectable screens (modulus for cycling on button press). */
uint32_t poc_gui_screen_count(void);

/**
 * Render one frame into an 8bpp ABGR2222 framebuffer.
 *
 * @param buf      framebuffer, 1 byte/pixel (ABGR2222), row-major.
 * @param buf_len  bytes available at @p buf. Passed explicitly so the size
 *                 invariant is checked where the writing happens, instead of
 *                 being a promise the caller makes and the callee trusts.
 * @param width    pixels.
 * @param height   pixels.
 * @param screen   which UI screen to draw (cycled by the shim).
 * @param state    current values to display. Must not be NULL.
 */
void poc_gui_render(uint8_t* buf, uint32_t buf_len,
                    uint16_t width, uint16_t height,
                    uint32_t screen, const poc_gui_state* state);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // POC_GUI_H
