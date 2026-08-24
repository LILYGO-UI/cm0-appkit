#include <cm0/status_bar.h>

#include <cm0/system_status.h>
#include <cm0/typography.h>

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

LV_IMAGE_DECLARE(cm0_status_icon_ethernet);
LV_IMAGE_DECLARE(cm0_status_icon_keyboard);
LV_IMAGE_DECLARE(cm0_status_icon_wifi_high);
LV_IMAGE_DECLARE(cm0_status_icon_wifi_mid);
LV_IMAGE_DECLARE(cm0_status_icon_wifi_low);
LV_IMAGE_DECLARE(cm0_status_icon_wifi_zero);
LV_IMAGE_DECLARE(cm0_status_icon_wifi_no);

namespace {

struct StatusBarState {
  lv_obj_t *clock = nullptr;
  lv_obj_t *ethernet = nullptr;
  lv_obj_t *keyboard = nullptr;
  lv_obj_t *wifi_no = nullptr;
  lv_obj_t *wifi_zero = nullptr;
  lv_obj_t *wifi_low = nullptr;
  lv_obj_t *wifi_mid = nullptr;
  lv_obj_t *wifi_high = nullptr;
  lv_obj_t *battery_bar = nullptr;
  lv_obj_t *battery_label = nullptr;
  lv_timer_t *timer = nullptr;
};

const char *environment_path(const char *name, const char *default_path) {
  const char *value = getenv(name);
  return value && value[0] ? value : default_path;
}

void make_clear(lv_obj_t *object) {
  lv_obj_remove_style_all(object);
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_shadow_width(object, 0, 0);
  lv_obj_set_style_pad_all(object, 0, 0);
}

lv_obj_t *create_status_image(lv_obj_t *parent, const char *name,
                              const lv_image_dsc_t *source, bool hidden) {
  lv_obj_t *image = lv_image_create(parent);
  lv_obj_set_name(image, name);
  lv_image_set_src(image, source);
  lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_flag(image, LV_OBJ_FLAG_HIDDEN, hidden);
  return image;
}

lv_obj_t *create_wifi_image(lv_obj_t *parent, const char *name,
                            const lv_image_dsc_t *source, bool hidden) {
  lv_obj_t *image = create_status_image(parent, name, source, hidden);
  lv_obj_center(image);
  return image;
}

lv_obj_t *create_status_bar_view(lv_obj_t *parent, StatusBarState *state) {
  lv_obj_t *status_bar = lv_obj_create(parent);
  lv_obj_set_name_static(status_bar, "status_bar");
  lv_obj_remove_style_all(status_bar);
  lv_obj_set_size(status_bar, LV_PCT(100), CM0_STATUS_BAR_HEIGHT);
  lv_obj_set_style_pad_left(status_bar, 9, 0);
  lv_obj_set_style_pad_right(status_bar, 9, 0);
  lv_obj_set_style_pad_top(status_bar, 2, 0);
  lv_obj_set_style_pad_bottom(status_bar, 0, 0);
  lv_obj_set_style_pad_column(status_bar, 0, 0);
  lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(status_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *time_container = lv_obj_create(status_bar);
  lv_obj_set_name_static(time_container, "cm0_status_bar_time_container");
  make_clear(time_container);
  lv_obj_set_height(time_container, CM0_STATUS_BAR_HEIGHT);
  lv_obj_set_flex_grow(time_container, 1);
  lv_obj_set_style_pad_right(time_container, 6, 0);

  state->clock = lv_label_create(time_container);
  lv_obj_set_name_static(state->clock, "cm0_status_bar_clock");
  lv_label_set_text(state->clock, "9:41");
  lv_obj_set_style_text_font(state->clock, lilygo_ui_font_get(22), 0);
  lv_obj_set_style_text_color(state->clock, lv_color_hex(0x000000), 0);
  lv_obj_center(state->clock);

  lv_obj_t *spacer = lv_obj_create(status_bar);
  lv_obj_set_name_static(spacer, "cm0_status_bar_spacer");
  make_clear(spacer);
  lv_obj_set_height(spacer, CM0_STATUS_BAR_HEIGHT);
  lv_obj_set_flex_grow(spacer, 1);

  lv_obj_t *levels = lv_obj_create(status_bar);
  lv_obj_set_name_static(levels, "cm0_status_bar_levels");
  make_clear(levels);
  lv_obj_set_height(levels, CM0_STATUS_BAR_HEIGHT);
  lv_obj_set_flex_grow(levels, 1);
  lv_obj_set_flex_flow(levels, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(levels, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(levels, 7, 0);

  state->keyboard = create_status_image(
      levels, "cm0_status_bar_keyboard", &cm0_status_icon_keyboard, true);
  state->ethernet = create_status_image(
      levels, "cm0_status_bar_ethernet", &cm0_status_icon_ethernet, true);

  lv_obj_t *wifi = lv_obj_create(levels);
  lv_obj_set_name_static(wifi, "cm0_status_bar_wifi");
  make_clear(wifi);
  lv_obj_set_size(wifi, 21, 21);
  lv_obj_remove_flag(wifi, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(wifi, LV_OBJ_FLAG_SCROLLABLE);
  state->wifi_no = create_wifi_image(wifi, "cm0_status_bar_wifi_no",
                                     &cm0_status_icon_wifi_no, true);
  state->wifi_zero = create_wifi_image(wifi, "cm0_status_bar_wifi_zero",
                                       &cm0_status_icon_wifi_zero, true);
  state->wifi_low = create_wifi_image(wifi, "cm0_status_bar_wifi_low",
                                      &cm0_status_icon_wifi_low, true);
  state->wifi_mid = create_wifi_image(wifi, "cm0_status_bar_wifi_mid",
                                      &cm0_status_icon_wifi_mid, true);
  state->wifi_high = create_wifi_image(wifi, "cm0_status_bar_wifi_high",
                                       &cm0_status_icon_wifi_high, false);

  lv_obj_t *battery = lv_obj_create(levels);
  lv_obj_set_name_static(battery, "cm0_status_bar_battery");
  make_clear(battery);
  lv_obj_set_size(battery, 45, 21);
  lv_obj_set_flex_flow(battery, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(battery, 0, 0);
  lv_obj_set_flex_align(battery, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_margin_right(battery, 8, 0);
  lv_obj_remove_flag(battery, LV_OBJ_FLAG_SCROLLABLE);

  state->battery_bar = lv_bar_create(battery);
  lv_obj_set_name_static(state->battery_bar,
                         "cm0_status_bar_battery_level");
  lv_obj_remove_style_all(state->battery_bar);
  lv_obj_set_size(state->battery_bar, 40, 21);
  lv_obj_align(state->battery_bar, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(state->battery_bar, lv_color_hex(0xa5a5a5),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(state->battery_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(state->battery_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(state->battery_bar, 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(state->battery_bar, lv_color_hex(0x000000),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(state->battery_bar, LV_OPA_COVER,
                          LV_PART_INDICATOR);
  lv_obj_set_style_radius(state->battery_bar, 4, LV_PART_INDICATOR);
  lv_bar_set_range(state->battery_bar, 0, 100);
  lv_bar_set_value(state->battery_bar, 82, LV_ANIM_OFF);

  state->battery_label = lv_label_create(state->battery_bar);
  lv_obj_set_name_static(state->battery_label, "cm0_status_bar_battery_text");
  lv_label_set_text(state->battery_label, "82");
  lv_obj_set_style_text_font(state->battery_label, lilygo_ui_font_get(22), 0);
  lv_obj_set_style_text_color(state->battery_label, lv_color_hex(0xffffff), 0);
  lv_obj_center(state->battery_label);

  lv_obj_t *terminal = lv_obj_create(battery);
  lv_obj_set_name_static(terminal, "cm0_status_bar_battery_terminal");
  lv_obj_remove_style_all(terminal);
  lv_obj_set_size(terminal, 2, 7);
  lv_obj_set_style_border_width(terminal, 0, 0);
  lv_obj_set_style_radius(terminal, 2, 0);
  lv_obj_set_style_margin_left(terminal, 1, 0);
  lv_obj_set_style_bg_color(terminal, lv_color_hex(0xa5a5a5), 0);
  lv_obj_set_style_bg_opa(terminal, LV_OPA_COVER, 0);
  return status_bar;
}

StatusBarState *state_for(lv_obj_t *status_bar) {
  return status_bar
             ? static_cast<StatusBarState *>(lv_obj_get_user_data(status_bar))
             : nullptr;
}

void show_only(lv_obj_t *selected, StatusBarState *state) {
  lv_obj_t *icons[] = {state->wifi_no, state->wifi_zero, state->wifi_low,
                       state->wifi_mid, state->wifi_high};
  for (lv_obj_t *icon : icons)
    lv_obj_set_flag(icon, LV_OBJ_FLAG_HIDDEN, icon != selected);
}

void refresh(StatusBarState *state) {
  if (!state)
    return;

  char clock_text[8] = "--:--";
  time_t now = time(nullptr);
  struct tm local_time;
  if (localtime_r(&now, &local_time))
    strftime(clock_text, sizeof(clock_text), "%H:%M", &local_time);
  lv_label_set_text(state->clock, clock_text);

  cm0_system_status_t status{};
  cm0_system_status_read_with_input(
      environment_path("CM0_NETWORK_DIR", "/sys/class/net"),
      environment_path("CM0_POWER_SUPPLY_DIR", "/sys/class/power_supply"),
      environment_path("CM0_INPUT_DIR", "/sys/class/input"), &status);
  lv_obj_set_flag(state->ethernet, LV_OBJ_FLAG_HIDDEN, !status.ethernet_has_ip);
  lv_obj_set_flag(state->keyboard, LV_OBJ_FLAG_HIDDEN,
                  !status.keyboard_present);

  if (!status.wifi_present)
    show_only(state->wifi_no, state);
  else if (!status.wifi_connected)
    show_only(state->wifi_zero, state);
  else
    show_only(state->wifi_high, state);

  const int capacity = status.battery_present && status.battery_capacity >= 0
                           ? status.battery_capacity
                           : 0;
  char capacity_text[12];
  snprintf(capacity_text, sizeof(capacity_text), "%d", capacity);
  lv_bar_set_value(state->battery_bar, capacity, LV_ANIM_OFF);
  lv_label_set_text(state->battery_label, capacity_text);
}

void timer_tick(lv_timer_t *timer) {
  refresh(static_cast<StatusBarState *>(lv_timer_get_user_data(timer)));
}

void status_bar_deleted(lv_event_t *event) {
  auto *state = static_cast<StatusBarState *>(lv_event_get_user_data(event));
  if (!state)
    return;
  if (state->timer)
    lv_timer_delete(state->timer);
  delete state;
}

} // namespace

extern "C" lv_obj_t *cm0_status_bar_create(lv_obj_t *parent) {
  if (!parent)
    return nullptr;
  auto *state = new (std::nothrow) StatusBarState;
  if (!state)
    return nullptr;
  lv_obj_t *status_bar = create_status_bar_view(parent, state);
  if (!status_bar) {
    delete state;
    return nullptr;
  }
  lv_obj_set_user_data(status_bar, state);
  lv_obj_add_event_cb(status_bar, status_bar_deleted, LV_EVENT_DELETE, state);
  refresh(state);
  state->timer = lv_timer_create(timer_tick, 1000, state);
  return status_bar;
}

extern "C" void cm0_status_bar_refresh(lv_obj_t *status_bar) {
  refresh(state_for(status_bar));
}

extern "C" lv_obj_t *cm0_home_indicator_create(lv_obj_t *parent) {
  if (!parent)
    return nullptr;
  lv_obj_t *indicator = lv_obj_create(parent);
  lv_obj_remove_style_all(indicator);
  lv_obj_set_size(indicator, LV_PCT(100), 38);

  lv_obj_t *bar = lv_obj_create(indicator);
  lv_obj_set_name_static(bar, "cm0_home_indicator_bar");
  lv_obj_set_size(bar, 120, 6);
  lv_obj_center(bar);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(bar, 3, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x1c1c1e), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  return indicator;
}
