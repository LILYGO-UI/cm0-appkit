#include <cm0/app.h>
#include <cm0/input.h>
#include <cm0/status_bar.h>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(CM0_APP_SIMULATOR)
#include <SDL.h>
#include <src/drivers/sdl/lv_sdl_window.h>
#else
#include <src/drivers/display/drm/lv_linux_drm.h>
#include <src/drivers/display/fb/lv_linux_fbdev.h>
#include <src/drivers/evdev/lv_evdev.h>
#endif

typedef struct app_runtime_options {
  const char *app_id;
  const char *display_backend;
  const char *fbdev_path;
  const char *drm_path;
  int64_t drm_connector_id;
  const char *input_path;
  const char *orientation;
  int width;
  int height;
} app_runtime_options_t;

typedef struct app_exit_gesture {
  lv_indev_t *input;
  lv_point_t start;
  int32_t minimum_start_y;
  int32_t required_distance;
  bool tracking;
} app_exit_gesture_t;

typedef struct app_input_state {
  lv_indev_t *keyboard;
  lv_group_t *group;
  volatile sig_atomic_t *running;
  bool hardware_keyboard;
  bool focus_suppressed;
  bool esc_pressed;
  bool enter_pressed;
  uint32_t keyboard_scan_at;
} app_input_state_t;

static constexpr int32_t kExitGestureMinStartZone = 48;
static constexpr int32_t kExitGestureMaxStartZone = 96;
static constexpr int32_t kExitGestureMinDistance = 72;
static constexpr int32_t kExitGestureMaxDistance = 120;
static constexpr uint32_t kPageBackgroundColor = 0xf2f2f7;
static constexpr uint32_t kKeyboardScanInterval = 500;

static volatile sig_atomic_t app_running;
static volatile sig_atomic_t display_alive;

static uint32_t tick_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint32_t)(now.tv_sec * 1000ULL +
                    (unsigned long long)now.tv_nsec / 1000000ULL);
}

static void lvgl_log(lv_log_level_t level, const char *message) {
  const char *level_name = level == LV_LOG_LEVEL_ERROR ? "error" : "warn";
  fprintf(stderr, "[appkit] [lvgl] [%s] %s", level_name,
          message ? message : "\n");
  if (message && message[0] && message[strlen(message) - 1] != '\n')
    fputc('\n', stderr);
}

static void stop_handler(int signal_number) {
  (void)signal_number;
  app_running = 0;
}

static void request_exit(void *user_data) {
  auto *running = static_cast<volatile sig_atomic_t *>(user_data);
  *running = 0;
}

static void collect_focusable(lv_obj_t *object, lv_group_t *group,
                              app_input_state_t *state);

static bool is_software_keyboard(lv_obj_t *object) {
  return object && lv_obj_has_class(object, &lv_keyboard_class);
}

static void hide_software_keyboards(lv_obj_t *object, lv_group_t *group) {
  if (!object) return;
  if (is_software_keyboard(object)) {
    /* LVGL represents all keys in this one button-matrix object. Only the
     * keyboard leaves the group; removing unrelated controls while walking
     * the tree can reenter this function through their focus callbacks. */
    if (group && lv_obj_get_group(object) == group) lv_group_remove_obj(object);
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  }
  const uint32_t children = lv_obj_get_child_count(object);
  for (uint32_t index = 0; index < children; ++index) {
    lv_obj_t *child = lv_obj_get_child(object, static_cast<int32_t>(index));
    hide_software_keyboards(child, group);
  }
}

static bool send_software_keyboard_event(lv_obj_t *object, lv_obj_t *textarea,
                                         lv_event_code_t code) {
  if (!object || !textarea) return false;
  if (is_software_keyboard(object) &&
      lv_keyboard_get_textarea(object) == textarea) {
    lv_obj_send_event(object, code, nullptr);
    /* The callback may hide or delete the keyboard. Do not walk its former
     * children after returning from the callback. */
    return true;
  }
  const uint32_t children = lv_obj_get_child_count(object);
  for (uint32_t index = 0; index < children; ++index) {
    if (send_software_keyboard_event(
            lv_obj_get_child(object, static_cast<int32_t>(index)), textarea,
            code))
      return true;
  }
  return false;
}

