// Exercise the private input callbacks against real LVGL objects. Including
// the implementation keeps these details out of the public SDK API; the
// runtime archive supplies its dependencies without extracting runtime.cpp.
#include <cm0/input.h>

static lv_indev_t *create_test_keyboard(bool *present);
#define cm0_input_create_keyboard create_test_keyboard
#include "../src/runtime.cpp"
#undef cm0_input_create_keyboard

#include <assert.h>
#include <initializer_list>
#include <algorithm>
#include <vector>
#include <fstream>

static bool keyboard_available = false;
static unsigned keyboard_open_attempts = 0;
static uint32_t keyboard_sample_key = LV_KEY_NEXT;
static lv_indev_state_t keyboard_sample_state = LV_INDEV_STATE_RELEASED;

static lv_indev_t *make_test_keyboard() {
  lv_indev_t *keyboard = lv_indev_create();
  assert(keyboard);
  lv_indev_set_type(keyboard, LV_INDEV_TYPE_KEYPAD);
  lv_indev_set_read_cb(keyboard, [](lv_indev_t *, lv_indev_data_t *data) {
    data->key = keyboard_sample_key;
    data->state = keyboard_sample_state;
  });
  return keyboard;
}

static lv_indev_t *create_test_keyboard(bool *present) {
  ++keyboard_open_attempts;
  *present = keyboard_available;
  return keyboard_available ? make_test_keyboard() : nullptr;
}

class InputFixture {
 public:
  explicit InputFixture(bool connected = true) {
    lv_init();
    display = lv_display_create(480, 640);
    assert(display);
    lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t *target,
                                        const lv_area_t *, uint8_t *) {
      lv_display_flush_ready(target);
    });
    group = lv_group_create();
    lv_group_set_default(group);
    state.group = group;
    state.running = &running;
    if (connected) attach_keyboard(&state, make_test_keyboard(), true);
    timer = lv_timer_create(input_housekeeping, 50, &state);
    assert(timer);
  }

  ~InputFixture() {
    lv_timer_delete(timer);
    if (state.keyboard) lv_indev_delete(state.keyboard);
    lv_group_set_default(nullptr);
    lv_group_delete(group);
    lv_display_delete(display);
    lilygo_ui_fonts_deinit();
    lv_deinit();
  }

  void expect_members(std::initializer_list<lv_obj_t *> members) const {
    assert(lv_group_get_obj_count(group) == members.size());
    for (lv_obj_t *object : members) {
      assert(lv_obj_get_group(object) == group);
      assert(!object_hidden_in_tree(object));
    }
  }

  lv_display_t *display = nullptr;
  lv_group_t *group = nullptr;
  lv_timer_t *timer = nullptr;
  volatile sig_atomic_t running = 1;
  app_input_state_t state{};

 private:
  uint32_t pixels[480 * 16]{};
};

struct SensorControls {
  SensorControls() {
    lv_obj_t *page = lv_obj_create(lv_screen_active());
    lv_obj_t *header = lv_obj_create(page);
    back = lv_button_create(header);
    lv_obj_t *navigation = lv_obj_create(page);
    tabs = lv_buttonmatrix_create(navigation);
    static const char *labels[] = {"Sensors", "History", ""};
    lv_buttonmatrix_set_map(tabs, labels);
    filter = lv_dropdown_create(navigation);
    lv_dropdown_set_options(filter, "All buses\nSystem");
    interval = lv_slider_create(page);
  }

  lv_obj_t *back;
  lv_obj_t *tabs;
  lv_obj_t *filter;
  lv_obj_t *interval;
};

static void verify_controls_without_keyboard() {
  InputFixture fixture;
  SensorControls controls;
  fixture.expect_members(
      {controls.back, controls.tabs, controls.filter, controls.interval});

  hide_software_keyboards(lv_screen_active(), fixture.group);
  fixture.expect_members(
      {controls.back, controls.tabs, controls.filter, controls.interval});
  assert(lv_group_get_focused(fixture.group) == controls.back);
}

static void verify_focus_housekeeping() {
  InputFixture fixture;
  SensorControls controls;
  assert(lv_group_get_focused(fixture.group) == controls.back);

  // Removing the focused button used to refocus the editable button matrix.
  // Its FOCUSED callback recursively removed the remaining group entries,
  // leaving LVGL's outer focus_next_core() with a null focus pointer.
  for (unsigned iteration = 0; iteration < 20; ++iteration) {
    lv_obj_t *focused = lv_group_get_focused(fixture.group);
    input_housekeeping(fixture.timer);
    fixture.expect_members(
        {controls.back, controls.tabs, controls.filter, controls.interval});
    assert(lv_group_get_focused(fixture.group) == focused);
    lv_group_focus_next(fixture.group);
  }
}

