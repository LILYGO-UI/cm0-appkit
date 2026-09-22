#include <cm0/app.h>
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

static constexpr int32_t kExitGestureMinStartZone = 48;
static constexpr int32_t kExitGestureMaxStartZone = 96;
static constexpr int32_t kExitGestureMinDistance = 72;
static constexpr int32_t kExitGestureMaxDistance = 120;
static constexpr uint32_t kPageBackgroundColor = 0xf2f2f7;

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
  // LVGL rotates rendered pixels counterclockwise, so 270 means clockwise 90.
  const lv_display_rotation_t rotation = landscape ? LV_DISPLAY_ROTATION_270
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
                                    lv_indev_t **input) {
  *input = NULL;
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
    *input = lv_sdl_mouse_create();
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
  if (display)
    *input = lv_evdev_create(LV_INDEV_TYPE_POINTER, options->input_path);
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
  AppDisplayRuntime runtime;
  const uint32_t runtime_ready = tick_ms();
  lv_indev_t *input = NULL;
  lv_display_t *display = create_display(&options, &input);
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
    return 1;
  }
  AppSession session{*descriptor};
  const uint32_t chrome_ready = tick_ms();
  session.open(context);
  const uint32_t app_ready = tick_ms();
  lv_timer_t *exit_gesture_timer =
      input ? lv_timer_create(exit_gesture_timer_tick, 16, &exit_gesture)
            : NULL;
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

  return 0;
}
