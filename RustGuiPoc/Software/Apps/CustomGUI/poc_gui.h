#ifndef POC_GUI_H
#define POC_GUI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float    accel_x_g;
    float    accel_y_g;
    float    accel_z_g;
    uint32_t sample_age_ms;
    uint32_t samples;
    uint32_t frames;
    uint8_t  valid;
    uint8_t  _pad[3];
} poc_gui_state;

uint32_t poc_gui_screen_count(void);

/* Called by the Rust panic handler. Must not return normally. */
void poc_gui_host_panic(const uint8_t* msg, uint32_t len);

void poc_gui_render(uint8_t* buf, uint32_t buf_len,
                    uint16_t width, uint16_t height,
                    uint32_t screen, const poc_gui_state* state);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // POC_GUI_H