static void verify_scan_completion() {
  InputFixture fixture;
  lv_obj_t *page = lv_obj_create(lv_screen_active());
  lv_obj_t *toolbar = lv_obj_create(page);
  lv_obj_t *selector = lv_dropdown_create(toolbar);
  lv_obj_t *scan = lv_button_create(toolbar);

  // I2C Detector starts scanning before any adapters are available. Both
  // controls remain disabled while the hardware input timer keeps running.
  lv_dropdown_set_options(selector, "No I2C buses");
  lv_obj_add_state(selector, LV_STATE_DISABLED);
  lv_obj_add_state(scan, LV_STATE_DISABLED);
  for (unsigned iteration = 0; iteration < 3; ++iteration)
    input_housekeeping(fixture.timer);

  // Publishing the scan result enables the editable dropdown. The old
  // keyboard-hiding walk had emptied the group, so collecting it again
  // focused the dropdown and removed it from inside its FOCUSED callback.
  lv_dropdown_set_options(selector, "/dev/i2c-1\n/dev/i2c-4");
  lv_obj_remove_state(selector, LV_STATE_DISABLED);
  lv_obj_remove_state(scan, LV_STATE_DISABLED);
  input_housekeeping(fixture.timer);
  fixture.expect_members({selector, scan});
  assert(lv_group_get_focused(fixture.group) == selector);

  lv_group_focus_next(fixture.group);
  assert(lv_group_get_focused(fixture.group) == scan);
  lv_group_focus_next(fixture.group);
  assert(lv_group_get_focused(fixture.group) == selector);
}

static void verify_keyboard_isolation() {
  InputFixture fixture;
  lv_obj_t *page = lv_obj_create(lv_screen_active());
  lv_obj_t *button = lv_button_create(page);
  lv_obj_t *textarea = lv_textarea_create(page);
  lv_obj_t *keyboard = lv_keyboard_create(lv_layer_top());
  lv_keyboard_set_textarea(keyboard, textarea);
  // Explicit membership also covers applications that assign their own
  // keyboard group rather than relying on the default group.
  lv_group_add_obj(fixture.group, keyboard);
  lv_group_add_obj(fixture.group, textarea);
  lv_group_focus_obj(textarea);

  hide_software_keyboards(lv_layer_top(), fixture.group);
  assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
  assert(lv_obj_get_group(keyboard) == nullptr);
  fixture.expect_members({button, textarea});
  assert(lv_group_get_focused(fixture.group) == textarea);

  for (unsigned iteration = 0; iteration < 20; ++iteration) {
    // Applications can show the keyboard again after a field gains focus.
    lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    input_housekeeping(fixture.timer);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_group(keyboard) == nullptr);
    fixture.expect_members({button, textarea});
    assert(lv_group_get_focused(fixture.group) == textarea);
    lv_group_focus_next(fixture.group);
    assert(lv_group_get_focused(fixture.group) == button);
    lv_group_focus_next(fixture.group);
    assert(lv_group_get_focused(fixture.group) == textarea);
  }

  // A previously focused software keyboard can still be present when the
  // hardware path takes over. Removing it synchronously focuses the textarea
  // and reenters keyboard hiding through the runtime's FOCUSED callback.
  lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
  lv_group_add_obj(fixture.group, keyboard);
  lv_group_focus_obj(keyboard);
  assert(lv_group_get_focused(fixture.group) == keyboard);
  input_housekeeping(fixture.timer);
  assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
  assert(lv_obj_get_group(keyboard) == nullptr);
  fixture.expect_members({button, textarea});
  assert(lv_group_get_focused(fixture.group) == textarea);
}

