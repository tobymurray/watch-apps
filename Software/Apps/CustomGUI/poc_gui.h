/**
 ******************************************************************************
 * @file    poc_gui.h
 * @brief   C ABI between the C++ CustomGUI shim and the Rust rendering core.
 *
 * The Rust staticlib (poc_gui) owns rendering only; it draws into a
 * caller-provided 8bpp ABGR2222 framebuffer. Everything else — display config,
 * pushing the buffer over the kernel bus, input, lifecycle — is the C++ shim.
 ******************************************************************************
 */
#ifndef POC_GUI_H
#define POC_GUI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of selectable screens (modulus for cycling on button press). */
uint32_t poc_gui_screen_count(void);

/**
 * Render one frame into an 8bpp ABGR2222 framebuffer.
 * @param buf     width*height bytes, row-major, 1 byte/pixel (ABGR2222).
 * @param width   pixels.
 * @param height  pixels.
 * @param screen  which UI screen to draw (cycled by the shim).
 * @param frame   monotonic frame counter, for animation.
 */
void poc_gui_render(uint8_t* buf, uint16_t width, uint16_t height,
                    uint32_t screen, uint32_t frame);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // POC_GUI_H
