#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define LV_ASSERT_NULL(value) assert((value) != NULL)
#define LV_LOG_WARN(...) ((void)0)
#define LV_LOG_TRACE(...) ((void)0)
#define LV_LOG_INFO(...) ((void)0)
#define LV_LOG_ERROR(...) ((void)0)
#define LV_MAX(a, b) ((a) > (b) ? (a) : (b))
#define MAX_TOUCH_POINTS 5
enum { EV_SYN, EV_KEY, EV_REL, EV_ABS };
enum { SYN_REPORT, REL_X = 0, REL_Y = 1, ABS_X = 0, ABS_Y = 1 };
enum { ABS_MT_SLOT = 47, ABS_MT_POSITION_X = 53, ABS_MT_POSITION_Y = 54, ABS_MT_TRACKING_ID = 57 };
enum { BTN_MOUSE = 272, BTN_TOUCH = 330, KEY_ENTER = 28 };
enum { LV_INDEV_TYPE_POINTER, LV_INDEV_TYPE_KEYPAD };
enum { LV_INDEV_STATE_RELEASED, LV_INDEV_STATE_PRESSED };

struct input_event { int64_t seconds, microseconds; uint16_t type, code; int32_t value; };
typedef struct { int x, y; } lv_point_t;
typedef struct { lv_point_t point; int state; int id; } lv_indev_touch_data_t;
typedef struct {
    int fd, root_x, root_y, key, state;
    bool deleting;
#if LV_USE_GESTURE_RECOGNITION
    lv_indev_touch_data_t touch_data[MAX_TOUCH_POINTS];
    uint8_t touch_count, current_slot;
    bool touch_data_changed;
#endif
} lv_evdev_t;
typedef struct { int type; lv_evdev_t *driver; } lv_indev_t;
typedef struct { bool continue_reading; int state, key; lv_point_t point; } lv_indev_data_t;
typedef struct { ssize_t bytes; int error; struct input_event event; } queued_read_t;

static queued_read_t queue[64];
static unsigned queued, consumed, deleted;
#if LV_USE_GESTURE_RECOGNITION
static unsigned gesture_updates, gesture_results;
#endif

static void enqueue_event(uint16_t type, uint16_t code, int32_t value) {
    assert(queued < 64);
    queue[queued++] = (queued_read_t){sizeof(struct input_event), 0, {0, 0, type, code, value}};
}
static void enqueue_error(int error) {
    assert(queued < 64);
    queue[queued++] = (queued_read_t){.bytes = -1, .error = error};
}
static ssize_t mocked_read(int fd, void *buffer, size_t size) {
    assert(fd == 7 && size == sizeof(struct input_event));
    if (consumed == queued) { errno = EAGAIN; return -1; }
    queued_read_t *item = &queue[consumed++];
    if (item->bytes < 0) { errno = item->error; return item->bytes; }
    assert((size_t)item->bytes <= size);
    memcpy(buffer, &item->event, (size_t)item->bytes);
    return item->bytes;
}
static void *lv_indev_get_driver_data(lv_indev_t *indev) { return indev->driver; }
static int lv_indev_get_type(lv_indev_t *indev) { return indev->type; }
static int _evdev_process_key(lv_evdev_t *driver, uint16_t code, int value) {
    (void)driver;
    (void)value;
    return code == KEY_ENTER ? 10 : 0;
}
static lv_point_t _evdev_process_pointer(lv_indev_t *indev, int x, int y) {
    assert(indev->type == LV_INDEV_TYPE_POINTER);
    return (lv_point_t){x, y};
}
static void _evdev_async_delete_cb(void *data) { assert(data != NULL); }
static void lv_async_call(void (*callback)(void *), void *data) {
    assert(callback == _evdev_async_delete_cb && data != NULL);
    ++deleted;
}
#if LV_USE_GESTURE_RECOGNITION
static void lv_indev_gesture_recognizers_update(lv_indev_t *indev,
                                                lv_indev_touch_data_t *touches, int count) {
    assert(indev && count > 0 && touches[0].point.x == 123 && touches[0].point.y == 456);
    ++gesture_updates;
}
static void lv_indev_gesture_recognizers_set_data(lv_indev_t *indev, lv_indev_data_t *data) {
    assert(indev && data);
    ++gesture_results;
}
#endif
#define read mocked_read
#include "evdev_read.inc"
#undef read