static bool notify_software_keyboard(lv_obj_t *textarea,
                                      lv_event_code_t code) {
  if (!textarea) return false;
  if (send_software_keyboard_event(lv_screen_active(), textarea, code)) return true;
  if (send_software_keyboard_event(lv_layer_bottom(), textarea, code)) return true;
  if (send_software_keyboard_event(lv_layer_top(), textarea, code)) return true;
  return send_software_keyboard_event(lv_layer_sys(), textarea, code);
}

static bool object_hidden_in_tree(lv_obj_t *object) {
  for (lv_obj_t *current = object; current;
       current = lv_obj_get_parent(current)) {
    if (lv_obj_has_flag(current, LV_OBJ_FLAG_HIDDEN)) return true;
  }
  return false;
}

static void focus_object_event(lv_event_t *event) {
  auto *state = static_cast<app_input_state_t *>(lv_event_get_user_data(event));
  lv_obj_t *object = lv_event_get_target_obj(event);
  if (!state || !state->group || !object || !lv_obj_is_editable(object) ||
      is_software_keyboard(object) || object_hidden_in_tree(object) ||
      lv_obj_has_state(object, LV_STATE_DISABLED))
    return;

  state->focus_suppressed = false;
  if (lv_obj_get_group(object) != state->group) {
    lv_group_add_obj(state->group, object);
  } else if (lv_group_get_focused(state->group) != object) {
    lv_group_focus_obj(object);
  }
  if (state->hardware_keyboard) {
    hide_software_keyboards(lv_screen_active(), state->group);
    hide_software_keyboards(lv_layer_bottom(), state->group);
    hide_software_keyboards(lv_layer_top(), state->group);
    hide_software_keyboards(lv_layer_sys(), state->group);
  }
}

static void watch_editable_object(lv_obj_t *object, app_input_state_t *state) {
  if (!object || !state || !lv_obj_is_editable(object)) return;
  const uint32_t event_count = lv_obj_get_event_count(object);
  for (uint32_t index = 0; index < event_count; ++index) {
    lv_event_dsc_t *descriptor = lv_obj_get_event_dsc(object, index);
    if (descriptor && lv_event_dsc_get_cb(descriptor) == focus_object_event &&
        lv_event_dsc_get_user_data(descriptor) == state)
      return;
  }
  lv_obj_add_event_cb(object, focus_object_event, LV_EVENT_FOCUSED, state);
}