static void verify_scroll_position() {
  InputFixture fixture;
  lv_obj_t *list = lv_obj_create(lv_screen_active());
  lv_obj_set_size(list, 400, 320);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_t *rows[12];
  for (lv_obj_t *&row : rows) {
    row = lv_button_create(list);
    lv_obj_set_size(row, 300, 64);
  }
  lv_obj_update_layout(list);

  // Advance real LVGL timers, including scroll animations and the 50 ms
  // hardware-keyboard housekeeping that used to refocus every list row.
  auto advance = [] {
    for (unsigned step = 0; step < 100; ++step) {
      lv_tick_inc(20);
      lv_timer_handler();
    }
  };
  advance();
  assert(lv_obj_get_scroll_y(list) == 0);
  assert(lv_group_get_focused(fixture.group) == rows[0]);

  // The user's chosen scroll position must survive idle polling, even when
  // the focused row is outside the viewport after a touch scroll.
  for (int32_t position : {160, 320, 0}) {
    lv_obj_scroll_to_y(list, position, LV_ANIM_OFF);
    assert(lv_obj_get_scroll_y(list) == position);
    advance();
    assert(lv_obj_get_scroll_y(list) == position);
    assert(lv_group_get_focused(fixture.group) == rows[0]);
  }

  // Keep normal keyboard navigation: a deliberate focus change must still
  // scroll the requested row into view.
  for (unsigned index = 1; index < 12; ++index)
    lv_group_focus_next(fixture.group);
  advance();
  assert(lv_group_get_focused(fixture.group) == rows[11]);
  assert(lv_obj_get_scroll_y(list) > 320);

  lv_obj_scroll_to_y(list, 160, LV_ANIM_OFF);
  advance();
  assert(lv_obj_get_scroll_y(list) == 160);
  assert(lv_group_get_focused(fixture.group) == rows[11]);
}

static void verify_enter_submission() {
  InputFixture fixture;
  lv_obj_t *textarea = lv_textarea_create(lv_screen_active());
  lv_textarea_set_one_line(textarea, true);
  lv_obj_t *keyboard = lv_keyboard_create(lv_layer_top());
  lv_keyboard_set_textarea(keyboard, textarea);
  lv_group_add_obj(fixture.group, textarea);
  lv_group_focus_obj(textarea);

  unsigned keyboard_ready = 0;
  unsigned textarea_ready = 0;
  auto count_ready = [](lv_event_t *event) {
    ++*static_cast<unsigned *>(lv_event_get_user_data(event));
  };
  lv_obj_add_event_cb(keyboard, count_ready, LV_EVENT_READY, &keyboard_ready);
  lv_obj_add_event_cb(textarea, count_ready, LV_EVENT_READY, &textarea_ready);

  lv_indev_state_t sample_state = LV_INDEV_STATE_RELEASED;
  lv_indev_t *indev = fixture.state.keyboard;
  lv_indev_set_user_data(indev, &sample_state);
  lv_indev_set_read_cb(indev, [](lv_indev_t *input, lv_indev_data_t *data) {
    data->key = LV_KEY_ENTER;
    data->state = *static_cast<lv_indev_state_t *>(lv_indev_get_user_data(input));
  });
  lv_indev_read(indev);
  for (unsigned press = 1; press <= 2; ++press) {
    sample_state = LV_INDEV_STATE_PRESSED;
    for (unsigned poll = 0; poll < 5; ++poll) lv_indev_read(indev);
    // Match the software keyboard's single READY on each object, even when
    // the input timer reads the held key repeatedly before it is released.
    assert(keyboard_ready == press);
    assert(textarea_ready == press);
    assert(lv_group_get_focused(fixture.group) == textarea);
    sample_state = LV_INDEV_STATE_RELEASED;
    lv_indev_read(indev);
  }
}

