#include <cm0/typography.h>
#include <lvgl.h>

#if LV_USE_LINUX_DRM
#include <src/drivers/display/drm/lv_linux_drm.h>
#endif
#if LV_USE_DRAW_OPENGLES || LV_USE_DRAW_NANOVG
#include <src/drivers/opengles/glad/include/glad/gles2.h>
#endif
#if LV_USE_DRAW_OPENGLES
#include <src/drivers/opengles/lv_opengles_texture.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <signal.h>
#include <time.h>
#include <vector>

namespace {

volatile sig_atomic_t running = 1;

struct Options {
  const char *drm = "/dev/dri/card0";
  const char *orientation = "portrait";
  const char *scene = "list";
  const char *output = nullptr;
  int64_t connector = -1;
  int seconds = 10;
  int cycles = 1;
  int snapshot_ms = -1;
  bool headless = false;
};

struct Metrics {
  bool enabled = false;
  bool rendered = false;
  double refresh_start = 0;
  const lv_draw_buf_t *last_buffer = nullptr;
  std::vector<double> refresh_ms;
};

struct Scene {
  lv_obj_t *list = nullptr;
  lv_obj_t *overlay = nullptr;
  int32_t scroll_range = 0;
  int32_t overlay_range = 0;
  uint32_t started_ms = 0;
  struct Tile {
    lv_obj_t *card = nullptr;
    lv_obj_t *arc = nullptr;
    lv_obj_t *value = nullptr;
    lv_obj_t *bar = nullptr;
    int32_t bar_range = 0;
  };
  std::vector<Tile> tiles;
};

double seconds_now(clockid_t clock = CLOCK_MONOTONIC) {
  timespec now{};
  if (clock_gettime(clock, &now) != 0) {
    std::perror("clock_gettime");
    std::exit(1);
  }
  return static_cast<double>(now.tv_sec) + now.tv_nsec / 1e9;
}

uint32_t tick_ms() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint32_t>(now.tv_sec * 1000ULL + now.tv_nsec / 1000000);
}

void stop_handler(int) { running = 0; }

void log_message(lv_log_level_t, const char *message) {
  if (message)
    std::fputs(message, stderr);
}

void usage(FILE *stream) {
  std::fputs(
      "Usage: lilygo-ui-graphics-benchmark [options]\n"
      "  --drm PATH                  DRM device (default /dev/dri/card0)\n"
      "  --connector ID              Connector ID (default -1: automatic)\n"
      "  --seconds N                 Measured seconds per cycle, 1..120 (10)\n"
      "  --cycles N                  Display create/delete cycles, 1..100 (1)\n"
      "  --orientation portrait|landscape\n"
      "  --scene list|dynamic        Cached scrolling or changing shapes (list)\n"
      "  --headless                  Software memory display, 568x1232\n"
      "  --output PATH               Final composed frame as a P6 PPM\n"
      "  --snapshot-ms N             Capture animation at N ms, 0..120000\n"
      "  --help\n",
      stream);
}

bool parse_number(const char *text, int64_t minimum, int64_t maximum,
                  int64_t &value) {
  char *end = nullptr;
  errno = 0;
  const long long parsed = std::strtoll(text, &end, 10);
  if (errno || end == text || *end || parsed < minimum || parsed > maximum)
    return false;
  value = parsed;
  return true;
}