static void hardware_key_event(lv_event_t *event) {
  auto *state = static_cast<app_input_state_t *>(lv_event_get_user_data(event));
  if (!state || !state->keyboard || !state->running || !state->group)
    return;

  const uint32_t key = lv_indev_get_key(state->keyboard);
  const lv_indev_state_t input_state = lv_indev_get_state(state->keyboard);
  if (input_state == LV_INDEV_STATE_RELEASED) {
    if (key == LV_KEY_ESC) state->esc_pressed = false;
    if (key == LV_KEY_ENTER) state->enter_pressed = false;
    return;
  }
  if (input_state != LV_INDEV_STATE_PRESSED) return;

  /* LVGL emits LV_EVENT_KEY repeatedly while a key is held. ESC changes the
   * group itself, so processing its repeat would immediately quit the app
   * after the first press had already left the focused field. */
  if (key == LV_KEY_ESC) {
    if (state->esc_pressed) return;
    state->esc_pressed = true;
  }
  /* The indev callback runs on every pressed sample, including polls before
   * LVGL's long-press threshold. Submit through the software keyboard once
   * per physical Enter press. */
  if (key == LV_KEY_ENTER) {
    if (state->enter_pressed) return;
    state->enter_pressed = true;
  }

  if (state->focus_suppressed &&
      (key == LV_KEY_LEFT || key == LV_KEY_RIGHT || key == LV_KEY_UP ||
       key == LV_KEY_DOWN || key == LV_KEY_ENTER)) {
    state->focus_suppressed = false;
    collect_focusable(lv_screen_active(), state->group, state);
    collect_focusable(lv_layer_top(), state->group, state);
  }
  if (key == LV_KEY_ENTER) {
    lv_obj_t *focused = lv_group_get_focused(state->group);
    if (focused && lv_obj_is_editable(focused) &&
        notify_software_keyboard(focused, LV_EVENT_READY)) {
      /* A software keyboard sends READY to both itself and its textarea. The
       * hardware path already sent READY to the keyboard above, so remove the
       * textarea briefly to avoid submitting twice when LVGL continues its
       * normal keypad routing. Restore focus for the next character. */
      if (lv_obj_is_valid(focused) &&
          lv_obj_get_group(focused) == state->group)
        lv_group_remove_obj(focused);
      if (lv_obj_is_valid(focused) && !object_hidden_in_tree(focused)) {
        lv_group_add_obj(state->group, focused);
        lv_group_focus_obj(focused);
      }
      lv_indev_stop_processing(state->keyboard);
    }
    return;
  }
  if (key != LV_KEY_ESC) return;

  lv_obj_t *focused = state->group ? lv_group_get_focused(state->group) : nullptr;
  if (focused && !object_hidden_in_tree(focused) &&
      (lv_obj_is_editable(focused) || is_software_keyboard(focused))) {
    /* Existing apps attach their cleanup callbacks to the LVGL keyboard
     * rather than to the textarea.  A hardware Esc has no keyboard button to
     * generate LV_EVENT_CANCEL, so mirror that part of LVGL's software
     * keyboard contract before dropping focus. */
    notify_software_keyboard(lv_obj_is_editable(focused)
                                 ? focused
                                 : lv_keyboard_get_textarea(focused),
                             LV_EVENT_CANCEL);
    /* Removing the whole focus set deliberately leaves the application with
     * no focused input. Focus collection stays suspended until the next touch
     * so the user can choose a new field. */
    lv_group_remove_all_objs(state->group);
    state->focus_suppressed = true;
  } else {
    /* LVGL's keypad path forwards LV_EVENT_KEY to the focused object after
     * the indev callback. Clear the group first so an application's focused
     * button cannot observe an Esc that belongs to the runtime exit action. */
    lv_group_remove_all_objs(state->group);
    *state->running = 0;
  }
  lv_indev_stop_processing(state->keyboard);
}

static void keyboard_deleted(lv_event_t *event) {
  auto *state = static_cast<app_input_state_t *>(lv_event_get_user_data(event));
  if (!state || lv_event_get_target(event) != state->keyboard) return;
  /* evdev deletes the old indev when a driver rebind removes its event node.
   * Never retain that pointer or a held Enter/Esc across a reconnect. */
  state->keyboard = nullptr;
  state->hardware_keyboard = false;
  state->esc_pressed = false;
  state->enter_pressed = false;
  state->keyboard_scan_at = lv_tick_get();
}

static void attach_keyboard(app_input_state_t *state, lv_indev_t *keyboard,
                             bool hardware_keyboard) {
  state->keyboard = keyboard;
  state->hardware_keyboard = keyboard && hardware_keyboard;
  state->esc_pressed = false;
  state->enter_pressed = false;
  if (!keyboard) return;
  state->focus_suppressed = false;
  lv_indev_set_group(keyboard, state->group);
  lv_indev_add_event_cb(keyboard, hardware_key_event, LV_EVENT_KEY, state);
  lv_indev_add_event_cb(keyboard, keyboard_deleted, LV_EVENT_DELETE, state);
}

static void pointer_focus_event(lv_event_t *event) {
  auto *state = static_cast<app_input_state_t *>(lv_event_get_user_data(event));
  if (!state || !state->group) return;
  state->focus_suppressed = false;
  /* ESC deliberately removes all group members.  Re-add the object under the
   * pointer before LVGL performs its normal click-to-focus step, otherwise a
   * textarea cannot be focused again after leaving it with ESC. */
  lv_obj_t *active = lv_indev_get_active_obj();
  if (active && lv_obj_is_editable(active) && !object_hidden_in_tree(active) &&
      !lv_obj_has_state(active, LV_STATE_DISABLED) &&
      lv_obj_get_group(active) != state->group)
    lv_group_add_obj(state->group, active);
  collect_focusable(lv_screen_active(), state->group, state);
  collect_focusable(lv_layer_top(), state->group, state);
}

