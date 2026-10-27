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

/* Find the canonical TCA8418 evdev keyboard. On the SDL simulator this
 * creates the SDL keyboard device. Linux requires the event device name to
 * identify TCA8418 and the standard navigation/text capability set. */
lv_indev_t *cm0_input_create_keyboard(bool *present);

/* True when the canonical TCA8418 event node is available. */
bool cm0_input_keyboard_present(void);

/* True only for a Linux event-node keyboard (the TCA8418 on CM0).  This is
 * false for the SDL simulator's host keyboard. */
bool cm0_input_hardware_keyboard_present(void);

/* Confine runtime keyboard navigation to this subtree while it is visible.
 * Use on modal overlays/full-screen detail views. Hide or delete the object
 * to restore the enclosing focus scope. The frontmost visible scope wins. */
void cm0_input_set_focus_scope(lv_obj_t *object);

/* Make an overflowing scrollable object a keyboard browsing region. Arrows
 * navigate visible controls and scroll incrementally through its contents.
 * At a scroll boundary, arrows can move to adjacent controls. Tab remains
 * available to enter and leave the region directly. */
void cm0_input_set_scroll_region(lv_obj_t *object);

#ifdef __cplusplus
}
#endif

#endif
