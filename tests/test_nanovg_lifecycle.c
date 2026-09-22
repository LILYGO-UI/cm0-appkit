#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the extracted production lifecycle with a current-context contract.
 * Every cache contains a GPU object and pending references, so releasing GL
 * resources too late or destroying referenced caches makes the test fail. */
#define LV_USE_DRAW_NANOVG 1
#define LV_USE_DRAW_OPENGLES 0
#define LV_USE_VECTOR_GRAPHIC 0
#define LV_USE_3DTEXTURE 0
#define NANOVG_DRAW_UNIT_ID 10
#define LV_UNUSED(value) ((void)(value))
#define LV_ASSERT_NULL(value) assert((value) != NULL)
#define LV_ASSERT_MALLOC(value) assert((value) != NULL)
#define LV_ASSERT_MSG(value, message) assert(value)
#define LV_ASSERT(value) assert(value)
#define LV_LOG_ERROR(...) ((void)0)
#define EGL_NO_CONTEXT NULL
#define EGL_NO_DISPLAY NULL
#define EGL_NO_SURFACE NULL
#define NVG_ANTIALIAS 1
#define NVG_CTX_CREATE create_nanovg
#define NVG_CTX_DELETE delete_nanovg

enum {
    LV_DRAW_TASK_TYPE_FILL, LV_DRAW_TASK_TYPE_BORDER,
    LV_DRAW_TASK_TYPE_BOX_SHADOW, LV_DRAW_TASK_TYPE_LETTER,
    LV_DRAW_TASK_TYPE_LABEL, LV_DRAW_TASK_TYPE_IMAGE,
    LV_DRAW_TASK_TYPE_LAYER, LV_DRAW_TASK_TYPE_LINE,
    LV_DRAW_TASK_TYPE_ARC, LV_DRAW_TASK_TYPE_TRIANGLE,
    LV_DRAW_TASK_TYPE_MASK_RECTANGLE
};

typedef struct { void *user_data; } lv_layer_t;
typedef struct { lv_layer_t *layer_head; } lv_display_t;
typedef struct { int type; int preference_score; int preferred_draw_unit_id; } lv_draw_task_t;
typedef int lv_event_t;
typedef struct lv_draw_unit_t lv_draw_unit_t;
struct lv_draw_unit_t {
    int32_t (*dispatch_cb)(lv_draw_unit_t *, lv_layer_t *);
    int32_t (*evaluate_cb)(lv_draw_unit_t *, lv_draw_task_t *);
    int32_t (*delete_cb)(lv_draw_unit_t *);
    void (*event_cb)(lv_event_t *);
    const char *name;
};
typedef struct { unsigned generation; } NVGcontext;
typedef struct {
    lv_draw_unit_t base_unit;
    lv_layer_t *current_layer;
    NVGcontext *vg;
    bool is_started;
    void *image_buf;
    void *image_cache;
    void *image_pending;
    void *letter_cache;
    void *letter_pending;
    void *fbo_cache;
} lv_draw_nanovg_unit_t;
typedef struct {
    void *native_window;
    void *egl_display;
    void *egl_context;
    void *egl_surface;
    void *egl_config;
    void *egl_lib_handle;
    void *opengl_lib_handle;
    struct { void (*destroy_window_cb)(void *, void *); void *driver_data; } interface;
    bool gl_initialized;
} lv_opengles_egl_t;

static lv_draw_nanovg_unit_t *g_unit;
static lv_display_t *refreshing;
static bool current;
static bool is_init;
static bool pending_refs;
static unsigned units_created;
static unsigned contexts_created;
static unsigned contexts_deleted;
static unsigned caches_deleted;
static char calls[256];