static void collect_focusable(lv_obj_t *object, lv_group_t *group,
                              app_input_state_t *state) {
  if (!object || !group) return;
  if (object_hidden_in_tree(object)) {
    if (lv_obj_get_group(object) == group) lv_group_remove_obj(object);
    const uint32_t hidden_children = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < hidden_children; ++index)
      collect_focusable(lv_obj_get_child(object, static_cast<int32_t>(index)),
                        group, state);
    return;
  }
  watch_editable_object(object, state);
  const bool editable = lv_obj_is_editable(object);
  const bool user_focused = lv_obj_has_state(object, LV_STATE_FOCUSED);
  /* Group-def widgets (buttons, list items, etc.) are navigable from the
   * hardware keyboard immediately. Text fields are added only after a
   * pointer or app callback has focused them; otherwise the first text field
   * would be focused implicitly when the group is populated at startup. */
  if (!is_software_keyboard(object) &&
      (lv_obj_is_group_def(object) || (editable && user_focused)) &&
      !lv_obj_has_state(object, LV_STATE_DISABLED) &&
      lv_obj_get_group(object) == nullptr)
    lv_group_add_obj(group, object);
  const uint32_t children = lv_obj_get_child_count(object);
  for (uint32_t index = 0; index < children; ++index)
    collect_focusable(lv_obj_get_child(object, static_cast<int32_t>(index)),
                      group, state);
}

static void input_housekeeping(lv_timer_t *timer) {
  auto *state = static_cast<app_input_state_t *>(lv_timer_get_user_data(timer));
  if (!state || !state->group) return;
  if (!state->keyboard &&
      lv_tick_elaps(state->keyboard_scan_at) >= kKeyboardScanInterval) {
    state->keyboard_scan_at = lv_tick_get();
    bool present = false;
    lv_indev_t *keyboard = cm0_input_create_keyboard(&present);
    attach_keyboard(state, keyboard, present);
  }
  if (!state->focus_suppressed) {
    collect_focusable(lv_screen_active(), state->group, state);
    collect_focusable(lv_layer_top(), state->group, state);
  }
  if (!state->hardware_keyboard) return;

  /* Existing applications still create LVGL's software keyboard. Keep it
   * hidden whenever the TCA8418 is available, including after a view creates
   * a new keyboard or explicitly asks it to show. */
  hide_software_keyboards(lv_screen_active(), state->group);
  hide_software_keyboards(lv_layer_bottom(), state->group);
  hide_software_keyboards(lv_layer_top(), state->group);
  hide_software_keyboards(lv_layer_sys(), state->group);
}

static int32_t clamp_dimension(int32_t value, int32_t minimum,
                               int32_t maximum) {
  if (value < minimum)
    return minimum;
  if (value > maximum)
    return maximum;
  return value;
}

static void exit_gesture_input_event(lv_event_t *event) {
  auto *gesture =
      static_cast<app_exit_gesture_t *>(lv_event_get_user_data(event));
  if (!gesture || !gesture->input)
    return;

  switch (lv_event_get_code(event)) {
  case LV_EVENT_PRESSED: {
    lv_indev_get_point(gesture->input, &gesture->start);
    gesture->tracking = gesture->start.y >= gesture->minimum_start_y;
    break;
  }
  case LV_EVENT_RELEASED:
    gesture->tracking = false;
    break;
  case LV_EVENT_DELETE:
    gesture->input = NULL;
    gesture->tracking = false;
    break;
  default:
    break;
  }
}

static void exit_gesture_timer_tick(lv_timer_t *timer) {
  auto *gesture =
      static_cast<app_exit_gesture_t *>(lv_timer_get_user_data(timer));
  if (!gesture || !gesture->tracking || !gesture->input ||
      lv_indev_get_state(gesture->input) != LV_INDEV_STATE_PRESSED)
    return;

  lv_point_t current;
  lv_indev_get_point(gesture->input, &current);
  const int32_t upward = gesture->start.y - current.y;
  const int32_t horizontal = current.x >= gesture->start.x
                                 ? current.x - gesture->start.x
                                 : gesture->start.x - current.x;
  if (upward >= gesture->required_distance && upward > horizontal) {
    gesture->tracking = false;
    app_running = 0;
  }
}

static void display_deleted(lv_event_t *event) {
  (void)event;
  app_running = 0;
  display_alive = 0;
}