bool parse_options(int argc, char **argv, Options &options) {
  for (int i = 1; i < argc; ++i) {
    const char *argument = argv[i];
    if (std::strcmp(argument, "--headless") == 0) {
      options.headless = true;
      continue;
    }
    if (std::strcmp(argument, "--help") == 0) {
      usage(stdout);
      std::exit(0);
    }
    if (++i == argc)
      return false;
    const char *value = argv[i];
    int64_t number = 0;
    if (std::strcmp(argument, "--drm") == 0)
      options.drm = value;
    else if (std::strcmp(argument, "--output") == 0)
      options.output = value;
    else if (std::strcmp(argument, "--orientation") == 0)
      options.orientation = value;
    else if (std::strcmp(argument, "--scene") == 0)
      options.scene = value;
    else if (std::strcmp(argument, "--snapshot-ms") == 0 &&
             parse_number(value, 0, 120000, number))
      options.snapshot_ms = static_cast<int>(number);
    else if (std::strcmp(argument, "--connector") == 0 &&
             parse_number(value, -1, UINT32_MAX, number))
      options.connector = number;
    else if (std::strcmp(argument, "--seconds") == 0 &&
             parse_number(value, 1, 120, number))
      options.seconds = static_cast<int>(number);
    else if (std::strcmp(argument, "--cycles") == 0 &&
             parse_number(value, 1, 100, number))
      options.cycles = static_cast<int>(number);
    else
      return false;
  }
  return (!std::strcmp(options.orientation, "portrait") ||
          !std::strcmp(options.orientation, "landscape")) &&
         (!std::strcmp(options.scene, "list") ||
          !std::strcmp(options.scene, "dynamic")) &&
         (options.snapshot_ms < 0 || options.output);
}

void memory_flush(lv_display_t *display, const lv_area_t *, uint8_t *) {
  lv_display_flush_ready(display);
}

lv_display_t *create_display(const Options &options,
                             std::vector<uint32_t> &pixels) {
  const bool landscape = !std::strcmp(options.orientation, "landscape");
  if (options.headless) {
    const int width = landscape ? 1232 : 568;
    const int height = landscape ? 568 : 1232;
    pixels.resize(static_cast<size_t>(width) * height);
    lv_display_t *display = lv_display_create(width, height);
    if (!display)
      return nullptr;
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers_with_stride(
        display, pixels.data(), nullptr,
        static_cast<uint32_t>(pixels.size() * sizeof(uint32_t)), width * 4,
        LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, memory_flush);
    return display;
  }
#if LV_USE_LINUX_DRM
  lv_display_t *display = lv_linux_drm_create();
  if (!display)
    return nullptr;
  if (lv_linux_drm_set_file(display, options.drm, options.connector) !=
      LV_RESULT_OK) {
    lv_display_delete(display);
    return nullptr;
  }
  // Match the runtime's clockwise 90-degree landscape orientation.
  lv_display_set_rotation(display, landscape ? LV_DISPLAY_ROTATION_270
                                             : LV_DISPLAY_ROTATION_0);
  return display;
#else
  std::fputs("This build has no DRM driver; use --headless.\n", stderr);
  return nullptr;
#endif
}

lv_obj_t *rectangle(lv_obj_t *parent, int x, int y, int width, int height,
                    uint32_t color, int radius = 0) {
  lv_obj_t *object = lv_obj_create(parent);
  lv_obj_remove_style_all(object);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_size(object, width, height);
  lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(object, radius, 0);
  return object;
}

lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width,
                uint32_t size, uint32_t color) {
  lv_obj_t *object = lv_label_create(parent);
  lv_label_set_text(object, text);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_width(object, width);
  lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(object, lilygo_ui_font_get(size), 0);
  lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
  return object;
}

