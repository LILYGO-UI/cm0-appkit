#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LV_ASSERT_NULL(value) assert((value) != NULL)
#define LV_UNUSED(value) ((void)(value))
#define LV_PROFILER_DRAW_BEGIN ((void)0)
#define LV_PROFILER_DRAW_END ((void)0)
#define LV_PROFILER_DRAW_BEGIN_TAG(value) ((void)0)
#define LV_PROFILER_DRAW_END_TAG(value) ((void)0)
#define LV_LOG_INFO(...) ((void)0)
#define LV_LOG_WARN(...) ((void)0)
#define LV_LOG_ERROR(...) ((void)0)
#define LV_RESULT_OK 0
#define LV_IMAGE_SRC_FILE 1
#define lv_memzero(pointer, size) memset(pointer, 0, size)

typedef int lv_result_t;
typedef int lv_image_src_t;
typedef uint32_t lv_color32_t;
typedef struct { uint32_t w, h; } lv_image_header_t;
typedef struct { lv_image_header_t header; void *data; } lv_draw_buf_t;
typedef struct { int unused; } lv_image_decoder_args_t;
typedef struct { lv_image_header_t header; const lv_draw_buf_t *decoded; } lv_image_decoder_dsc_t;
typedef struct { unsigned glyph_index; void *entry; } lv_font_glyph_dsc_t;
typedef struct _lv_draw_nanovg_unit_t lv_draw_nanovg_unit_t;
typedef struct {
    lv_draw_nanovg_unit_t *u;
    lv_font_glyph_dsc_t g_dsc;
    int image_handle;
} letter_item_t;
typedef struct {
    lv_draw_nanovg_unit_t *u;
    lv_draw_buf_t src_buf;
    lv_color32_t color;
    int image_flags;
    const void *src;
    lv_image_src_t src_type;
    int image_handle;
} image_item_t;
typedef struct {
    int refs;
    union { letter_item_t letter; image_item_t image; } item;
} lv_cache_entry_t;
typedef struct {
    bool is_image;
    bool occupied;
    lv_cache_entry_t entry;
} lv_cache_t;
typedef struct { unsigned count; lv_cache_entry_t *entries[16]; } lv_pending_t;
typedef struct {
    unsigned queued;
    unsigned submitted;
    unsigned flushes;
    int transform;
    int scissor;
} NVGcontext;
struct _lv_draw_nanovg_unit_t {
    NVGcontext *vg;
    bool is_started;
    void *current_layer;
    lv_cache_t *letter_cache;
    lv_cache_t *image_cache;
    lv_pending_t *letter_pending;
    lv_pending_t *image_pending;
};

static NVGcontext context;
static unsigned next_image;
static unsigned evictions;
static unsigned decoder_opens;
static unsigned decoder_closes;

static void nvgEndFrame(NVGcontext *vg) {
    assert(vg == &context);
    assert(vg->transform == 90 && vg->scissor == 37);
    vg->submitted += vg->queued;
    vg->queued = 0;
    ++vg->flushes;
}
static void lv_pending_remove_all(lv_pending_t *pending) {
    assert(context.queued == 0); /* Cache refs must survive until commands are submitted. */
    for (unsigned i = 0; i < pending->count; ++i) {
        assert(pending->entries[i]->refs > 0);
        --pending->entries[i]->refs;
    }
    pending->count = 0;
}
static void lv_pending_add(lv_pending_t *pending, void *entry) {
    assert(pending->count < 16);
    pending->entries[pending->count++] = *(lv_cache_entry_t **)entry;
}
static lv_cache_entry_t *lv_cache_acquire(lv_cache_t *cache, const void *key, void *user_data) {
    LV_UNUSED(cache); LV_UNUSED(key); LV_UNUSED(user_data);
    return NULL; /* Every lookup in the scene uses a distinct glyph/image. */
}
static size_t lv_cache_get_free_size(lv_cache_t *cache, void *user_data) {
    LV_UNUSED(user_data);
    return cache->occupied ? 0 : 1;
}
static lv_cache_entry_t *lv_cache_acquire_or_create(lv_cache_t *cache, const void *key, void *user_data) {
    LV_UNUSED(user_data);
    assert(cache->entry.refs == 0);
    if (cache->occupied) {
        assert(context.queued == 0);
        ++evictions;
    }
    cache->occupied = true;
    cache->entry.refs = 1;
    if (cache->is_image) {
        cache->entry.item.image = *(const image_item_t *)key;
        cache->entry.item.image.image_handle = ++next_image;
    }
    else {
        cache->entry.item.letter = *(const letter_item_t *)key;
        cache->entry.item.letter.image_handle = ++next_image;
    }
    return &cache->entry;
}
static void *lv_cache_entry_get_data(lv_cache_entry_t *entry) { return &entry->item; }
static lv_result_t lv_image_decoder_open(lv_image_decoder_dsc_t *decoder, const void *src,
                                         const lv_image_decoder_args_t *args) {
    LV_UNUSED(args);
    decoder->decoded = src;
    decoder->header = decoder->decoded->header;
    ++decoder_opens;
    return LV_RESULT_OK;
}
static void lv_image_decoder_close(lv_image_decoder_dsc_t *decoder) {
    assert(decoder->decoded != NULL);
    ++decoder_closes;
}
static lv_image_src_t lv_image_src_get_type(const void *src) { LV_UNUSED(src); return 0; }

#include "clean_up.inc"
#include "end_frame.inc"
#include "flush_pending.inc"
#include "letter_get.inc"
#include "image_get.inc"

int main(void) {
    lv_cache_t letters = {0};
    lv_cache_t images = {.is_image = true};
    lv_pending_t letter_pending = {0};
    lv_pending_t image_pending = {0};
    lv_draw_nanovg_unit_t unit = {
        .vg = &context, .is_started = true, .current_layer = &unit,
        .letter_cache = &letters, .image_cache = &images,
        .letter_pending = &letter_pending, .image_pending = &image_pending
    };
    context.transform = 90;
    context.scissor = 37;

    /* One uninterrupted draw task overflows both one-entry caches repeatedly.
     * The last command is queued after an overflow and must reach the final
     * end_frame, even if the draw dispatcher has no following task. */
    for (unsigned i = 0; i < 3; ++i) {
        lv_font_glyph_dsc_t glyph = {.glyph_index = i};
        assert(letter_get_image_handle(&unit, &glyph) > 0);
        assert(unit.is_started);
        ++context.queued;
        lv_draw_buf_t image = {{3, 2}, &unit};
        assert(lv_nanovg_image_cache_get_handle(&unit, &image, 0, 0, NULL) > 0);
        assert(unit.is_started);
        ++context.queued;
    }
    assert(evictions == 4 && context.queued == 1);
    assert(context.submitted == 5 && context.flushes == 4);
    lv_nanovg_end_frame(&unit);
    assert(!unit.is_started && context.queued == 0 && context.submitted == 6);
    assert(context.flushes == 5 && !letter_pending.count && !image_pending.count);
    assert(letters.entry.refs == 0 && images.entry.refs == 0);
    assert(context.transform == 90 && context.scissor == 37 && unit.current_layer == &unit);
    assert(decoder_opens == 3 && decoder_closes == 3);

    lv_nanovg_flush_pending(&unit);
    lv_nanovg_end_frame(&unit);
    assert(!unit.is_started && context.flushes == 5);
    puts("NanoVG cache-pressure submission and final-frame flush tests passed");
    return 0;
}