class AppDisplayRuntime {
public:
  AppDisplayRuntime() {
    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_log_register_print_cb(lvgl_log);
  }

  ~AppDisplayRuntime() {
    if (display_ && display_alive)
      lv_display_delete(display_);
#if defined(CM0_APP_SIMULATOR)
    if (display_)
      lv_sdl_quit();
#endif
    lv_log_register_print_cb(NULL);
    lilygo_ui_fonts_deinit();
    lv_deinit();
  }

  void set_display(lv_display_t *display) { display_ = display; }

private:
  lv_display_t *display_ = nullptr;
};

class AppSession {
public:
  explicit AppSession(const cm0_app_descriptor_t &descriptor)
      : descriptor_(descriptor) {}

  ~AppSession() {
    if (opened_ && descriptor_.close)
      descriptor_.close();
  }

  void open(const cm0_app_context_t &context) {
    descriptor_.open(&context);
    opened_ = true;
  }

private:
  const cm0_app_descriptor_t &descriptor_;
  bool opened_ = false;
};

#if defined(CM0_APP_SIMULATOR)
static void fit_simulator_window(lv_display_t *display, int width, int height) {
  SDL_Window *window = lv_sdl_window_get_window(display);
  int display_index = window ? SDL_GetWindowDisplayIndex(window) : -1;
  SDL_Rect usable_bounds;
  if (display_index < 0 ||
      SDL_GetDisplayUsableBounds(display_index, &usable_bounds) != 0)
    return;

  const float usable_fraction = 0.90f;
  float horizontal_zoom = usable_bounds.w * usable_fraction / (float)width;
  float vertical_zoom = usable_bounds.h * usable_fraction / (float)height;
  float zoom =
      horizontal_zoom < vertical_zoom ? horizontal_zoom : vertical_zoom;
  if (zoom >= 1.0f)
    return;

  lv_sdl_window_set_zoom(display, zoom);
  lv_display_set_resolution(display, width, height);
  SDL_SetWindowPosition(
      window, usable_bounds.x + (usable_bounds.w - (int)(width * zoom)) / 2,
      usable_bounds.y + (usable_bounds.h - (int)(height * zoom)) / 2);
}
#endif

static int parse_options(int argc, char **argv,
                         app_runtime_options_t *options) {
#if defined(CM0_APP_SIMULATOR)
  options->display_backend = "sdl";
#else
  options->display_backend = "drm";
#endif
  options->fbdev_path = "/dev/fb0";
  options->drm_path = "/dev/dri/card0";
  options->drm_connector_id = -1;
  options->input_path = "/dev/input/by-path/platform-3f205000.i2c-event";
  options->orientation = "portrait";
  options->width = 568;
  options->height = 1232;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--app-id") == 0 && i + 1 < argc)
      options->app_id = argv[++i];
    else if (strcmp(argv[i], "--app-name") == 0 && i + 1 < argc)
      ++i;
    else if (strcmp(argv[i], "--display") == 0 && i + 1 < argc)
      options->display_backend = argv[++i];
    else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)
      options->width = atoi(argv[++i]);
    else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
      options->height = atoi(argv[++i]);
    else if (strcmp(argv[i], "--fbdev") == 0 && i + 1 < argc)
      options->fbdev_path = argv[++i];
    else if (strcmp(argv[i], "--drm") == 0 && i + 1 < argc)
      options->drm_path = argv[++i];
    else if (strcmp(argv[i], "--drm-connector") == 0 && i + 1 < argc)
      options->drm_connector_id = strtoll(argv[++i], NULL, 10);
    else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc)
      options->input_path = argv[++i];
    else if (strcmp(argv[i], "--orientation") == 0 && i + 1 < argc)
      options->orientation = argv[++i];
    else
      return -1;
  }
  const bool valid_orientation =
      strcmp(options->orientation, "portrait") == 0 ||
      strcmp(options->orientation, "landscape") == 0;
  return options->width >= 320 && options->height >= 240 && valid_orientation
             ? 0
             : -1;
}