void create_scene(lv_display_t *display, Scene &scene, const Options &options) {
  const int width = lv_display_get_horizontal_resolution(display);
  const int height = lv_display_get_vertical_resolution(display);
  lv_obj_t *screen = lv_display_get_screen_active(display);
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0xf2f4f5), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_text_font(screen, lilygo_ui_font_get(14), 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  label(screen, "09:41", 24, 14, 100, 22, 0x25282b);
  label(screen, LV_SYMBOL_WIFI "  85%", width - 140, 14, 120, 22, 0x25282b);
  const bool dynamic = !std::strcmp(options.scene, "dynamic");
  label(screen, dynamic ? "Live dashboard" : "Applications", 24, 66,
        width - 48, 36, 0x25282b);
  rectangle(screen, 24, 122, width - 48, 2, 0xd6dcdf);

  if (dynamic) {
    const int columns = width > height ? 3 : 2;
    const int rows = 6 / columns;
    const int tile_width = (width - 48 - (columns - 1) * 16) / columns;
    const int tile_height = (height - 164 - (rows - 1) * 16) / rows;
    const int arc_size = std::min(120, tile_height - 66);
    const char *names[] = {"Network", "Display", "Storage", "Battery",
                           "Messages", "Audio"};
    for (int i = 0; i < 6; ++i) {
      Scene::Tile tile;
      tile.card = rectangle(screen, 24 + (i % columns) * (tile_width + 16),
                            140 + (i / columns) * (tile_height + 16),
                            tile_width, tile_height, 0xffffff, 12);
      lv_obj_set_style_shadow_color(tile.card, lv_color_hex(0x526274), 0);
      lv_obj_set_style_shadow_opa(tile.card, LV_OPA_20, 0);
      lv_obj_set_style_shadow_width(tile.card, 12, 0);
      lv_obj_set_style_shadow_offset_y(tile.card, 3, 0);
      label(tile.card, names[i], 16, 10, tile_width - 32, 22, 0x25282b);
      tile.arc = lv_arc_create(tile.card);
      lv_obj_set_size(tile.arc, arc_size, arc_size);
      lv_obj_set_pos(tile.arc, (tile_width - arc_size) / 2, 40);
      lv_obj_remove_style(tile.arc, nullptr, LV_PART_KNOB);
      lv_obj_remove_flag(tile.arc, LV_OBJ_FLAG_CLICKABLE);
      lv_arc_set_range(tile.arc, 0, 100);
      lv_obj_set_style_arc_width(tile.arc, 9, LV_PART_MAIN);
      lv_obj_set_style_arc_width(tile.arc, 9, LV_PART_INDICATOR);
      lv_obj_set_style_arc_color(tile.arc, lv_color_hex(0xd6dcdf), LV_PART_MAIN);
      tile.value = label(tile.arc, "0%", 0, (arc_size - 28) / 2,
                         arc_size, 28, 0x25282b);
      lv_obj_set_style_text_align(tile.value, LV_TEXT_ALIGN_CENTER, 0);
      rectangle(tile.card, 16, tile_height - 20, tile_width - 32, 6,
                0xd6dcdf, 3);
      tile.bar = rectangle(tile.card, 16, tile_height - 20, 1, 6, 0x258468, 3);
      tile.bar_range = tile_width - 32;
      scene.tiles.push_back(tile);
    }
    scene.overlay = rectangle(screen, 24, height - 96, 224, 64, 0x258468, 8);
    lv_obj_set_style_opa(scene.overlay, LV_OPA_80, 0);
    label(scene.overlay, "Live updates", 16, 17, 192, 22, 0xffffff);
    lv_obj_update_layout(screen);
    scene.overlay_range = std::max(0, width - 272);
    scene.started_ms = tick_ms();
    return;
  }

  scene.list = rectangle(screen, 16, 140, width - 32, height - 176,
                         0xf2f4f5);
  lv_obj_add_flag(scene.list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(scene.list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(scene.list, LV_SCROLLBAR_MODE_OFF);
  const char *names[] = {"Files", "Settings", "Messages", "Music",
                         "Calendar", "Maps"};
  const char *details[] = {"Documents and recent downloads", "Network and display",
                           "Recent conversations", "Albums and playlists",
                           "Today and upcoming events", "Saved places"};
  const char *icons[] = {LV_SYMBOL_DIRECTORY, LV_SYMBOL_SETTINGS,
                         LV_SYMBOL_ENVELOPE, LV_SYMBOL_AUDIO,
                         LV_SYMBOL_LIST, LV_SYMBOL_GPS};
  const uint32_t colors[] = {0x258468, 0x526274, 0x376bba,
                             0xb84e77, 0xb17831, 0x287d91};
  for (int row = 0; row < 18; ++row) {
    const int index = row % 6;
    lv_obj_t *item = rectangle(scene.list, 0, row * 94, width - 32, 88,
                               0xffffff, 8);
    lv_obj_t *icon = rectangle(item, 16, 16, 56, 56, colors[index], 8);
    lv_obj_t *symbol = label(icon, icons[index], 0, 10, 56, 28, 0xffffff);
    lv_obj_set_style_text_align(symbol, LV_TEXT_ALIGN_CENTER, 0);
    label(item, names[index], 88, 12, width - 150, 28, 0x25282b);
    label(item, details[index], 88, 50, width - 150, 14, 0x687178);
  }
  scene.overlay = rectangle(screen, 24, height - 120, 224, 64, 0x258468, 8);
  lv_obj_set_style_opa(scene.overlay, LV_OPA_80, 0);
  label(scene.overlay, LV_SYMBOL_DOWNLOAD "  Downloads", 16, 17, 192, 22,
        0xffffff);
  lv_obj_update_layout(screen);
  scene.scroll_range = std::max(0, static_cast<int>(lv_obj_get_scroll_bottom(scene.list)));
  scene.overlay_range = std::max(0, width - 272);
  scene.started_ms = tick_ms();
}

int32_t triangle(uint32_t elapsed, uint32_t period, int32_t range) {
  const uint32_t phase = elapsed % period;
  const uint32_t distance = phase <= period / 2 ? phase : period - phase;
  return static_cast<int32_t>(static_cast<uint64_t>(distance) * range /
                              (period / 2));
}

void animate_scene(Scene *scene, uint32_t elapsed) {
  if (scene->list)
    lv_obj_scroll_to_y(scene->list, triangle(elapsed, 6000, scene->scroll_range),
                       LV_ANIM_OFF);
  for (size_t i = 0; i < scene->tiles.size(); ++i) {
    auto &tile = scene->tiles[i];
    const uint32_t phase = elapsed + static_cast<uint32_t>(i) * 531;
    const int32_t value = triangle(phase, 3200, 100);
    const lv_color_t color = lv_color_mix(lv_color_hex(0x376bba),
                                          lv_color_hex(0x258468),
                                          static_cast<uint8_t>(value * 255 / 100));
    lv_arc_set_value(tile.arc, value);
    lv_obj_set_style_arc_color(tile.arc, color, LV_PART_INDICATOR);
    lv_label_set_text_fmt(tile.value, "%d%%", static_cast<int>(value));
    lv_obj_set_width(tile.bar, std::max(1, tile.bar_range * value / 100));
    lv_obj_set_style_bg_color(tile.bar, color, 0);
    lv_obj_set_style_radius(tile.card, 4 + triangle(phase, 4000, 20), 0);
    lv_obj_set_style_shadow_width(tile.card, 8 + triangle(phase, 2800, 12), 0);
  }
  lv_obj_set_x(scene->overlay, 24 + triangle(elapsed, 4000, scene->overlay_range));
}

void animate(lv_timer_t *timer) {
  auto *scene = static_cast<Scene *>(lv_timer_get_user_data(timer));
  animate_scene(scene, tick_ms() - scene->started_ms);
}

void display_event(lv_event_t *event) {
  auto *metrics = static_cast<Metrics *>(lv_event_get_user_data(event));
  if (lv_event_get_code(event) == LV_EVENT_FLUSH_FINISH)
    metrics->last_buffer = lv_display_get_buf_active(
        static_cast<lv_display_t *>(lv_event_get_current_target(event)));
  if (!metrics->enabled)
    return;
  switch (lv_event_get_code(event)) {
  case LV_EVENT_REFR_START:
    metrics->refresh_start = seconds_now();
    metrics->rendered = false;
    break;
  case LV_EVENT_RENDER_START:
    metrics->rendered = true;
    break;
  case LV_EVENT_REFR_READY:
    if (metrics->rendered)
      metrics->refresh_ms.push_back((seconds_now() - metrics->refresh_start) * 1000);
    break;
  default:
    break;
  }
}

void run_until(double deadline) {
  while (running && seconds_now() < deadline) {
    const uint32_t delay = std::clamp(lv_timer_handler(), 1U, 10U);
    timespec request{0, static_cast<long>(delay) * 1000000};
    while (running && nanosleep(&request, &request) != 0 && errno == EINTR) {}
  }
}

bool write_ppm(const char *path, int width, int height, const uint8_t *pixels,
                size_t stride, bool flip_y) {
  FILE *file = std::fopen(path, "wb");
  if (!file) {
    std::perror(path);
    return false;
  }
  bool ok = std::fprintf(file, "P6\n%d %d\n255\n", width, height) > 0;
  std::vector<uint8_t> row(static_cast<size_t>(width) * 3);
  for (int y = 0; ok && y < height; ++y) {
    const int source_y = flip_y ? height - 1 - y : y;
    const auto *source = reinterpret_cast<const lv_color32_t *>(
        pixels + static_cast<size_t>(source_y) * stride);
    for (int x = 0; x < width; ++x) {
      row[x * 3] = source[x].red;
      row[x * 3 + 1] = source[x].green;
      row[x * 3 + 2] = source[x].blue;
    }
    ok = std::fwrite(row.data(), 1, row.size(), file) == row.size();
  }
  if (std::fclose(file) != 0)
    ok = false;
  if (!ok)
    std::fprintf(stderr, "Cannot write PPM: %s\n", path);
  return ok;
}

bool capture(lv_display_t *display, const char *path,
              const lv_draw_buf_t *last_buffer) {
  const int width = lv_display_get_horizontal_resolution(display);
  const int height = lv_display_get_vertical_resolution(display);
#if LV_USE_DRAW_OPENGLES
  (void)last_buffer;
  const GLuint texture = lv_opengles_texture_get_texture_id(display);
  if (!texture || !glGetString(GL_VERSION)) {
    std::fputs("No current OpenGL context or composed display texture.\n", stderr);
    return false;
  }
  GLint previous_framebuffer = 0;
  GLint previous_pack_alignment = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
  glGetIntegerv(GL_PACK_ALIGNMENT, &previous_pack_alignment);
  const GLenum previous_error = glGetError();
  if (previous_error != GL_NO_ERROR) {
    std::fprintf(stderr, "OpenGL error before capture: 0x%x\n", previous_error);
    return false;
  }
  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
  if (status == GL_FRAMEBUFFER_COMPLETE) {
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  }
  const GLenum error = glGetError();
  glPixelStorei(GL_PACK_ALIGNMENT, previous_pack_alignment);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
  glDeleteFramebuffers(1, &framebuffer);
  if (status != GL_FRAMEBUFFER_COMPLETE || error != GL_NO_ERROR) {
    std::fprintf(stderr, "GPU capture failed: framebuffer=0x%x, error=0x%x\n",
                 status, error);
    return false;
  }
  // LVGL's composed texture keeps BGRA channels until scanout's red/blue swap.
  return write_ppm(path, width, height, pixels.data(), width * 4, true);
#elif LV_USE_DRAW_NANOVG
  (void)last_buffer;
  if (!glGetString(GL_VERSION)) {
    std::fputs("No current NanoVG OpenGL context.\n", stderr);
    return false;
  }
  const lv_display_rotation_t rotation = lv_display_get_rotation(display);
  const bool rotated = rotation == LV_DISPLAY_ROTATION_90 ||
                       rotation == LV_DISPLAY_ROTATION_270;
  const int native_width = rotated ? height : width;
  const int native_height = rotated ? width : height;
  GLint previous_framebuffer = 0;
  GLint previous_pack_alignment = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
  glGetIntegerv(GL_PACK_ALIGNMENT, &previous_pack_alignment);
  const GLenum previous_error = glGetError();
  if (previous_error != GL_NO_ERROR) {
    std::fprintf(stderr, "OpenGL error before NanoVG capture: 0x%x\n", previous_error);
    return false;
  }
  // NanoVG renders into the EGL window framebuffer; read before eglSwapBuffers.
  std::vector<uint8_t> native_pixels(static_cast<size_t>(width) * height * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, native_width, native_height, GL_RGBA, GL_UNSIGNED_BYTE,
               native_pixels.data());
  const GLenum error = glGetError();
  glPixelStorei(GL_PACK_ALIGNMENT, previous_pack_alignment);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
  if (error != GL_NO_ERROR) {
    std::fprintf(stderr, "NanoVG capture failed: error=0x%x\n", error);
    return false;
  }
  std::vector<uint8_t> pixels(native_pixels.size());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int native_x = x;
      int native_y = y;
      switch (rotation) {
      case LV_DISPLAY_ROTATION_90:
        native_x = y;
        native_y = native_height - 1 - x;
        break;
      case LV_DISPLAY_ROTATION_180:
        native_x = native_width - 1 - x;
        native_y = native_height - 1 - y;
        break;
      case LV_DISPLAY_ROTATION_270:
        native_x = native_width - 1 - y;
        native_y = x;
        break;
      default:
        break;
      }
      const size_t source =
          (static_cast<size_t>(native_height - 1 - native_y) * native_width + native_x) * 4;
      auto *pixel = reinterpret_cast<lv_color32_t *>(
          pixels.data() + (static_cast<size_t>(y) * width + x) * 4);
      pixel->red = native_pixels[source];
      pixel->green = native_pixels[source + 1];
      pixel->blue = native_pixels[source + 2];
      pixel->alpha = native_pixels[source + 3];
    }
  }
  return write_ppm(path, width, height, pixels.data(), width * 4, false);
#else
  const lv_draw_buf_t *buffer = last_buffer;
  const lv_color_format_t format = lv_display_get_color_format(display);
  if (!buffer || !buffer->data ||
      (format != LV_COLOR_FORMAT_XRGB8888 && format != LV_COLOR_FORMAT_ARGB8888) ||
      buffer->header.w < static_cast<uint32_t>(width) ||
      buffer->header.h < static_cast<uint32_t>(height)) {
    std::fputs("Capture requires a full-size 32-bit software draw buffer.\n", stderr);
    return false;
  }
  return write_ppm(path, width, height, buffer->data, buffer->header.stride, false);
#endif
}

