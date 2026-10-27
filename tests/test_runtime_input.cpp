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
    assert(!fixture.state.focus_visible);
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
    assert(fixture.state.focus_visible);
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

static void verify_focus_cleanup() {
  InputFixture fixture;
  auto *disabled = plain_control(lv_screen_active(), 0, 0);
  auto *content = plain_control(lv_screen_active(), 100, 0);
  auto *content_next = plain_control(lv_screen_active(), 200, 0);
  auto *hidden = plain_control(lv_screen_active(), 300, 0);
  auto *navigation = plain_control(lv_screen_active(), 400, 0);
  lv_obj_add_state(disabled, LV_STATE_DISABLED);
  lv_obj_add_flag(hidden, LV_OBJ_FLAG_HIDDEN);

  input_housekeeping(fixture.timer);
  // Removing the initially focused disabled control must preserve the first
  // visible content control instead of wrapping focus to a later navigation
  // item through LVGL's default PREV refocus policy.
  assert(lv_group_get_focused(fixture.group) == content);
  assert(lv_obj_get_group(navigation) == fixture.group);
  press_key(fixture, LV_KEY_NEXT);
  assert(lv_group_get_focused(fixture.group) == content_next);
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
  lv_obj_set_pos(toggle, 0, 260);
  input_housekeeping(fixture.timer);
  assert(!lv_obj_get_group(read_only));
  lv_group_focus_obj(toggle);
  press_key(fixture, LV_KEY_RIGHT);
  assert(!lv_obj_has_state(toggle, LV_STATE_CHECKED));
  assert(lv_group_get_focused(fixture.group) != toggle);
  lv_group_focus_obj(toggle);
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

static void verify_unmarked_scroll_navigation() {
  InputFixture fixture;
  const auto settle_scroll = [] {
    for (unsigned step = 0; step < 30; ++step) {
      lv_tick_inc(20);
      lv_timer_handler();
    }
  };
  auto *list = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, 200, 100);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  auto *first = plain_control(list, 0, 0);
  auto *second = plain_control(list, 0, 140);
  input_housekeeping(fixture.timer);
  lv_group_focus_obj(first);

  // Existing applications do not mark their lists as browsing regions. Keep
  // their directional focus navigation and automatic reveal across viewports.
  press_key(fixture, LV_KEY_DOWN);
  assert(lv_group_get_focused(fixture.group) == second);
  // Legacy lists reveal immediately so the next directional key is usable
  // even before a timer tick or scroll animation has run.
  assert(lv_obj_get_scroll_y(list) > 0);
  settle_scroll();
  assert(lv_obj_get_scroll_y(list) > 0);
  lv_area_t viewport, bounds;
  lv_obj_get_coords(list, &viewport);
  lv_obj_get_coords(second, &bounds);
  assert(bounds.y1 >= viewport.y1 && bounds.y2 <= viewport.y2);

  press_key(fixture, LV_KEY_UP);
  assert(lv_group_get_focused(fixture.group) == first);
  assert(lv_obj_get_scroll_y(list) == 0);
  settle_scroll();
  assert(lv_obj_get_scroll_y(list) == 0);
  lv_obj_get_coords(first, &bounds);
  assert(bounds.y1 >= viewport.y1 && bounds.y2 <= viewport.y2);
}