static void record(char call) {
    size_t length = strlen(calls);
    assert(length + 1 < sizeof(calls));
    calls[length] = call;
    calls[length + 1] = '\0';
}
static void *lv_draw_create_unit(size_t size) {
    assert(size == sizeof(lv_draw_nanovg_unit_t));
    ++units_created;
    void *unit = calloc(1, size);
    assert(unit != NULL);
    return unit;
}
static NVGcontext *create_nanovg(int flags) {
    LV_UNUSED(flags);
    assert(current);
    NVGcontext *context = calloc(1, sizeof(*context));
    assert(context != NULL);
    context->generation = ++contexts_created;
    record('n');
    return context;
}
static void delete_nanovg(NVGcontext *context) {
    assert(current && context != NULL);
    assert(!pending_refs && !g_unit->image_cache && !g_unit->letter_cache && !g_unit->fbo_cache);
    ++contexts_deleted;
    record('N');
    free(context);
}
static void nvgCancelFrame(NVGcontext *context) {
    assert(current && context != NULL && g_unit->is_started);
    record('C');
}
static void lv_nanovg_clean_up(lv_draw_nanovg_unit_t *unit) {
    assert(current && unit->vg && unit->image_pending && unit->letter_pending);
    pending_refs = false;
    unit->is_started = false;
    record('P');
}
static void lv_nanovg_utils_init(lv_draw_nanovg_unit_t *unit) {
    assert(current && unit->vg && !unit->image_buf);
    unit->image_buf = unit;
}
static void lv_nanovg_utils_deinit(lv_draw_nanovg_unit_t *unit) {
    assert(current && unit->image_buf);
    unit->image_buf = NULL;
    record('U');
}
static void lv_nanovg_image_cache_init(lv_draw_nanovg_unit_t *unit) {
    assert(current && unit->vg && !unit->image_cache && !unit->image_pending);
    unit->image_cache = unit;
    unit->image_pending = unit;
}
static void lv_nanovg_image_cache_deinit(lv_draw_nanovg_unit_t *unit) {
    assert(current && unit->vg && !pending_refs && unit->image_cache && unit->image_pending);
    unit->image_cache = NULL;
    unit->image_pending = NULL;
    ++caches_deleted;
    record('I');
}
static void lv_nanovg_fbo_cache_init(lv_draw_nanovg_unit_t *unit) {
    assert(current && unit->vg && !unit->fbo_cache);
    unit->fbo_cache = unit;
}
static void lv_nanovg_fbo_cache_deinit(lv_draw_nanovg_unit_t *unit) {
    assert(current && unit->vg && !pending_refs && unit->fbo_cache);
    unit->fbo_cache = NULL;
    ++caches_deleted;
    record('F');
}
static void lv_draw_nanovg_label_init(lv_draw_nanovg_unit_t *unit) {
    assert(current && unit->vg && !unit->letter_cache && !unit->letter_pending);
    unit->letter_cache = unit;
    unit->letter_pending = unit;
}
static void lv_draw_nanovg_label_deinit(lv_draw_nanovg_unit_t *unit) {
    assert(current && unit->vg && !pending_refs && unit->letter_cache && unit->letter_pending);
    unit->letter_cache = NULL;
    unit->letter_pending = NULL;
    ++caches_deleted;
    record('L');
}
static lv_display_t *lv_refr_get_disp_refreshing(void) { return refreshing; }
static int32_t draw_dispatch(lv_draw_unit_t *unit, lv_layer_t *layer) {
    LV_UNUSED(unit); LV_UNUSED(layer); return 0;
}
static void draw_event_cb(lv_event_t *event) { LV_UNUSED(event); }
static int32_t draw_delete(lv_draw_unit_t *unit);
#include "nanovg_evaluate.inc"
#include "nanovg_init.inc"
#include "nanovg_reset.inc"
#include "nanovg_delete.inc"

static void lv_opengles_shader_deinit(void) { assert(current); record('s'); }
static void lv_opengles_index_buffer_deinit(void) { assert(current); record('i'); }
static void lv_opengles_vertex_buffer_deinit(void) { assert(current); record('v'); }
static void lv_opengles_vertex_array_deinit(void) { assert(current); record('a'); }
#include "opengles_deinit.inc"

static int make_current(void *display, void *draw, void *read, void *context) {
    assert(display && draw == read);
    current = context != NULL;
    record(current ? 'M' : 'm');
    return 1;
}
static int (*eglMakeCurrent)(void *, void *, void *, void *) = make_current;
static int eglDestroyContext(void *display, void *context) {
    assert(display && context && !current && (!g_unit || !g_unit->vg));
    record('E'); return 1;
}
static int eglDestroySurface(void *display, void *surface) {
    assert(display && surface && !current); record('S'); return 1;
}
static void destroy_window(void *driver_data, void *window) {
    assert(driver_data && window && !current); record('W');
}
static int terminate_display(void *display) {
    assert(display && !current); record('T'); return 1;
}
static int (*eglTerminate)(void *) = terminate_display;
static int dlclose(void *handle) {
    assert(handle && strchr(calls, 'T')); record('D'); return 0;
}
static void lv_free(void *pointer) { free(pointer); }
#include "egl_destroy.inc"