static void apply_display_orientation(lv_display_t *display,
                                      const app_runtime_options_t *options) {
  const bool landscape = strcmp(options->orientation, "landscape") == 0;
#if defined(CM0_APP_SIMULATOR)
  lv_display_set_resolution(display,
                            landscape ? options->height : options->width,
                            landscape ? options->width : options->height);
#else
  // Keep application rotation aligned with the Launcher: LVGL rotation 90
  // produces the same clockwise landscape orientation for both processes.
  const lv_display_rotation_t rotation = landscape ? LV_DISPLAY_ROTATION_90
                                                   : LV_DISPLAY_ROTATION_0;
  // Setting the same rotation still sends a resolution event in LVGL, which
  // needlessly rebuilds the DRM/EGL display during portrait startup.
  if (lv_display_get_rotation(display) != rotation)
    lv_display_set_rotation(display, rotation);
#endif
}

static lv_obj_t *create_app_root(void) {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(kPageBackgroundColor), 0);
  lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(screen, 36, 0);
  lv_obj_set_style_clip_corner(screen, true, 0);

  lv_obj_t *system_layer = lv_layer_top();
  lv_obj_t *status = cm0_status_bar_create(system_layer);
  if (!status)
    return nullptr;
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *content = lv_obj_create(screen);
  lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
  lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_pad_top(content, CM0_STATUS_BAR_HEIGHT, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_radius(content, 0, 0);
  lv_obj_set_style_bg_color(content, lv_color_hex(kPageBackgroundColor), 0);
  lv_obj_set_style_bg_grad_dir(content, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
  lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *home_indicator = cm0_home_indicator_create(system_layer);
  if (!home_indicator)
    return nullptr;
  lv_obj_set_size(home_indicator, LV_PCT(100), 38);
  lv_obj_align(home_indicator, LV_ALIGN_BOTTOM_MID, 0, -3);
  lv_obj_clear_flag(home_indicator,
                    static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE |
                                               LV_OBJ_FLAG_CLICKABLE));
  return content;
}

static lv_display_t *create_display(const app_runtime_options_t *options,
                                    lv_indev_t **input, lv_indev_t **keyboard,
                                    bool *keyboard_present) {
  *input = NULL;
  *keyboard = NULL;
  *keyboard_present = false;
#if defined(CM0_APP_SIMULATOR)
  if (strcmp(options->display_backend, "sdl") != 0)
    return NULL;
  SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
  lv_display_t *display = lv_sdl_window_create(options->width, options->height);
  if (display) {
    lv_sdl_window_set_title(display,
                            options->app_id ? options->app_id : "CM0 App");
    lv_sdl_window_set_resizeable(display, true);
    fit_simulator_window(display, options->width, options->height);
    *input = cm0_input_create_pointer(options->input_path);
    *keyboard = cm0_input_create_keyboard(keyboard_present);
  }
  return display;
#else
  lv_display_t *display = NULL;
  lv_result_t result = LV_RESULT_INVALID;
  if (strcmp(options->display_backend, "fbdev") == 0) {
    display = lv_linux_fbdev_create();
    if (display)
      result = lv_linux_fbdev_set_file(display, options->fbdev_path);
  } else if (strcmp(options->display_backend, "drm") == 0) {
    display = lv_linux_drm_create();
    if (display)
      result = lv_linux_drm_set_file(display, options->drm_path,
                                     options->drm_connector_id);
  }
  if (display && result != LV_RESULT_OK) {
    lv_display_delete(display);
    display = NULL;
  }
  if (display) {
    *input = cm0_input_create_pointer(options->input_path);
    *keyboard = cm0_input_create_keyboard(keyboard_present);
  }
  return display;
#endif
}