#if LV_USE_DRAW_NANOVG
struct CaptureRequest {
  const char *path;
  bool called = false;
  bool succeeded = false;
};

void capture_before_swap(lv_event_t *event) {
  auto *request = static_cast<CaptureRequest *>(lv_event_get_user_data(event));
  auto *display = static_cast<lv_display_t *>(lv_event_get_current_target(event));
  if (!request->called && lv_display_flush_is_last(display)) {
    request->called = true;
    request->succeeded = capture(display, request->path, nullptr);
  }
}
#endif

void report(const Options &options, int cycle, int width, int height,
             Metrics &metrics, double wall, double cpu) {
  auto &samples = metrics.refresh_ms;
  const double total = std::accumulate(samples.begin(), samples.end(), 0.0);
  std::sort(samples.begin(), samples.end());
  const size_t count = samples.size();
  const double p95 = count ? samples[(count * 95 + 99) / 100 - 1] : 0;
  const double p99 = count ? samples[(count * 99 + 99) / 100 - 1] : 0;
#if LV_USE_DRAW_NANOVG
  const char *renderer = "nanovg";
#elif LV_USE_DRAW_OPENGLES
  const char *renderer = "opengles";
#else
  const char *renderer = "software";
#endif
  std::printf(
      "{\"benchmark\":\"%s\",\"scene\":\"%s\",\"renderer\":\"%s\","
      "\"display\":\"%s\",\"orientation\":\"%s\",\"cycle\":%d,"
      "\"width\":%d,\"height\":%d,\"warmup_seconds\":1,"
      "\"requested_seconds\":%d,\"wall_seconds\":%.6f,\"cpu_seconds\":%.6f,"
      "\"cpu_percent_one_core\":%.3f,\"frames\":%zu,\"refresh_fps\":%.3f,"
      "\"refresh_mean_ms\":%.3f,\"refresh_p95_ms\":%.3f,\"refresh_p99_ms\":%.3f,"
      "\"refresh_max_ms\":%.3f,"
      "\"interrupted\":%s}\n",
      !std::strcmp(options.scene, "list") ? "lvgl-app-list-v1" : "lvgl-dynamic-v1",
      options.scene, renderer, options.headless ? "headless" : "drm", options.orientation,
      cycle, width, height, options.seconds, wall, cpu,
      wall > 0 ? cpu * 100 / wall : 0, count, wall > 0 ? count / wall : 0,
      count ? total / count : 0, p95, p99, count ? samples.back() : 0,
      running ? "false" : "true");
  std::fflush(stdout);
}

} // namespace