static void verify_switch_keys() {
  InputFixture fixture;
  auto *toggle = lv_switch_create(lv_screen_active());
  lv_obj_set_pos(toggle, 200, 200);
  lv_obj_set_size(toggle, 48, 28);
  auto *left = plain_control(lv_screen_active(), 60, 190);
  auto *right = plain_control(lv_screen_active(), 320, 190);
  auto *up = plain_control(lv_screen_active(), 184, 80);
  auto *down = plain_control(lv_screen_active(), 184, 300);
  unsigned changes = 0;
  lv_obj_add_event_cb(toggle, [](lv_event_t *event) {
    ++*static_cast<unsigned *>(lv_event_get_user_data(event));
  }, LV_EVENT_VALUE_CHANGED, &changes);
  input_housekeeping(fixture.timer);
  const uint32_t arrows[]{LV_KEY_LEFT, LV_KEY_RIGHT, LV_KEY_UP, LV_KEY_DOWN};
  lv_obj_t *neighbors[]{left, right, up, down};
  for (const bool checked : {false, true}) {
    lv_obj_set_state(toggle, LV_STATE_CHECKED, checked);
    for (unsigned i = 0; i < 4; ++i) {
      lv_group_focus_obj(toggle);
      press_key(fixture, arrows[i]);
      assert(lv_group_get_focused(fixture.group) == neighbors[i]);
      assert(lv_obj_has_state(toggle, LV_STATE_CHECKED) == checked);
      assert(changes == 0);
    }
  }
  for (auto *neighbor : neighbors) lv_obj_add_flag(neighbor, LV_OBJ_FLAG_HIDDEN);
  input_housekeeping(fixture.timer);
  lv_group_focus_obj(toggle);
  // Even at a navigation boundary, arrows must never change the value.
  for (const bool checked : {false, true}) {
    lv_obj_set_state(toggle, LV_STATE_CHECKED, checked);
    for (const auto key : arrows) {
      press_key(fixture, key);
      assert(lv_group_get_focused(fixture.group) == toggle);
      assert(lv_obj_has_state(toggle, LV_STATE_CHECKED) == checked);
      assert(changes == 0);
    }
  }
  for (const auto key : {uint32_t(LV_KEY_ENTER), uint32_t(' ')}) {
    for (unsigned press = 0; press < 2; ++press) {
      const bool before = lv_obj_has_state(toggle, LV_STATE_CHECKED);
      press_key(fixture, key);
      assert(lv_obj_has_state(toggle, LV_STATE_CHECKED) != before);
      assert(lv_group_get_focused(fixture.group) == toggle);
      assert(changes == (key == LV_KEY_ENTER ? 0u : 2u) + press + 1);
    }
  }
  // Long holds include repeated KEY events for Space and must toggle once.
  for (const auto key : {uint32_t(LV_KEY_ENTER), uint32_t(' ')}) {
    const bool before = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    const unsigned previous_changes = changes;
    keyboard_sample_key = key;
    keyboard_sample_state = LV_INDEV_STATE_PRESSED;
    lv_indev_read(fixture.state.keyboard);
    for (unsigned repeat = 0; repeat < 8; ++repeat) {
      lv_tick_inc(500);
      lv_indev_read(fixture.state.keyboard);
    }
    keyboard_sample_state = LV_INDEV_STATE_RELEASED;
    lv_indev_read(fixture.state.keyboard);
    assert(changes == previous_changes + 1);
    assert(lv_obj_has_state(toggle, LV_STATE_CHECKED) != before);
    assert(!fixture.state.space_pressed);
  }
  // Text entry still receives spaces normally.
  auto *field = lv_textarea_create(lv_screen_active());
  input_housekeeping(fixture.timer);
  lv_group_focus_obj(field);
  press_key(fixture, 'a');
  press_key(fixture, ' ');
  press_key(fixture, 'b');
  assert(strcmp(lv_textarea_get_text(field), "a b") == 0);
}

static lv_obj_t *browse_region(lv_obj_t *parent, int x, int y, int height,
                                int content_height) {
  auto *region = lv_obj_create(parent);
  lv_obj_remove_style_all(region);
  lv_obj_set_pos(region, x, y);
  lv_obj_set_size(region, 200, height);
  lv_obj_set_scroll_dir(region, LV_DIR_VER);
  cm0_input_set_scroll_region(region);
  auto *content = lv_obj_create(region);
  lv_obj_remove_style_all(content);
  lv_obj_set_size(content, 180, content_height);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  return region;
}