static lv_opengles_egl_t *new_egl_context(bool initialized) {
    lv_opengles_egl_t *ctx = calloc(1, sizeof(*ctx));
    assert(ctx != NULL);
    ctx->native_window = ctx;
    ctx->egl_display = ctx;
    ctx->egl_context = ctx;
    ctx->egl_surface = ctx;
    ctx->egl_lib_handle = ctx;
    ctx->opengl_lib_handle = ctx;
    ctx->interface.destroy_window_cb = destroy_window;
    ctx->interface.driver_data = ctx;
    ctx->gl_initialized = initialized;
    return ctx;
}
static void assert_not_selected(lv_display_t *display) {
    refreshing = display;
    lv_draw_task_t task = {LV_DRAW_TASK_TYPE_FILL, 100, 0};
    draw_evaluate(&g_unit->base_unit, &task);
    assert(task.preference_score == 100 && task.preferred_draw_unit_id == 0);
}
static void assert_before(char first, char second) {
    const char *first_call = strchr(calls, first);
    const char *second_call = strchr(calls, second);
    assert(first_call && second_call && first_call < second_call);
}

int main(void) {
    lv_draw_nanovg_reset();
    assert(calls[0] == '\0');
    lv_layer_t layer = {0};
    lv_display_t display = {&layer};

    for (unsigned iteration = 0; iteration < 3; ++iteration) {
        current = true;
        is_init = true;
        calls[0] = '\0';
        lv_draw_nanovg_init();
        lv_draw_nanovg_unit_t *registered = g_unit;
        assert(registered && registered->vg);
        assert(registered->base_unit.evaluate_cb == draw_evaluate);
        assert(units_created == 1 && contexts_created == iteration + 1);
        lv_draw_nanovg_init();
        assert(g_unit == registered && contexts_created == iteration + 1);

        assert_not_selected(NULL);
        assert_not_selected(&display);
        layer.user_data = registered;
        refreshing = &display;
        lv_draw_task_t task = {LV_DRAW_TASK_TYPE_FILL, 100, 0};
        draw_evaluate(&registered->base_unit, &task);
        assert(task.preferred_draw_unit_id == NANOVG_DRAW_UNIT_ID);
        task.type = -1;
        task.preference_score = 100;
        task.preferred_draw_unit_id = 0;
        draw_evaluate(&registered->base_unit, &task);
        assert(task.preferred_draw_unit_id == 0 && task.preference_score == 100);

        registered->current_layer = &layer;
        registered->is_started = true;
        pending_refs = true;
        calls[0] = '\0';
        current = false; /* Destruction must restore this context first. */
        lv_opengles_egl_context_destroy(new_egl_context(true));
        assert(g_unit == registered && !registered->vg && !registered->current_layer);
        assert(!registered->is_started && !pending_refs && !is_init && !current);
        assert(contexts_deleted == iteration + 1 && caches_deleted == (iteration + 1) * 3);
        assert_before('M', 'C');
        assert_before('C', 'P');
        assert_before('P', 'L');
        assert_before('L', 'N');
        assert_before('F', 'N');
        assert_before('I', 'N');
        assert_before('N', 'm');
        assert_before('m', 'E');

        size_t call_count = strlen(calls);
        lv_draw_nanovg_reset();
        lv_opengles_deinit();
        assert(strlen(calls) == call_count);
        assert_not_selected(&display); /* Marker alone is insufficient after reset. */
        layer.user_data = NULL;
    }

    calls[0] = '\0';
    lv_opengles_egl_context_destroy(new_egl_context(false));
    assert(strcmp(calls, "mESWTDD") == 0);
    lv_opengles_egl_context_destroy(NULL);

    lv_draw_nanovg_unit_t *registered = g_unit;
    draw_delete(&registered->base_unit); /* After display/context destruction. */
    assert(g_unit == NULL);
    free(registered);
    lv_draw_nanovg_reset();

    /* A later complete LVGL restart must create a new registered unit. */
    current = true;
    lv_draw_nanovg_init();
    assert(units_created == 2 && contexts_created == 4);
    registered = g_unit;
    calls[0] = '\0';
    draw_delete(&registered->base_unit); /* Also valid while still initialized. */
    assert(!g_unit && contexts_deleted == 4 && caches_deleted == 12);
    assert(strchr(calls, 'C') == NULL); /* No frame was started. */
    free(registered);
    puts("NanoVG EGL lifecycle, unit reuse and software exclusion tests passed");
    return 0;
}
