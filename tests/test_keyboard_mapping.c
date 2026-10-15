#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

enum {
    EV_SYN = 0,
    EV_KEY = 1,
    EV_REL = 2,
    EV_ABS = 3,
    SYN_REPORT = 0,
    REL_X = 0,
    REL_Y = 1,
    ABS_X = 0,
    ABS_Y = 1,
    ABS_MT_POSITION_X = 53,
    ABS_MT_POSITION_Y = 54,
    ABS_MT_TRACKING_ID = 57,
    BTN_MOUSE = 272,
    BTN_TOUCH = 330,
    KEY_ESC = 1,
    KEY_1 = 2,
    KEY_2 = 3,
    KEY_3 = 4,
    KEY_4 = 5,
    KEY_5 = 6,
    KEY_6 = 7,
    KEY_7 = 8,
    KEY_8 = 9,
    KEY_9 = 10,
    KEY_0 = 11,
    KEY_MINUS = 12,
    KEY_EQUAL = 13,
    KEY_BACKSPACE = 14,
    KEY_TAB = 15,
    KEY_Q = 16,
    KEY_W = 17,
    KEY_E = 18,
    KEY_R = 19,
    KEY_T = 20,
    KEY_Y = 21,
    KEY_U = 22,
    KEY_I = 23,
    KEY_O = 24,
    KEY_P = 25,
    KEY_LEFTBRACE = 26,
    KEY_RIGHTBRACE = 27,
    KEY_ENTER = 28,
    KEY_LEFTCTRL = 29,
    KEY_A = 30,
    KEY_S = 31,
    KEY_D = 32,
    KEY_F = 33,
    KEY_G = 34,
    KEY_H = 35,
    KEY_J = 36,
    KEY_K = 37,
    KEY_L = 38,
    KEY_SEMICOLON = 39,
    KEY_APOSTROPHE = 40,
    KEY_GRAVE = 41,
    KEY_LEFTSHIFT = 42,
    KEY_BACKSLASH = 43,
    KEY_Z = 44,
    KEY_X = 45,
    KEY_C = 46,
    KEY_V = 47,
    KEY_B = 48,
    KEY_N = 49,
    KEY_M = 50,
    KEY_COMMA = 51,
    KEY_DOT = 52,
    KEY_SLASH = 53,
    KEY_RIGHTSHIFT = 54,
    KEY_LEFTALT = 56,
    KEY_CAPSLOCK = 58,
    KEY_RIGHTALT = 100,
    KEY_HOME = 102,
    KEY_UP = 103,
    KEY_LEFT = 105,
    KEY_RIGHT = 106,
    KEY_END = 107,
    KEY_DOWN = 108,
    KEY_DELETE = 111,
    KEY_RIGHTCTRL = 97,
    KEY_LEFTMETA = 125,
    KEY_RIGHTMETA = 126,
    KEY_NEXT = 407,
    KEY_PREVIOUS = 412,
    KEY_FN = 464,
    KEY_SPACE = 57,
};

#define LV_ASSERT_NULL(value) assert((value) != NULL)
#define LV_LOG_WARN(...) ((void)0)
#define LV_LOG_TRACE(...) ((void)0)
#define LV_LOG_INFO(...) ((void)0)
#define LV_LOG_ERROR(...) ((void)0)
#define LV_MAX(a, b) ((a) > (b) ? (a) : (b))

enum {
    LV_KEY_UP = 0x100,
    LV_KEY_DOWN,
    LV_KEY_RIGHT,
    LV_KEY_LEFT,
    LV_KEY_ESC,
    LV_KEY_DEL,
    LV_KEY_BACKSPACE,
    LV_KEY_ENTER,
    LV_KEY_NEXT,
    LV_KEY_PREV,
    LV_KEY_HOME,
    LV_KEY_END,
};
enum { LV_INDEV_TYPE_POINTER, LV_INDEV_TYPE_KEYPAD };
enum { LV_INDEV_STATE_RELEASED, LV_INDEV_STATE_PRESSED };

struct input_event {
    int64_t seconds, microseconds;
    uint16_t type, code;
    int32_t value;
};

typedef struct {
    int fd, root_x, root_y, key, state;
    bool deleting;
    bool left_shift, right_shift, caps;
    uint16_t active_key_code;
    int active_key;
} lv_evdev_t;
typedef struct { int x, y; } lv_point_t;
typedef struct { int type; lv_evdev_t *driver; } lv_indev_t;
typedef struct { bool continue_reading; int state, key; lv_point_t point; } lv_indev_data_t;
typedef struct { ssize_t bytes; int error; struct input_event event; } queued_read_t;

static queued_read_t queue[128];
static unsigned queued, consumed, deleted;