static void verify_browse_scrolling() {
  InputFixture fixture;
  auto *before = plain_control(lv_screen_active(), 0, 0);
  auto *region = browse_region(lv_screen_active(), 0, 80, 140, 600);
  auto *after = plain_control(lv_screen_active(), 280, 0);
  auto *short_region = browse_region(lv_screen_active(), 280, 80, 100, 40);
  // Repeated registration must not duplicate the browsing stop.
  cm0_input_set_scroll_region(region);
  input_housekeeping(fixture.timer);
  fixture.expect_members({before, region, after});
  assert(!lv_obj_get_group(short_region));
  assert(!lv_obj_get_group(lv_obj_get_child(region, 0)));

  lv_group_focus_obj(before);
  press_key(fixture, LV_KEY_NEXT);
  assert(lv_group_get_focused(fixture.group) == region);
  for (unsigned step = 0; step < 5; ++step) {
    const auto previous = lv_obj_get_scroll_y(region);
    press_key(fixture, LV_KEY_DOWN);
    assert(lv_group_get_focused(fixture.group) == region);
    assert(lv_obj_get_scroll_y(region) > previous);
  }
  const auto browsed = lv_obj_get_scroll_y(region);
  for (unsigned poll = 0; poll < 4; ++poll)
    input_housekeeping(fixture.timer);
  assert(lv_obj_get_scroll_y(region) == browsed);
  press_key(fixture, LV_KEY_UP);
  assert(lv_obj_get_scroll_y(region) < browsed);

  // Exercise a held arrow through LVGL's actual key-repeat path.
  keyboard_sample_key = LV_KEY_DOWN;
  keyboard_sample_state = LV_INDEV_STATE_PRESSED;
  lv_indev_read(fixture.state.keyboard);
  const auto first_press = lv_obj_get_scroll_y(region);
  lv_tick_inc(500);
  lv_indev_read(fixture.state.keyboard);
  lv_tick_inc(500);
  lv_indev_read(fixture.state.keyboard);
  assert(lv_obj_get_scroll_y(region) > first_press);
  assert(lv_group_get_focused(fixture.group) == region);
  keyboard_sample_state = LV_INDEV_STATE_RELEASED;
  lv_indev_read(fixture.state.keyboard);
  for (unsigned step = 0; step < 20; ++step)
    press_key(fixture, LV_KEY_DOWN);
  assert(lv_obj_get_scroll_bottom(region) == 0);
  assert(lv_group_get_focused(fixture.group) == region);
  for (unsigned step = 0; step < 20 && lv_obj_get_scroll_y(region) > 0; ++step)
    press_key(fixture, LV_KEY_UP);
  assert(lv_obj_get_scroll_y(region) == 0);
  assert(lv_group_get_focused(fixture.group) == region);
  press_key(fixture, LV_KEY_NEXT);
  assert(lv_group_get_focused(fixture.group) == after);
  press_key(fixture, LV_KEY_PREV);
  assert(lv_group_get_focused(fixture.group) == region);
  press_key(fixture, LV_KEY_PREV);
  assert(lv_group_get_focused(fixture.group) == before);
}

static void verify_browse_dynamic_content() {
  InputFixture fixture;
  auto *before = plain_control(lv_screen_active(), 0, 0);
  auto *region = browse_region(lv_screen_active(), 0, 80, 140, 500);
  auto *after = plain_control(lv_screen_active(), 280, 0);
  input_housekeeping(fixture.timer);
  lv_group_focus_obj(region);
  press_key(fixture, LV_KEY_DOWN);
  assert(lv_obj_get_scroll_y(region) > 0);
  // A refresh shrinks a focused, scrolled list to a single nonoverflowing row.
  lv_obj_clean(region);
  auto *content = lv_obj_create(region);
  lv_obj_remove_style_all(content);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(content, 180, 40);
  input_housekeeping(fixture.timer);
  fixture.expect_members({before, after});
  assert(!lv_obj_get_group(region));
  assert(lv_group_get_focused(fixture.group) != region);
  // Growing the same viewport again must restore its original Tab position.
  lv_obj_set_height(content, 500);
  input_housekeeping(fixture.timer);
  fixture.expect_members({before, region, after});
  lv_group_focus_obj(before);
  press_key(fixture, LV_KEY_NEXT);
  assert(lv_group_get_focused(fixture.group) == region);
  press_key(fixture, LV_KEY_NEXT);
  assert(lv_group_get_focused(fixture.group) == after);
  press_key(fixture, LV_KEY_PREV);
  assert(lv_group_get_focused(fixture.group) == region);
  lv_obj_set_height(region, 520);
  input_housekeeping(fixture.timer);
  fixture.expect_members({before, after});
  lv_obj_set_height(region, 140);
  input_housekeeping(fixture.timer);
  fixture.expect_members({before, region, after});
  lv_obj_remove_flag(region, LV_OBJ_FLAG_SCROLLABLE);
  input_housekeeping(fixture.timer);
  fixture.expect_members({before, after});
  lv_obj_add_flag(region, LV_OBJ_FLAG_SCROLLABLE);
  input_housekeeping(fixture.timer);
  fixture.expect_members({before, region, after});
}