static void reset(lv_evdev_t *driver) {
    memset(driver, 0, sizeof(*driver));
    driver->fd = 7;
    queued = consumed = deleted = 0;
}
static lv_indev_data_t read_sample(lv_indev_t *indev) {
    lv_indev_data_t data = {0};
    _evdev_read(indev, &data);
    return data;
}

int main(void) {
    lv_evdev_t driver;
    lv_indev_t indev = {LV_INDEV_TYPE_POINTER, &driver};
    reset(&driver);

    /* A short tap and a move are already buffered when LVGL resumes rendering. */
    enqueue_event(EV_ABS, ABS_X, 100);
    enqueue_event(EV_ABS, ABS_Y, 200);
    enqueue_event(EV_KEY, BTN_TOUCH, 1);
    enqueue_event(EV_SYN, SYN_REPORT, 0);
    enqueue_event(EV_REL, REL_X, 3);
    enqueue_event(EV_REL, REL_Y, -2);
    enqueue_event(EV_SYN, SYN_REPORT, 0);
    enqueue_event(EV_KEY, BTN_TOUCH, 0);
    enqueue_event(EV_SYN, SYN_REPORT, 0);
    lv_indev_data_t sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_PRESSED && sample.point.x == 100 && sample.point.y == 200);
    assert(sample.continue_reading && consumed == 4);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_PRESSED && sample.point.x == 103 && sample.point.y == 198);
    assert(sample.continue_reading && consumed == 7);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_RELEASED && sample.point.x == 103 && sample.point.y == 198);
    assert(sample.continue_reading && consumed == 9);
    sample = read_sample(&indev);
    assert(!sample.continue_reading && sample.state == LV_INDEV_STATE_RELEASED && !deleted);

    /* The keypad continues reporting key transitions without stopping at SYN. */
    reset(&driver);
    indev.type = LV_INDEV_TYPE_KEYPAD;
    enqueue_event(EV_KEY, KEY_ENTER, 1);
    enqueue_event(EV_SYN, SYN_REPORT, 0);
    enqueue_event(EV_KEY, KEY_ENTER, 0);
    enqueue_event(EV_SYN, SYN_REPORT, 0);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_PRESSED && sample.key == 10 && consumed == 1);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_RELEASED && sample.key == 10 && consumed == 3);
    sample = read_sample(&indev);
    assert(!sample.continue_reading && consumed == 4 && !deleted);

    /* Signal interruption must not delete a connected input device. */
    reset(&driver);
    indev.type = LV_INDEV_TYPE_POINTER;
    enqueue_error(EINTR);
    enqueue_event(EV_KEY, BTN_MOUSE, 1);
    enqueue_event(EV_SYN, SYN_REPORT, 0);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_PRESSED && sample.continue_reading && consumed == 3 && !deleted);

    /* Ignore a malformed partial event rather than synthesizing a press. */
    reset(&driver);
    enqueue_event(EV_KEY, BTN_TOUCH, 1);
    queue[0].bytes = sizeof(struct input_event) - 1;
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_RELEASED && !sample.continue_reading && !deleted);

    reset(&driver);
    enqueue_error(ENODEV);
    sample = read_sample(&indev);
    assert(driver.deleting && deleted == 1 && !sample.continue_reading);
    enqueue_error(ENODEV);
    read_sample(&indev);
    assert(deleted == 1);

#if LV_USE_GESTURE_RECOGNITION
    reset(&driver);
    enqueue_event(EV_ABS, ABS_MT_SLOT, 0);
    enqueue_event(EV_ABS, ABS_MT_TRACKING_ID, 42);
    enqueue_event(EV_ABS, ABS_MT_POSITION_X, 123);
    enqueue_event(EV_ABS, ABS_MT_POSITION_Y, 456);
    enqueue_event(EV_SYN, SYN_REPORT, 0);
    enqueue_event(EV_ABS, ABS_MT_TRACKING_ID, -1);
    enqueue_event(EV_SYN, SYN_REPORT, 0);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_PRESSED && sample.point.x == 123 && sample.point.y == 456);
    assert(sample.continue_reading && consumed == 5 && gesture_updates == 1 && gesture_results == 1);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_RELEASED && sample.continue_reading && consumed == 7);
#endif
    printf("Evdev sample boundaries, keys and read errors passed (gestures=%d)\n", LV_USE_GESTURE_RECOGNITION);
    return 0;
}
