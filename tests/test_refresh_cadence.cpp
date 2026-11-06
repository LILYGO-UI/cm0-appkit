#include <lvgl.h>
#include <cm0/typography.h>

#include <assert.h>
#include <cstdint>
#include <vector>

namespace {
constexpr uint32_t kPeriodMs = 20;
constexpr uint32_t kDurationMs = 1000;
uint32_t now_ms = 0;
std::vector<uint32_t> frames;
std::vector<uint32_t> animation_steps;
std::vector<uint32_t> input_reads;

void expect_50_hz(const std::vector<uint32_t> &samples) {
    assert(samples.size() == kDurationMs / kPeriodMs);
    for (size_t index = 0; index < samples.size(); ++index)
        assert(samples[index] == (index + 1) * kPeriodMs);
}
} // namespace

int main() {
    // A virtual clock avoids host scheduling, SDL, and physical vsync jitter.
    // Test the compiled LVGL behavior, including its global animation timer.
    lv_init();
    lv_tick_set_cb([]() { return now_ms; });
    lv_display_t *display = lv_display_create(32, 32);
    assert(display);
    uint32_t pixels[32 * 32]{};
    lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t *target,
                                        const lv_area_t *, uint8_t *) {
        lv_display_flush_ready(target);
    });
    lv_display_add_event_cb(display, [](lv_event_t *) {
        frames.push_back(now_ms);
    }, LV_EVENT_RENDER_START, nullptr);

    lv_indev_t *input = lv_indev_create();
    assert(input);
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input, [](lv_indev_t *, lv_indev_data_t *data) {
        input_reads.push_back(now_ms);
        data->state = LV_INDEV_STATE_RELEASED;
    });

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, display);
    lv_anim_set_exec_cb(&animation, [](void *, int32_t) {
        animation_steps.push_back(now_ms);
    });
    lv_anim_set_values(&animation, 0, 10000);
    lv_anim_set_duration(&animation, 10000);
    assert(lv_anim_start(&animation));

    // Remove initialization/early-apply callbacks from the measured interval.
    lv_timer_handler();
    frames.clear();
    animation_steps.clear();
    input_reads.clear();
    for (now_ms = 1; now_ms <= kDurationMs; ++now_ms) {
        lv_obj_invalidate(lv_screen_active());
        lv_timer_handler();
    }
    expect_50_hz(frames);
    expect_50_hz(animation_steps);
    expect_50_hz(input_reads);

    lv_anim_delete_all();
    lv_indev_delete(input);
    lv_display_delete(display);
    lilygo_ui_fonts_deinit();
    lv_deinit();
    return 0;
}