static void verify_browse_visibility_and_scopes() {
  InputFixture fixture;
  auto *background = plain_control(lv_screen_active(), 0, 0);
  auto *page = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(page);
  lv_obj_set_size(page, 240, 400);
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  auto *region = browse_region(page, 0, 80, 140, 500);
  input_housekeeping(fixture.timer);
  fixture.expect_members({background, region});
  lv_group_focus_obj(region);
  lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
  input_housekeeping(fixture.timer);
  fixture.expect_members({background});
  lv_obj_remove_flag(page, LV_OBJ_FLAG_HIDDEN);
  input_housekeeping(fixture.timer);
  fixture.expect_members({background, region});
  lv_group_focus_obj(region);
  lv_obj_add_state(page, LV_STATE_DISABLED);
  input_housekeeping(fixture.timer);
  fixture.expect_members({background});
  lv_obj_remove_state(page, LV_STATE_DISABLED);
  input_housekeeping(fixture.timer);
  fixture.expect_members({background, region});

  auto *overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(overlay);
  lv_obj_set_size(overlay, 460, 600);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  cm0_input_set_focus_scope(overlay);
  auto *cancel = plain_control(overlay, 0, 0);
  auto *modal_region = browse_region(overlay, 0, 80, 140, 500);
  input_housekeeping(fixture.timer);
  fixture.expect_members({cancel, modal_region});
  lv_group_focus_obj(cancel);
  press_key(fixture, LV_KEY_NEXT);
  assert(lv_group_get_focused(fixture.group) == modal_region);
  press_key(fixture, LV_KEY_NEXT);
  assert(lv_group_get_focused(fixture.group) == cancel);
  press_key(fixture, LV_KEY_PREV);
  assert(lv_group_get_focused(fixture.group) == modal_region);
  auto *nested = lv_obj_create(overlay);
  lv_obj_remove_style_all(nested);
  lv_obj_set_size(nested, 440, 500);
  lv_obj_remove_flag(nested, LV_OBJ_FLAG_SCROLLABLE);
  cm0_input_set_focus_scope(nested);
  auto *close = plain_control(nested, 0, 0);
  auto *nested_region = browse_region(nested, 0, 80, 140, 500);
  input_housekeeping(fixture.timer);
  fixture.expect_members({close, nested_region});
  lv_group_focus_obj(nested_region);
  press_key(fixture, LV_KEY_DOWN);
  assert(lv_obj_get_scroll_y(nested_region) > 0);
  assert(lv_obj_get_scroll_y(modal_region) == 0);
  assert(lv_obj_get_scroll_y(region) == 0);
  lv_obj_delete(nested);
  input_housekeeping(fixture.timer);
  fixture.expect_members({cancel, modal_region});
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  input_housekeeping(fixture.timer);
  fixture.expect_members({background, region});
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  input_housekeeping(fixture.timer);
  fixture.expect_members({cancel, modal_region});
  lv_obj_delete(overlay);
  input_housekeeping(fixture.timer);
  fixture.expect_members({background, region});
}

static void verify_nested_browse_regions() {
  InputFixture fixture;
  auto *before = plain_control(lv_screen_active(), 0, 0);
  auto *outer = browse_region(lv_screen_active(), 0, 80, 200, 700);
  auto *inner = browse_region(outer, 0, 0, 100, 400);
  auto *after = plain_control(lv_screen_active(), 280, 0);
  input_housekeeping(fixture.timer);
  fixture.expect_members({before, outer, inner, after});
  lv_group_focus_obj(outer);
  press_key(fixture, LV_KEY_DOWN);
  assert(lv_group_get_focused(fixture.group) == inner);
  assert(lv_obj_get_scroll_y(outer) == 0);
  assert(lv_obj_get_scroll_y(inner) == 0);
  press_key(fixture, LV_KEY_DOWN);
  assert(lv_obj_get_scroll_y(inner) > 0);
  assert(lv_obj_get_scroll_y(outer) == 0);
  press_key(fixture, LV_KEY_NEXT);
  assert(lv_group_get_focused(fixture.group) == after);
  press_key(fixture, LV_KEY_PREV);
  assert(lv_group_get_focused(fixture.group) == inner);
  press_key(fixture, LV_KEY_PREV);
  assert(lv_group_get_focused(fixture.group) == outer);

  // Tab must reveal a nested viewport that sits below the visible part of its
  // parent, while retaining the position already browsed inside that viewport.
  const auto inner_scroll = lv_obj_get_scroll_y(inner);
  lv_obj_set_y(inner, 300);
  lv_obj_scroll_to_y(outer, 0, LV_ANIM_OFF);
  press_key(fixture, LV_KEY_NEXT);
  assert(lv_group_get_focused(fixture.group) == inner);
  for (unsigned step = 0; step < 30; ++step) {
    lv_tick_inc(20);
    lv_timer_handler();
  }
  assert(lv_obj_get_scroll_y(outer) > 0);
  assert(lv_obj_get_scroll_y(inner) == inner_scroll);
}