static void verify_keyboard_hotplug() {
  InputFixture fixture(false);
  lv_obj_t *first = lv_button_create(lv_screen_active());
  lv_obj_t *second = lv_button_create(lv_screen_active());
  lv_obj_t *textarea = lv_textarea_create(lv_screen_active());
  lv_textarea_set_one_line(textarea, true);
  lv_obj_t *software_keyboard = lv_keyboard_create(lv_layer_top());
  lv_keyboard_set_textarea(software_keyboard, textarea);
  unsigned ready = 0;
  lv_obj_add_event_cb(software_keyboard, [](lv_event_t *event) {
    ++*static_cast<unsigned *>(lv_event_get_user_data(event));
  }, LV_EVENT_READY, &ready);

  // Startup without a keyboard keeps its focus group and does not suppress
  // software input. Absent-device scanning is bounded by the retry interval.
  for (unsigned poll = 0; poll < 10; ++poll) {
    lv_tick_inc(50);
    input_housekeeping(fixture.timer);
  }
  assert(keyboard_open_attempts == 1);
  assert(!fixture.state.keyboard);
  assert(!fixture.state.hardware_keyboard);
  assert(!lv_obj_has_flag(software_keyboard, LV_OBJ_FLAG_HIDDEN));

  for (unsigned cycle = 0; cycle < 3; ++cycle) {
    keyboard_available = true;
    lv_tick_inc(kKeyboardScanInterval);
    input_housekeeping(fixture.timer);
    assert(fixture.state.keyboard);
    assert(fixture.state.hardware_keyboard);
    assert(lv_indev_get_group(fixture.state.keyboard) == fixture.group);
    assert(!fixture.state.esc_pressed);
    assert(!fixture.state.enter_pressed);
    assert(!fixture.state.focus_suppressed);
    assert(lv_obj_has_flag(software_keyboard, LV_OBJ_FLAG_HIDDEN));

    // A replacement indev must have both normal focus navigation and the
    // runtime's special Enter/ESC callbacks, not merely a readable event fd.
    lv_group_focus_obj(first);
    keyboard_sample_state = LV_INDEV_STATE_RELEASED;
    lv_indev_read(fixture.state.keyboard);
    keyboard_sample_key = LV_KEY_NEXT;
    keyboard_sample_state = LV_INDEV_STATE_PRESSED;
    lv_indev_read(fixture.state.keyboard);
    assert(lv_group_get_focused(fixture.group) == second);
    keyboard_sample_state = LV_INDEV_STATE_RELEASED;
    lv_indev_read(fixture.state.keyboard);

    lv_group_add_obj(fixture.group, textarea);
    lv_group_focus_obj(textarea);
    keyboard_sample_key = LV_KEY_ENTER;
    keyboard_sample_state = LV_INDEV_STATE_PRESSED;
    lv_indev_read(fixture.state.keyboard);
    assert(ready == cycle + 1);
    assert(fixture.state.enter_pressed);
    keyboard_sample_key = LV_KEY_ESC;
    lv_indev_read(fixture.state.keyboard);
    assert(fixture.state.esc_pressed);
    assert(fixture.state.focus_suppressed);
    assert(fixture.running);

    // Match evdev's deletion on ENODEV while keys were still held. Timer
    // callbacks must stop using the old indev and accept a new one later.
    lv_indev_delete(fixture.state.keyboard);
    assert(!fixture.state.keyboard);
    assert(!fixture.state.hardware_keyboard);
    assert(!fixture.state.esc_pressed);
    assert(!fixture.state.enter_pressed);
    keyboard_available = false;
    keyboard_sample_state = LV_INDEV_STATE_RELEASED;
    lv_obj_remove_flag(software_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_tick_inc(kKeyboardScanInterval);
    input_housekeeping(fixture.timer);
    assert(!fixture.state.keyboard);
    assert(!lv_obj_has_flag(software_keyboard, LV_OBJ_FLAG_HIDDEN));
  }
}

static void press_key(InputFixture &fixture, uint32_t key) {
  keyboard_sample_key = key;
  keyboard_sample_state = LV_INDEV_STATE_PRESSED;
  lv_indev_read(fixture.state.keyboard);
  keyboard_sample_state = LV_INDEV_STATE_RELEASED;
  lv_indev_read(fixture.state.keyboard);
}

static lv_obj_t *plain_control(lv_obj_t *parent, int x, int y) {
  auto *button = lv_button_create(parent);
  lv_obj_remove_style_all(button);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, 80, 50);
  return button;
}

