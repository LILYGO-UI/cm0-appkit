#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LV_USE_DRAW_OPENGLES 1
#define LV_UNUSED(value) ((void)(value))
#define LV_LOG_ERROR(...) ((void)0)
#define GL_CALL(call) call
#define GL_FRAMEBUFFER 1
#define EGL_NO_CONTEXT NULL
#define EGL_NO_DISPLAY NULL
#define EGL_NO_SURFACE NULL
#define LV_EVENT_DELETE 1
#define LV_EVENT_RESOLUTION_CHANGED 2
#define LV_RESULT_OK 0
#define LV_DRAW_TASK_TYPE_IMAGE 1
#define LV_COLOR_FORMAT_PROPRIETARY_START 100
#define DRAW_UNIT_ID_OPENGLES 3

typedef int lv_result_t;
typedef int lv_event_code_t;
typedef int lv_draw_unit_t;
typedef struct { void *user_data; } lv_draw_dsc_base_t;
typedef struct { lv_draw_dsc_base_t base; struct { int cf; } header; } lv_draw_image_dsc_t;
typedef struct { int type; void *draw_dsc; int preference_score; int preferred_draw_unit_id; } lv_draw_task_t;
typedef struct { void *user_data; } lv_layer_t;
typedef struct { lv_layer_t *layer_head; void *driver_data; } lv_display_t;
typedef struct { int code; lv_display_t *display; } lv_event_t;
typedef struct { unsigned int texture_id; } lv_opengles_texture_t;
typedef struct {
    struct { void *unaligned_data; } render_draw_buf;
    void *texture_cache;
    unsigned int framebuffer;
} lv_draw_opengles_unit_t;
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
typedef struct {
    lv_opengles_texture_t texture;
    lv_opengles_egl_t *egl_ctx;
} lv_drm_ctx_t;

static lv_draw_opengles_unit_t unit;
static lv_draw_opengles_unit_t *g_unit;
static lv_display_t *refreshing;
static bool current;
static bool cache_has_texture;
static int cache_drops;
static int framebuffer_deletes;
static int drm_restores;
static int drm_deletes;
static int texture_deletes;
static int context_deletes;
static int library_closes;
static char calls[128];

static void record(char call) {
    size_t length = strlen(calls);
    assert(length + 1 < sizeof(calls));
    calls[length] = call;
    calls[length + 1] = '\0';
}
static void lv_free(void *pointer) { free(pointer); }
static void lv_cache_drop_all(void *cache, void *user_data) {
    assert(cache == &unit && user_data == &unit);
    if (cache_has_texture) {
        assert(current);
        cache_has_texture = false;
        cache_drops++;
        record('c');
    }
}
static void lv_cache_destroy(void *cache, void *user_data) {
    assert(cache == &unit && user_data == &unit && !cache_has_texture);
}
static void glBindFramebuffer(int target, unsigned int framebuffer) {
    assert(current && target == GL_FRAMEBUFFER && framebuffer == 0);
}
static void glDeleteFramebuffers(int count, unsigned int *framebuffer) {
    assert(current && count == 1 && *framebuffer != 0);
    framebuffer_deletes++;
    record('f');
}
static lv_display_t *lv_refr_get_disp_refreshing(void) { return refreshing; }
#include "draw_reset.inc"
#include "draw_deinit.inc"
#include "evaluate.inc"

static int make_current(void *display, void *draw, void *read, void *context) {
    assert(display != NULL && draw == read);
    current = context != NULL;
    record(current ? 'M' : 'm');
    return 1;
}
static int (*eglMakeCurrent)(void *, void *, void *, void *) = make_current;
static void lv_opengles_deinit(void) { assert(current); record('g'); }
static int eglDestroyContext(void *display, void *context) {
    assert(display != NULL && context != NULL && !current);
    context_deletes++;
    record('C');
    return 1;
}
static int eglDestroySurface(void *display, void *surface) {
    assert(display != NULL && surface != NULL && !current);
    record('S');
    return 1;
}
static void destroy_window(void *driver_data, void *window) {
    assert(driver_data != NULL && window != NULL && !current);
    record('W');
}
static int terminate_display(void *display) {
    assert(display != NULL && library_closes == 0);
    record('T');
    return 1;
}
static int (*eglTerminate)(void *) = terminate_display;
static int dlclose(void *handle) {
    assert(handle != NULL && strchr(calls, 'T') != NULL);
    library_closes++;
    record('L');
    return 0;
}
#include "egl_destroy.inc"