static void verify_browse_region() {
  verify_browse_scrolling();
  verify_browse_dynamic_content();
  verify_browse_visibility_and_scopes();
  verify_nested_browse_regions();
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
  assert(!fixture.state.focus_visible);
  bool focus_activated = false;
  for (const auto size : {lv_point_t{320, 568}, lv_point_t{568, 1232},
                          lv_point_t{1232, 568}, lv_point_t{1024, 768}}) {
    lv_display_set_resolution(fixture.display, size.x, size.y);
    frame.resize(size.x * size.y);
    lv_obj_update_layout(clip);
    lv_refr_now(fixture.display);
    const auto focus_ring = [&](int x, int y) {
      const auto &pixel = frame[y * size.x + x];
      return pixel.red == 0x25 && pixel.green == 0x29 && pixel.blue == 0x32;
    };
    if (!focus_activated) {
      assert(!focus_ring(20, 20));
      assert(!focus_ring(20, 50));
      press_key(fixture, LV_KEY_NEXT);
      assert(fixture.state.focus_visible);
      lv_refr_now(fixture.display);
      focus_activated = true;
    }
    for (int x = 20; x < 120; ++x) {
      assert(focus_ring(x, 20));
      assert(focus_ring(x, 89));
    }
    for (int y = 20; y < 90; ++y) {
      assert(focus_ring(20, y));
      assert(focus_ring(119, y));
    }
    assert(!focus_ring(19, 50) && !focus_ring(120, 50));
    // Keep both edges visible on action blue, dark photos, and light cards.
    for (const uint32_t background : {0x0b82ff, 0x20262d, 0xffffff}) {
      lv_obj_set_style_bg_color(photo, lv_color_hex(background), 0);
      lv_refr_now(fixture.display);
      assert(focus_ring(70, 21) && focus_ring(70, 88));
      assert(focus_ring(21, 50) && focus_ring(118, 50));
      for (const auto point : {lv_point_t{70, 22}, lv_point_t{70, 87},
                               lv_point_t{22, 50}, lv_point_t{117, 50}}) {
        const auto &pixel = frame[point.y * size.x + point.x];
        assert(pixel.red == 0xff && pixel.green == 0xff && pixel.blue == 0xff);
      }
      const auto &center = frame[50 * size.x + 70];
      assert(center.red == ((background >> 16) & 0xff));
      assert(center.green == ((background >> 8) & 0xff));
      assert(center.blue == (background & 0xff));
    }
    // Optional artifact used for visual inspection alongside pixel assertions.
    if (const char *path = getenv("LILYGO_FOCUS_RING_PPM")) {
      std::ofstream file(path, std::ios::binary);
      file << "P6\n" << size.x << ' ' << size.y << "\n255\n";
      for (const auto &pixel : frame) {
        file.put(pixel.red); file.put(pixel.green); file.put(pixel.blue);
      }
    }
  }

  // Use the app's switch size and indicator color, retaining the theme's
  // rounded track and knob. Focus must remain visible in either switch state.
  auto *toggle = lv_switch_create(lv_screen_active());
  lv_obj_set_pos(toggle, 140, 20);
  lv_obj_set_size(toggle, 48, 28);
  lv_obj_set_style_anim_duration(toggle, 0, 0);
  lv_obj_set_style_bg_color(toggle, lv_color_hex(0x0b82ff),
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  input_housekeeping(fixture.timer);
  lv_display_set_resolution(fixture.display, 320, 568);
  frame.assign(320 * 568, {});
  lv_obj_update_layout(toggle);
  const auto dark = [&](int x, int y) {
    const auto &pixel = frame[y * 320 + x];
    return pixel.red == 0x25 && pixel.green == 0x29 && pixel.blue == 0x32;
  };
  for (const bool checked : {false, true}) {
    lv_obj_set_state(toggle, LV_STATE_CHECKED, checked);
    lv_group_focus_obj(tile);
    lv_refr_now(fixture.display);
    assert(!dark(164, 21));
    lv_group_focus_obj(toggle);
    lv_refr_now(fixture.display);
    assert(dark(164, 21) && dark(164, 46));
    assert(dark(141, 34) && dark(186, 34));
    assert(!dark(139, 34) && !dark(188, 34));
  }
}

static void verify_native_focus_outline() {
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
  frame.resize(480 * 640);
  auto *other = plain_control(lv_screen_active(), 20, 20);
  input_housekeeping(fixture.timer);
  // An explicit local focus outline keeps the native LVGL treatment. Exercise
  // both selectors through real keypad focus and leave room on all four sides.
  for (const auto selector : {LV_STATE_FOCUS_KEY, LV_STATE_FOCUSED}) {
    auto *button = lv_button_create(lv_screen_active());
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, 200, 150);
    lv_obj_set_size(button, 120, 60);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x303030), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    const int width = lv_display_dpx(fixture.display, 3);
    lv_obj_set_style_outline_width(button, width, selector);
    lv_obj_set_style_outline_pad(button, width, selector);
    lv_obj_set_style_outline_color(button, lv_theme_get_color_primary(button), selector);
    lv_obj_set_style_outline_opa(button, LV_OPA_50, selector);
    input_housekeeping(fixture.timer);
    lv_obj_update_layout(button);
    lv_refr_now(fixture.display);
    const auto pixel = [&](int x, int y) -> const lv_color32_t & {
      return frame[y * lv_display_get_horizontal_resolution(fixture.display) + x];
    };
    const auto same_rgb = [](const lv_color32_t &a, const lv_color32_t &b) {
      return a.red == b.red && a.green == b.green && a.blue == b.blue;
    };
    lv_area_t bounds;
    lv_obj_get_coords(button, &bounds);
    const int cx = (bounds.x1 + bounds.x2) / 2;
    const int cy = (bounds.y1 + bounds.y2) / 2;
    const lv_point_t outside[] = {{bounds.x1 - width - 1, cy},
                                  {bounds.x2 + width + 1, cy},
                                  {cx, bounds.y1 - width - 1},
                                  {cx, bounds.y2 + width + 1}};
    std::vector<lv_color32_t> unfocused;
    for (const auto &point : outside) unfocused.push_back(pixel(point.x, point.y));
    press_key(fixture, LV_KEY_NEXT);
    assert(lv_group_get_focused(fixture.group) == button);
    assert(lv_obj_has_state(button, LV_STATE_FOCUS_KEY));
    input_housekeeping(fixture.timer);
    assert(lv_obj_get_style_outline_opa(button, LV_PART_MAIN) == LV_OPA_50);
    lv_refr_now(fixture.display);
    for (size_t i = 0; i < unfocused.size(); ++i)
      assert(!same_rgb(pixel(outside[i].x, outside[i].y), unfocused[i]));
    // The gap remains clear and the original fill reaches every inside edge;
    // the runtime must not add its default inner stroke.
    assert(same_rgb(pixel(bounds.x1 - 1, cy), unfocused[0]));
    for (const auto &point : {lv_point_t{bounds.x1, cy}, lv_point_t{bounds.x2, cy},
                              lv_point_t{cx, bounds.y1}, lv_point_t{cx, bounds.y2}}) {
      const auto &color = pixel(point.x, point.y);
      assert(color.red == 0x30 && color.green == 0x30 && color.blue == 0x30);
    }
    press_key(fixture, LV_KEY_NEXT);
    assert(lv_group_get_focused(fixture.group) == other);
    lv_refr_now(fixture.display);
    for (size_t i = 0; i < unfocused.size(); ++i)
      assert(same_rgb(pixel(outside[i].x, outside[i].y), unfocused[i]));
    lv_obj_delete(button);
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
  else if (strcmp(argv[1], "navigation") == 0) {
    verify_navigation();
    verify_unmarked_scroll_navigation();
    verify_switch_keys();
  }
  else if (strcmp(argv[1], "browse-region") == 0)
    verify_browse_region();
  else if (strcmp(argv[1], "focus-cleanup") == 0)
    verify_focus_cleanup();
  else if (strcmp(argv[1], "scope") == 0)
    verify_modal_scope();
  else if (strcmp(argv[1], "native-focus-outline") == 0)
    verify_native_focus_outline();
  else if (strcmp(argv[1], "focus-ring") == 0)
    verify_focus_ring();
  else
    return 2;
  return 0;
}
