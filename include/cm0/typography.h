#ifndef CM0_TYPOGRAPHY_H
#define CM0_TYPOGRAPHY_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

const lv_font_t *lilygo_ui_font_get(uint32_t size);
void lilygo_ui_fonts_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