static void enqueue_event(uint16_t type, uint16_t code, int32_t value)
{
    assert(queued < 128);
    queue[queued++] = (queued_read_t){sizeof(struct input_event), 0,
                                      {0, 0, type, code, value}};
}

static ssize_t mocked_read(int fd, void *buffer, size_t size)
{
    assert(fd == 7 && size == sizeof(struct input_event));
    if (consumed == queued) {
        errno = EAGAIN;
        return -1;
    }
    queued_read_t *item = &queue[consumed++];
    assert(item->bytes == (ssize_t)sizeof(struct input_event));
    memcpy(buffer, &item->event, sizeof(struct input_event));
    return item->bytes;
}

static void *lv_indev_get_driver_data(lv_indev_t *indev) { return indev->driver; }
static int lv_indev_get_type(lv_indev_t *indev) { return indev->type; }
static lv_point_t _evdev_process_pointer(lv_indev_t *indev, int x, int y)
{
    (void)indev;
    return (lv_point_t){x, y};
}
static void _evdev_async_delete_cb(void *data) { assert(data != NULL); }
static void lv_async_call(void (*callback)(void *), void *data)
{
    assert(callback == _evdev_async_delete_cb && data != NULL);
    ++deleted;
}

#include "keyboard_process.inc"
#define read mocked_read
#include "evdev_read.inc"
#undef read

static void reset(lv_evdev_t *driver)
{
    memset(driver, 0, sizeof(*driver));
    driver->fd = 7;
    queued = consumed = deleted = 0;
}

static lv_indev_data_t read_sample(lv_indev_t *indev)
{
    lv_indev_data_t data = {0};
    _evdev_read(indev, &data);
    return data;
}

static void test_shift_tab_release_pairing(void)
{
    lv_evdev_t driver;
    lv_indev_t indev = {LV_INDEV_TYPE_KEYPAD, &driver};
    reset(&driver);

    enqueue_event(EV_KEY, KEY_LEFTSHIFT, 1);
    enqueue_event(EV_KEY, KEY_TAB, 1);
    lv_indev_data_t sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_PRESSED && sample.key == LV_KEY_PREV);

    /* Releasing Shift first must not turn Tab's release into LV_KEY_NEXT. */
    enqueue_event(EV_KEY, KEY_LEFTSHIFT, 0);
    enqueue_event(EV_KEY, KEY_TAB, 0);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_RELEASED && sample.key == LV_KEY_PREV);

    /* Either Shift key keeps the modifier active while the other is held. */
    reset(&driver);
    enqueue_event(EV_KEY, KEY_LEFTSHIFT, 1);
    enqueue_event(EV_KEY, KEY_RIGHTSHIFT, 1);
    enqueue_event(EV_KEY, KEY_LEFTSHIFT, 0);
    enqueue_event(EV_KEY, KEY_TAB, 1);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_PRESSED && sample.key == LV_KEY_PREV);
    enqueue_event(EV_KEY, KEY_RIGHTSHIFT, 0);
    enqueue_event(EV_KEY, KEY_TAB, 0);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_RELEASED && sample.key == LV_KEY_PREV);
}

static void test_modifier_release_does_not_release_key(void)
{
    lv_evdev_t driver;
    lv_indev_t indev = {LV_INDEV_TYPE_KEYPAD, &driver};
    reset(&driver);

    enqueue_event(EV_KEY, KEY_A, 1);
    lv_indev_data_t sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_PRESSED && sample.key == 'a');

    /* A modifier transition while A is held emits no synthetic release. */
    enqueue_event(EV_KEY, KEY_LEFTSHIFT, 1);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_PRESSED && sample.key == 'a');
    enqueue_event(EV_KEY, KEY_LEFTSHIFT, 0);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_PRESSED && sample.key == 'a');

    enqueue_event(EV_KEY, KEY_A, 0);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_RELEASED && sample.key == 'a');
}

static void test_caps_lock_and_shifted_text(void)
{
    lv_evdev_t driver;
    lv_indev_t indev = {LV_INDEV_TYPE_KEYPAD, &driver};
    reset(&driver);

    enqueue_event(EV_KEY, KEY_CAPSLOCK, 1);
    enqueue_event(EV_KEY, KEY_A, 1);
    lv_indev_data_t sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_PRESSED && sample.key == 'A');
    enqueue_event(EV_KEY, KEY_A, 0);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_RELEASED && sample.key == 'A');

    enqueue_event(EV_KEY, KEY_CAPSLOCK, 2);
    enqueue_event(EV_KEY, KEY_B, 1);
    sample = read_sample(&indev);
    assert(sample.state == LV_INDEV_STATE_PRESSED && sample.key == 'B');
}

int main(void)
{
    test_shift_tab_release_pairing();
    test_modifier_release_does_not_release_key();
    test_caps_lock_and_shifted_text();
    puts("Keyboard mapping, modifier state, and release pairing passed");
    return 0;
}