static void verify_navigation() {
  InputFixture fixture;
  auto *first = plain_control(lv_screen_active(), 0, 0);
  auto *right = plain_control(lv_screen_active(), 100, 0);
  auto *down = plain_control(lv_screen_active(), 0, 80);
  auto *field = lv_textarea_create(lv_screen_active());
  lv_obj_set_pos(field, 0, 160);
  lv_obj_set_size(field, 200, 50);
  lv_textarea_set_one_line(field, true);
  auto *card = lv_obj_create(lv_screen_active());
  lv_obj_set_pos(card, 100, 80);
  lv_obj_set_size(card, 80, 50);
  unsigned clicks = 0;
  lv_obj_add_event_cb(card, [](lv_event_t *event) {
    ++*static_cast<unsigned *>(lv_event_get_user_data(event));
  }, LV_EVENT_CLICKED, &clicks);
  auto *decoration = lv_obj_create(lv_screen_active());
  input_housekeeping(fixture.timer);
  assert(!lv_obj_get_group(decoration));
  assert(lv_obj_get_group(card) == fixture.group);
  lv_group_focus_obj(first);
  press_key(fixture, LV_KEY_RIGHT);
  assert(lv_group_get_focused(fixture.group) == right);
  press_key(fixture, LV_KEY_DOWN);
  assert(lv_group_get_focused(fixture.group) == card);
  press_key(fixture, LV_KEY_ENTER);
  assert(clicks == 1);
  press_key(fixture, LV_KEY_LEFT);
  assert(lv_group_get_focused(fixture.group) == down);
  press_key(fixture, LV_KEY_DOWN);
  assert(lv_group_get_focused(fixture.group) == field);
  press_key(fixture, 'a');
  press_key(fixture, 'b');
  press_key(fixture, LV_KEY_LEFT);
  press_key(fixture, 'X');
  assert(strcmp(lv_textarea_get_text(field), "aXb") == 0);
  press_key(fixture, LV_KEY_BACKSPACE);
  assert(strcmp(lv_textarea_get_text(field), "ab") == 0);
  press_key(fixture, LV_KEY_NEXT);
  assert(lv_group_get_focused(fixture.group) != field);
  lv_group_focus_obj(field);
  press_key(fixture, LV_KEY_ESC);
  assert(fixture.running && fixture.state.focus_suppressed);
  press_key(fixture, LV_KEY_NEXT);
  assert(!fixture.state.focus_suppressed && lv_group_get_focused(fixture.group));

  // Hide/disable entire pages: no descendant should remain keyboard reachable.
  auto *disabled_page = lv_obj_create(lv_screen_active());
  auto *disabled = plain_control(disabled_page, 0, 0);
  lv_obj_add_state(disabled_page, LV_STATE_DISABLED);
  input_housekeeping(fixture.timer);
  assert(!lv_obj_get_group(disabled));
  lv_obj_remove_state(disabled_page, LV_STATE_DISABLED);
  input_housekeeping(fixture.timer);
  assert(lv_obj_get_group(disabled) == fixture.group);
  lv_obj_add_flag(disabled_page, LV_OBJ_FLAG_HIDDEN);
  input_housekeeping(fixture.timer);
  assert(!lv_obj_get_group(disabled));

  // Tooltip callbacks listening to ALL events must not suppress navigation.
  lv_obj_add_event_cb(first, [](lv_event_t *) {}, LV_EVENT_ALL, nullptr);
  auto *hint_button = plain_control(lv_screen_active(), 400, 80);
  lv_obj_add_event_cb(hint_button, [](lv_event_t *) {}, LV_EVENT_ALL, nullptr);
  input_housekeeping(fixture.timer);
  lv_group_focus_obj(hint_button);
  press_key(fixture, LV_KEY_LEFT);
  assert(lv_group_get_focused(fixture.group) != hint_button);

  auto *read_only = plain_control(lv_screen_active(), 400, 160);
  lv_obj_remove_flag(read_only, LV_OBJ_FLAG_CLICKABLE);
  auto *toggle = lv_switch_create(lv_screen_active());
  input_housekeeping(fixture.timer);
  assert(!lv_obj_get_group(read_only));
  lv_group_focus_obj(toggle);
  press_key(fixture, LV_KEY_RIGHT);
  assert(lv_obj_has_state(toggle, LV_STATE_CHECKED));
  assert(lv_group_get_focused(fixture.group) == toggle);
  press_key(fixture, LV_KEY_NEXT);
  assert(lv_group_get_focused(fixture.group) != toggle);

  // Existing application key handlers retain control of navigation.
  auto *custom = plain_control(lv_screen_active(), 300, 0);
  unsigned custom_keys = 0;
  lv_obj_add_event_cb(custom, [](lv_event_t *event) {
    ++*static_cast<unsigned *>(lv_event_get_user_data(event));
  }, LV_EVENT_KEY, &custom_keys);
  input_housekeeping(fixture.timer);
  lv_group_focus_obj(custom);
  press_key(fixture, LV_KEY_LEFT);
  assert(custom_keys == 1 && lv_group_get_focused(fixture.group) == custom);
}

static void verify_modal_scope() {
  InputFixture fixture;
  auto *background = plain_control(lv_screen_active(), 0, 0);
  auto *overlay = lv_obj_create(lv_screen_active());
  cm0_input_set_focus_scope(overlay);
  auto *cancel = plain_control(overlay, 0, 0);
  auto *confirm = plain_control(overlay, 100, 0);
  input_housekeeping(fixture.timer);
  fixture.expect_members({cancel, confirm});
  press_key(fixture, LV_KEY_NEXT);
  press_key(fixture, LV_KEY_NEXT);
  assert(lv_group_get_focused(fixture.group) != background);
  auto *nested = lv_obj_create(overlay);
  cm0_input_set_focus_scope(nested);
  auto *close = plain_control(nested, 0, 0);
  input_housekeeping(fixture.timer);
  fixture.expect_members({close});
  lv_obj_delete(nested);
  input_housekeeping(fixture.timer);
  fixture.expect_members({cancel, confirm});
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  input_housekeeping(fixture.timer);
  fixture.expect_members({background});
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  input_housekeeping(fixture.timer);
  fixture.expect_members({cancel, confirm});
  lv_obj_delete(overlay);
  input_housekeeping(fixture.timer);
  fixture.expect_members({background});
}