static lv_event_code_t lv_event_get_code(lv_event_t *event) { return event->code; }
static void *lv_event_get_target(lv_event_t *event) { return event->display; }
static void *lv_display_get_driver_data(lv_display_t *display) { return display->driver_data; }
static void lv_display_set_driver_data(lv_display_t *display, void *data) { display->driver_data = data; }
static void drm_restore_crtc(lv_drm_ctx_t *ctx) { assert(ctx != NULL); drm_restores++; record('R'); }
static void drm_device_deinit(lv_drm_ctx_t *ctx) { assert(!current); drm_deletes++; record('D'); free(ctx); }
static void lv_opengles_texture_deinit(lv_opengles_texture_t *texture) {
    assert(current && texture->texture_id != 0);
    texture_deletes++;
    record('t');
}
static lv_result_t lv_opengles_texture_reshape(lv_opengles_texture_t *texture, lv_display_t *display, int width, int height) {
    LV_UNUSED(texture); LV_UNUSED(display); LV_UNUSED(width); LV_UNUSED(height);
    return LV_RESULT_OK;
}
static int lv_display_get_horizontal_resolution(lv_display_t *display) { LV_UNUSED(display); return 480; }
static int lv_display_get_vertical_resolution(lv_display_t *display) { LV_UNUSED(display); return 800; }
#include "drm_event.inc"

static lv_opengles_egl_t *new_context(bool initialized) {
    lv_opengles_egl_t *ctx = calloc(1, sizeof(*ctx));
    assert(ctx != NULL);
    ctx->native_window = &unit;
    ctx->egl_display = &unit;
    ctx->egl_context = &unit;
    ctx->egl_surface = &unit;
    ctx->egl_lib_handle = &unit;
    ctx->opengl_lib_handle = &unit;
    ctx->interface.destroy_window_cb = destroy_window;
    ctx->interface.driver_data = &unit;
    ctx->gl_initialized = initialized;
    return ctx;
}

int main(void) {
    g_unit = &unit;
    unit.texture_cache = &unit;
    unit.render_draw_buf.unaligned_data = malloc(16);
    assert(unit.render_draw_buf.unaligned_data != NULL);

    lv_layer_t software_layer = {0};
    lv_display_t display = {&software_layer, NULL};
    lv_draw_dsc_base_t descriptor = {0};
    lv_draw_task_t task = {0, &descriptor, 50, 1};
    refreshing = &display;
    evaluate(NULL, &task);
    assert(task.preferred_draw_unit_id == 1 && task.preference_score == 50);
    software_layer.user_data = &unit;
    evaluate(NULL, &task);
    assert(task.preferred_draw_unit_id == DRAW_UNIT_ID_OPENGLES);
    descriptor.user_data = &unit;
    task.preferred_draw_unit_id = 1;
    evaluate(NULL, &task);
    assert(task.preferred_draw_unit_id == 1);
    refreshing = NULL;

    for (int iteration = 0; iteration < 2; ++iteration) {
        calls[0] = '\0';
        library_closes = 0;
        current = true;
        cache_has_texture = true;
        unit.framebuffer = 42;
        lv_drm_ctx_t *drm = calloc(1, sizeof(*drm));
        assert(drm != NULL);
        drm->texture.texture_id = 9;
        drm->egl_ctx = new_context(true);
        display.driver_data = drm;
        lv_event_t event = {LV_EVENT_DELETE, &display};
        event_cb(&event);
        assert(strcmp(calls, "RtMcfgmCSWTLLD") == 0);
        assert(display.driver_data == NULL && g_unit == &unit);
        assert(unit.framebuffer == 0 && !cache_has_texture);
        event_cb(&event);
        assert(strcmp(calls, "RtMcfgmCSWTLLD") == 0);
    }
    assert(cache_drops == 2 && framebuffer_deletes == 2);
    assert(drm_restores == 2 && drm_deletes == 2 && texture_deletes == 2);

    calls[0] = '\0';
    library_closes = 0;
    lv_opengles_egl_context_destroy(new_context(false));
    assert(strcmp(calls, "mCSWTLL") == 0);
    assert(context_deletes == 3 && cache_drops == 2);
    lv_opengles_egl_context_destroy(NULL);
    lv_draw_opengles_deinit();
    assert(g_unit == NULL);
    lv_draw_opengles_deinit();
    puts("DRM EGL lifecycle and software fallback tests passed");
    return 0;
}