int cm0_app_run(int argc, char **argv, const cm0_app_descriptor_t *descriptor) {
  const uint32_t startup_started = tick_ms();
  app_runtime_options_t options{};
  if (parse_options(argc, argv, &options) != 0) {
    fprintf(stderr, "cm0-app: invalid runtime arguments\n");
    return 2;
  }
  if (!descriptor || descriptor->api_version != CM0_APP_API_VERSION ||
      descriptor->struct_size < sizeof(*descriptor) || !descriptor->id ||
      !descriptor->open ||
      (options.app_id && strcmp(options.app_id, descriptor->id) != 0)) {
    fprintf(stderr, "cm0-app: incompatible application descriptor\n");
    return 2;
  }

  app_exit_gesture_t exit_gesture{};
  // Input deletion callbacks run from the runtime's lv_deinit(), so their
  // state must outlive the runtime, including after a keyboard reconnect.
  app_input_state_t input_state{};
  AppDisplayRuntime runtime;
  const uint32_t runtime_ready = tick_ms();
  lv_indev_t *input = NULL;
  lv_indev_t *keyboard = NULL;
  bool keyboard_present = false;
  lv_display_t *display = create_display(&options, &input, &keyboard,
                                         &keyboard_present);
  if (!display) {
    fprintf(stderr, "cm0-app: cannot initialize %s display\n",
            options.display_backend);
    return 1;
  }
  apply_display_orientation(display, &options);
  const uint32_t display_ready = tick_ms();
  app_running = 1;
  display_alive = 1;
  runtime.set_display(display);
  lv_display_add_event_cb(display, display_deleted, LV_EVENT_DELETE, NULL);
  signal(SIGINT, stop_handler);
  signal(SIGTERM, stop_handler);
  // The default group and polling timer must exist even when the application
  // starts with no keyboard; a later connection uses the same focus set.
  lv_group_t *input_group = lv_group_create();
  if (input_group) lv_group_set_default(input_group);
  input_state.group = input_group;
  input_state.running = &app_running;
  input_state.keyboard_scan_at = lv_tick_get();
  attach_keyboard(&input_state, keyboard, keyboard_present);
  if (input && input_group)
    lv_indev_add_event_cb(input, pointer_focus_event, LV_EVENT_PRESSED,
                          &input_state);
  if (input) {
    const int32_t height = lv_display_get_vertical_resolution(display);
    exit_gesture.input = input;
    exit_gesture.minimum_start_y =
        height - clamp_dimension(height / 12, kExitGestureMinStartZone,
                                 kExitGestureMaxStartZone);
    exit_gesture.required_distance = clamp_dimension(
        height / 10, kExitGestureMinDistance, kExitGestureMaxDistance);
    lv_indev_add_event_cb(input, exit_gesture_input_event, LV_EVENT_PRESSED,
                          &exit_gesture);
    lv_indev_add_event_cb(input, exit_gesture_input_event, LV_EVENT_RELEASED,
                          &exit_gesture);
    lv_indev_add_event_cb(input, exit_gesture_input_event, LV_EVENT_DELETE,
                          &exit_gesture);
  }
  cm0_app_context_t context{create_app_root(), request_exit,
                            const_cast<sig_atomic_t *>(&app_running)};
  if (!context.root) {
    fprintf(stderr, "cm0-app: cannot create application chrome\n");
    if (keyboard) lv_indev_delete(keyboard);
    if (input) lv_indev_delete(input);
    if (input_group) {
      lv_group_set_default(NULL);
      lv_group_delete(input_group);
    }
    return 1;
  }
  AppSession session{*descriptor};
  const uint32_t chrome_ready = tick_ms();
  session.open(context);
  const uint32_t app_ready = tick_ms();
  lv_timer_t *exit_gesture_timer =
      input ? lv_timer_create(exit_gesture_timer_tick, 16, &exit_gesture)
            : NULL;
  lv_timer_t *input_timer = input_group ? lv_timer_create(input_housekeeping, 50, &input_state) : nullptr;
  if (input_timer) input_housekeeping(input_timer);
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(display);
  const uint32_t frame_ready = tick_ms();
  fprintf(stderr,
          "[appkit] UI ready app_id=%s first_frame_ms=%u runtime_ms=%u "
          "display_ms=%u chrome_ms=%u open_ms=%u render_ms=%u\n",
          descriptor->id, frame_ready - startup_started,
          runtime_ready - startup_started,
          display_ready - runtime_ready, chrome_ready - display_ready,
          app_ready - chrome_ready, frame_ready - app_ready);

  while (app_running) {
    uint32_t delay = lv_timer_handler();
    if (delay < 1)
      delay = 1;
    if (delay > 20)
      delay = 20;
    usleep(delay * 1000U);
  }

  if (exit_gesture_timer)
    lv_timer_delete(exit_gesture_timer);
  if (input_timer) lv_timer_delete(input_timer);
  if (input_group) {
    lv_group_set_default(NULL);
    lv_group_delete(input_group);
  }

  return 0;
}