int main(int argc, char **argv) {
  Options options;
  if (!parse_options(argc, argv, options)) {
    usage(stderr);
    std::fputs("Invalid arguments.\n", stderr);
    return 2;
  }
#if LV_USE_DRAW_OPENGLES || LV_USE_DRAW_NANOVG
  if (options.headless) {
    std::fputs("Headless rendering requires a software build.\n", stderr);
    return 2;
  }
#endif
  signal(SIGINT, stop_handler);
  signal(SIGTERM, stop_handler);
  lv_init();
  lv_tick_set_cb(tick_ms);
  lv_log_register_print_cb(log_message);
  int result = 0;
  for (int cycle = 1; running && cycle <= options.cycles; ++cycle) {
    std::vector<uint32_t> pixels;
    lv_display_t *display = create_display(options, pixels);
    if (!display) {
      std::fputs("Cannot initialize benchmark display.\n", stderr);
      result = 1;
      break;
    }
    const int width = lv_display_get_horizontal_resolution(display);
    const int height = lv_display_get_vertical_resolution(display);
    Scene scene;
    create_scene(display, scene, options);
    Metrics metrics;
    metrics.refresh_ms.reserve(static_cast<size_t>(options.seconds) * 240);
    lv_display_add_event_cb(display, display_event, LV_EVENT_ALL, &metrics);
    lv_timer_t *animation = lv_timer_create(animate, 16, &scene);
    run_until(seconds_now() + 1.0);
    const double wall_start = seconds_now();
    const double cpu_start = seconds_now(CLOCK_PROCESS_CPUTIME_ID);
    metrics.enabled = true;
    run_until(wall_start + options.seconds);
    metrics.enabled = false;
    const double cpu = seconds_now(CLOCK_PROCESS_CPUTIME_ID) - cpu_start;
    const double wall = seconds_now() - wall_start;
    report(options, cycle, width, height, metrics, wall, cpu);
    if (metrics.refresh_ms.empty() && running) {
      std::fputs("No rendered frames were observed.\n", stderr);
      result = 1;
    }
    lv_timer_delete(animation);
    if (options.output && (cycle == options.cycles || !running)) {
      if (options.snapshot_ms >= 0)
        animate_scene(&scene, static_cast<uint32_t>(options.snapshot_ms));
#if LV_USE_DRAW_NANOVG
      CaptureRequest request{options.output};
      lv_display_add_event_cb(display, capture_before_swap, LV_EVENT_FLUSH_START,
                              &request);
#endif
      // Capture work is excluded from metrics. Redraw also makes NanoVG's window
      // back buffer defined before reading it; post-swap contents are undefined.
      lv_obj_invalidate(lv_display_get_screen_active(display));
      lv_refr_now(display);
#if LV_USE_DRAW_NANOVG
      lv_display_remove_event_cb_with_user_data(display, capture_before_swap, &request);
      if (!request.called || !request.succeeded)
        result = 1;
#else
      if (!capture(display, options.output, metrics.last_buffer))
        result = 1;
#endif
    }
    lv_display_delete(display);
    if (result)
      break;
  }
  lilygo_ui_fonts_deinit();
  lv_log_register_print_cb(nullptr);
  lv_deinit();
  return running ? result : 130;
}
