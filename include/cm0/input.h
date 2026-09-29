#ifndef CM0_INPUT_H
#define CM0_INPUT_H

#include <stdbool.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create the configured pointer device.  The caller owns the returned LVGL
 * input device and must delete it before deleting the display. */
lv_indev_t *cm0_input_create_pointer(const char *path);

/* Find the TCA8418 (or another full keyboard) event device.  On the SDL
 * simulator this creates the SDL keyboard device. */
lv_indev_t *cm0_input_create_keyboard(bool *present);

/* True when a keyboard event node with alphabetic keys is available. */
bool cm0_input_keyboard_present(void);

/* True only for a Linux event-node keyboard (the TCA8418 on CM0).  This is
 * false for the SDL simulator's host keyboard. */
bool cm0_input_hardware_keyboard_present(void);

#ifdef __cplusplus
}
#endif

#endif
