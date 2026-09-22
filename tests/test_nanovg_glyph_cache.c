#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LV_UNUSED(value) ((void)(value))
#define LV_PROFILER_DRAW_BEGIN ((void)0)
#define LV_PROFILER_DRAW_END ((void)0)
#define LV_PROFILER_DRAW_BEGIN_TAG(value) ((void)0)
#define LV_PROFILER_DRAW_END_TAG(value) ((void)0)
#define LV_LOG_ERROR(...) ((void)0)
#define LV_LOG_TRACE(...) ((void)0)
#define LV_COLOR_FORMAT_A8 8
#define NVG_TEXTURE_ALPHA 1
#define lv_memcpy memcpy
#define lv_memcmp memcmp

typedef struct {
    const void *resolved_font;
    uint16_t box_w;
    uint16_t box_h;
    void *entry;
} lv_font_glyph_dsc_t;
typedef struct {
    struct { uint32_t w, h, stride, cf; } header;
    uint8_t data[32];
} lv_draw_buf_t;
typedef struct { void *vg; } lv_draw_nanovg_unit_t;
typedef struct {
    lv_draw_nanovg_unit_t *u;
    lv_font_glyph_dsc_t g_dsc;
    int image_handle;
} letter_item_t;
typedef int lv_cache_compare_res_t;

enum test_case {
    CACHED_PADDED_BUFFER, SUPPLIED_BUFFER, ALLOCATION_FAILURE,
    BITMAP_FAILURE, UNSUPPORTED_FORMAT, SHORT_WIDTH, SHORT_HEIGHT,
    SHORT_STRIDE, UPLOAD_FAILURE
};

static enum test_case scenario;
static lv_draw_buf_t packed_buffer;
static lv_draw_buf_t cached_buffer;
static bool glyph_acquired;
static unsigned acquisitions;
static unsigned releases;
static unsigned uploads;
static unsigned deletes;
static int live_image;
static const uint8_t expected_pixels[] = {1, 2, 3, 4, 5, 6};

static lv_draw_buf_t *lv_nanovg_reshape_global_image(lv_draw_nanovg_unit_t *unit,
                                                    int format, uint32_t w, uint32_t h) {
    assert(unit && unit->vg && format == LV_COLOR_FORMAT_A8 && w == 3 && h == 2);
    if (scenario == ALLOCATION_FAILURE) return NULL;
    memset(&packed_buffer, 0x7c, sizeof(packed_buffer));
    packed_buffer.header.w = w;
    packed_buffer.header.h = h;
    packed_buffer.header.stride = w;
    packed_buffer.header.cf = format;
    return &packed_buffer;
}
static const void *lv_font_get_glyph_bitmap(lv_font_glyph_dsc_t *descriptor, lv_draw_buf_t *supplied) {
    assert(descriptor && !descriptor->entry && supplied == &packed_buffer && !glyph_acquired);
    ++acquisitions;
    glyph_acquired = true;
    descriptor->entry = &cached_buffer; /* Actual FreeType provider behavior. */
    if (scenario == BITMAP_FAILURE) return NULL;
    if (scenario == SUPPLIED_BUFFER) {
        memcpy(supplied->data, expected_pixels, sizeof(expected_pixels));
        return supplied;
    }
    memset(&cached_buffer, 0xd5, sizeof(cached_buffer));
    cached_buffer.header.w = 3;
    cached_buffer.header.h = 2;
    cached_buffer.header.stride = 5;
    cached_buffer.header.cf = LV_COLOR_FORMAT_A8;
    memcpy(cached_buffer.data, expected_pixels, 3);
    memcpy(cached_buffer.data + 5, expected_pixels + 3, 3);
    if (scenario == UNSUPPORTED_FORMAT) cached_buffer.header.cf = 32;
    if (scenario == SHORT_WIDTH) cached_buffer.header.w = 2;
    if (scenario == SHORT_HEIGHT) cached_buffer.header.h = 1;
    if (scenario == SHORT_STRIDE) cached_buffer.header.stride = 2;
    return &cached_buffer;
}
static void lv_font_glyph_release_draw_data(lv_font_glyph_dsc_t *descriptor) {
    assert(glyph_acquired && descriptor->entry == &cached_buffer);
    ++releases;
    glyph_acquired = false;
    descriptor->entry = NULL;
    /* The borrowed pixels cease to be valid when the glyph reference is released. */
    memset(cached_buffer.data, 0xe7, sizeof(cached_buffer.data));
}
static void *lv_draw_buf_goto_xy(const lv_draw_buf_t *buffer, uint32_t x, uint32_t y) {
    assert(buffer && x < buffer->header.w && y < buffer->header.h);
    return (void *)(buffer->data + y * buffer->header.stride + x);
}
static int nvgCreateImage(void *context, uint32_t w, uint32_t h,
                          int flags, int format, const uint8_t *pixels) {
    assert(context && glyph_acquired && w == 3 && h == 2 && flags == 0 && format == NVG_TEXTURE_ALPHA);
    assert(pixels == packed_buffer.data);
    assert(memcmp(pixels, expected_pixels, sizeof(expected_pixels)) == 0);
    assert(packed_buffer.data[sizeof(expected_pixels)] == 0x7c); /* No padding copied. */
    ++uploads;
    if (scenario == UPLOAD_FAILURE) return 0;
    assert(live_image == 0);
    live_image = 71;
    return live_image;
}
static void nvgDeleteImage(void *context, int image) {
    assert(context && image > 0 && image == live_image && !glyph_acquired);
    live_image = 0;
    ++deletes;
}

#include "letter_create.inc"
#include "letter_free.inc"
#include "letter_compare.inc"

int main(void) {
    lv_draw_nanovg_unit_t unit = {&unit};
    for (int value = CACHED_PADDED_BUFFER; value <= UPLOAD_FAILURE; ++value) {
        scenario = (enum test_case)value;
        acquisitions = releases = uploads = deletes = 0;
        letter_item_t item = {0};
        item.u = &unit;
        item.g_dsc.resolved_font = &unit;
        item.g_dsc.box_w = 3;
        item.g_dsc.box_h = 2;
        letter_item_t original = item;

        bool created = letter_create_cb(&item, NULL);
        bool expected_success = scenario == CACHED_PADDED_BUFFER || scenario == SUPPLIED_BUFFER;
        assert(created == expected_success);
        assert(!glyph_acquired && acquisitions == releases);
        /* Modifying entry used to corrupt the RB tree before the first eviction. */
        assert(memcmp(&original.g_dsc, &item.g_dsc, sizeof(item.g_dsc)) == 0);
        assert(letter_compare_cb(&item, &original) == 0);
        assert(acquisitions == (scenario != ALLOCATION_FAILURE));
        assert(uploads == (expected_success || scenario == UPLOAD_FAILURE));

        /* The cache also invokes free_cb after a failed create_cb. */
        letter_free_cb(&item, NULL);
        assert(item.image_handle == -1 && live_image == 0);
        assert(deletes == expected_success);
        letter_free_cb(&item, NULL);
        assert(deletes == expected_success);
    }
    puts("NanoVG glyph key immutability, padded bitmap upload and reference release tests passed");
    return 0;
}