static void verify_focus_ring() {
  InputFixture fixture;
  std::vector<lv_color32_t> frame;
  lv_display_set_user_data(fixture.display, &frame);
  lv_display_set_flush_cb(fixture.display, [](lv_display_t *display,
                                              const lv_area_t *area, uint8_t *pixels) {
    auto &frame = *static_cast<std::vector<lv_color32_t> *>(lv_display_get_user_data(display));
    const int width = lv_display_get_horizontal_resolution(display);
    auto *source = reinterpret_cast<lv_color32_t *>(pixels);
    for (int y = area->y1; y <= area->y2; ++y)
      std::copy_n(source + (y - area->y1) * lv_area_get_width(area),
                  lv_area_get_width(area), frame.begin() + y * width + area->x1);
    lv_display_flush_ready(display);
  });
  // A photo tile fills a zero-padding clipped parent and is covered by its
  // opaque child. All four sides of the focus ring must still be drawn.
  auto *clip = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(clip);
  lv_obj_set_pos(clip, 20, 20);
  lv_obj_set_size(clip, 100, 70);
  auto *tile = plain_control(clip, 0, 0);
  lv_obj_set_size(tile, 100, 70);
  auto *photo = lv_obj_create(tile);
  lv_obj_remove_style_all(photo);
  lv_obj_set_size(photo, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(photo, lv_color_hex(0x20262d), 0);
  lv_obj_set_style_bg_opa(photo, LV_OPA_COVER, 0);
  input_housekeeping(fixture.timer);
  lv_group_focus_obj(tile);
  for (const auto size : {lv_point_t{320, 568}, lv_point_t{568, 1232},
                          lv_point_t{1232, 568}, lv_point_t{1024, 768}}) {
    lv_display_set_resolution(fixture.display, size.x, size.y);
    frame.resize(size.x * size.y);
    lv_obj_update_layout(clip);
    lv_refr_now(fixture.display);
    const auto blue = [&](int x, int y) {
      const auto &pixel = frame[y * size.x + x];
      return pixel.red == 0 && pixel.green == 0x7a && pixel.blue == 0xff;
    };
    for (int x = 20; x < 120; ++x) {
      assert(blue(x, 20));
      assert(blue(x, 89));
    }
    for (int y = 20; y < 90; ++y) {
      assert(blue(20, y));
      assert(blue(119, y));
    }
    assert(!blue(19, 50) && !blue(120, 50));
    // Optional artifact used for visual inspection alongside pixel assertions.
    if (const char *path = getenv("LILYGO_FOCUS_RING_PPM")) {
      std::ofstream file(path, std::ios::binary);
      file << "P6\n" << size.x << ' ' << size.y << "\n255\n";
      for (const auto &pixel : frame) {
        file.put(pixel.red); file.put(pixel.green); file.put(pixel.blue);
      }
    }
  }
}

int main(int argc, char **argv) {
  assert(argc == 2);
  if (strcmp(argv[1], "controls") == 0)
    verify_controls_without_keyboard();
  else if (strcmp(argv[1], "focus") == 0)
    verify_focus_housekeeping();
  else if (strcmp(argv[1], "scan-completion") == 0)
    verify_scan_completion();
  else if (strcmp(argv[1], "keyboard") == 0)
    verify_keyboard_isolation();
  else if (strcmp(argv[1], "scroll") == 0)
    verify_scroll_position();
  else if (strcmp(argv[1], "enter") == 0)
    verify_enter_submission();
  else if (strcmp(argv[1], "hotplug") == 0)
    verify_keyboard_hotplug();
  else if (strcmp(argv[1], "navigation") == 0)
    verify_navigation();
  else if (strcmp(argv[1], "scope") == 0)
    verify_modal_scope();
  else if (strcmp(argv[1], "focus-ring") == 0)
    verify_focus_ring();
  else
    return 2;
  return 0;
}
