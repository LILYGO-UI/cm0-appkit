#ifndef CM0_STATUS_BAR_H
#define CM0_STATUS_BAR_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CM0_STATUS_BAR_HEIGHT 96

/* Creates a self-refreshing status bar owned by parent. */
lv_obj_t *cm0_status_bar_create(lv_obj_t *parent);

/* Refreshes time, Wi-Fi, and battery state immediately. */
void cm0_status_bar_refresh(lv_obj_t *status_bar);

/* Creates the shared home indicator view. */
lv_obj_t *cm0_home_indicator_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif
