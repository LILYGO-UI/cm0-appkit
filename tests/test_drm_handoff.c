#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LV_USE_DRAW_OPENGLES 1
#define LV_UNUSED(value) ((void)(value))
#define LV_LOG_ERROR(...) ((void)0)
#define LV_RESULT_OK 0
#define LV_RESULT_INVALID 1
typedef int lv_result_t;
typedef int lv_area_t;
typedef struct { int value; } drmModeModeInfo;
typedef struct {
    uint32_t crtc_id, buffer_id, x, y;
    bool mode_valid;
    drmModeModeInfo mode;
} drmModeCrtc;
typedef struct { uint32_t connector_id; } drmModeConnector;
typedef struct { uint32_t crtc_id; } drmModeEncoder;
struct gbm_bo { void *data; int releases; };
struct gbm_surface { int unused; };
typedef struct {
    int fd;
    struct gbm_bo *bo;
    uint32_t fb_id;
} drm_fb_state_t;
typedef struct {
    void *egl_ctx;
    struct gbm_surface *gbm_surface;
    struct gbm_bo *gbm_bo_pending, *gbm_bo_flipped, *gbm_bo_presented;
    drmModeCrtc *drm_crtc;
    drmModeConnector *drm_connector;
    drmModeEncoder *drm_encoder;
    drmModeModeInfo *drm_mode;
    int fd;
    bool crtc_isset, suspended;
} lv_drm_ctx_t;
typedef struct { lv_drm_ctx_t *data; } lv_display_t;

static lv_drm_ctx_t ctx;
static lv_display_t display;
static struct gbm_surface surface;
static struct gbm_bo front, next, pending;
static drm_fb_state_t front_fb, next_fb;
static drmModeCrtc saved;
static drmModeModeInfo mode;
static uint32_t clock_ms;
static int flip_status, flip_waits, flip_no_completion;
static int drops, drop_error, master_calls, master_busy, master_error;
static int modesets, modeset_error, frees, sleeps, swaps, draws, flushes;
static uint32_t last_fb;
static bool owns_master;

static void *lv_display_get_driver_data(lv_display_t *disp) { return disp->data; }
static uint32_t tick_cb(void) { return clock_ms; }
static void *gbm_bo_get_user_data(struct gbm_bo *bo) { return bo->data; }
static void gbm_surface_release_buffer(struct gbm_surface *s, struct gbm_bo *bo) {
    assert(s == &surface && bo != NULL);
    bo->releases++;
}
#include "page_flip.inc"
static int drm_do_page_flip(lv_drm_ctx_t *drm, int timeout_ms) {
    assert(drm == &ctx && timeout_ms > 0 && timeout_ms <= 1000);
    flip_waits++;
    if (flip_status > 0 && !flip_no_completion) drm_on_page_flip(5, 0, 0, 0, drm);
    else if (flip_no_completion) clock_ms += 600;
    return flip_status;
}
static int drmDropMaster(int fd) {
    assert(fd == 5);
    drops++;
    if (drop_error) { errno = drop_error; return -1; }
    owns_master = false;
    return 0;
}
static int drmSetMaster(int fd) {
    assert(fd == 5);
    master_calls++;
    if (master_error || master_calls <= master_busy) {
        errno = master_error ? master_error : EBUSY;
        return -1;
    }
    owns_master = true;
    return 0;
}
static int usleep(unsigned int microseconds) {
    assert(microseconds == 10000);
    sleeps++;
    return 0;
}
static int drmModeSetCrtc(int fd, uint32_t crtc, uint32_t fb, uint32_t x, uint32_t y,
                          uint32_t *connectors, int count, drmModeModeInfo *requested_mode) {
    assert(fd == 5 && crtc == 7 && owns_master);
    assert(x == 0 && y == 0 && connectors && *connectors == 44 && count == 1);
    assert(requested_mode == &mode || requested_mode == &saved.mode);
    modesets++;
    last_fb = fb;
    if (modeset_error) { errno = modeset_error; return -1; }
    return 0;
}
static void drmModeFreeCrtc(drmModeCrtc *crtc) { assert(crtc == &saved); frees++; }
static bool lv_display_flush_is_last(lv_display_t *disp) { assert(disp == &display); return true; }
static void lv_display_flush_ready(lv_display_t *disp) { assert(disp == &display); flushes++; }
static void set_viewport(lv_display_t *disp) { assert(disp == &display && !ctx.suspended); }
static void lv_opengles_render_display_texture(lv_display_t *disp, bool first, bool second) {
    assert(disp == &display && !first && second && !ctx.suspended); draws++;
}
static void lv_opengles_egl_update(void *egl) { assert(egl == &ctx && !ctx.suspended); swaps++; }
#include "suspend.inc"
#include "resume.inc"
#include "restore_crtc.inc"
#include "flush.inc"

static void reset(void) {
    static drmModeConnector connector;
    static drmModeEncoder encoder;
    memset(&ctx, 0, sizeof(ctx));
    memset(&front, 0, sizeof(front));
    memset(&next, 0, sizeof(next));
    memset(&pending, 0, sizeof(pending));
    memset(&saved, 0, sizeof(saved));
    connector.connector_id = 44;
    encoder.crtc_id = 7;
    ctx.drm_connector = &connector;
    ctx.drm_encoder = &encoder;
    ctx.egl_ctx = &ctx;
    ctx.gbm_surface = &surface;
    ctx.gbm_bo_presented = &front;
    ctx.drm_crtc = &saved;
    ctx.drm_mode = &mode;
    ctx.fd = 5;
    ctx.crtc_isset = true;
    display.data = &ctx;
    front_fb = (drm_fb_state_t){5, &front, 10};
    next_fb = (drm_fb_state_t){5, &next, 11};
    front.data = &front_fb;
    next.data = &next_fb;
    saved.crtc_id = 7;
    saved.buffer_id = 3;
    saved.mode_valid = true;
    clock_ms = 0;
    flip_status = 1;
    flip_waits = flip_no_completion = 0;
    drops = drop_error = master_calls = master_busy = master_error = 0;
    modesets = modeset_error = frees = sleeps = swaps = draws = flushes = 0;
    last_fb = 0;
    owns_master = true;
}

int main(void) {
    reset();
    assert(lv_linux_drm_suspend(NULL) == LV_RESULT_INVALID);
    assert(lv_linux_drm_resume(NULL) == LV_RESULT_INVALID);
    display.data = NULL;
    assert(lv_linux_drm_suspend(&display) == LV_RESULT_INVALID);
    assert(lv_linux_drm_resume(&display) == LV_RESULT_INVALID);
    display.data = &ctx;
    ctx.fd = -1;
    assert(lv_linux_drm_suspend(&display) == LV_RESULT_INVALID);
    ctx.fd = 5;
    ctx.gbm_bo_presented = NULL;
    assert(lv_linux_drm_suspend(&display) == LV_RESULT_INVALID && drops == 0);
    reset();
    assert(lv_linux_drm_resume(&display) == LV_RESULT_INVALID && master_calls == 0);
    ctx.egl_ctx = NULL;
    assert(lv_linux_drm_suspend(&display) == LV_RESULT_INVALID && drops == 0);

    reset();
    ctx.gbm_bo_flipped = &next;
    ctx.gbm_bo_pending = &pending;
    assert(lv_linux_drm_suspend(&display) == LV_RESULT_OK);
    assert(ctx.suspended && !ctx.crtc_isset && !owns_master && drops == 1);
    assert(flip_waits == 1 && front.releases == 1 && pending.releases == 1);
    assert(ctx.gbm_bo_presented == &next && next.releases == 0);
    assert(!ctx.gbm_bo_flipped && !ctx.gbm_bo_pending);
    assert(ctx.egl_ctx == &ctx && ctx.drm_crtc == &saved);
    assert(lv_linux_drm_suspend(&display) == LV_RESULT_INVALID && drops == 1);
    flush_cb(&display, NULL, NULL);
    assert(flushes == 1 && draws == 0 && swaps == 0);
    master_busy = 2;
    assert(lv_linux_drm_resume(&display) == LV_RESULT_OK);
    assert(master_calls == 3 && sleeps == 2 && modesets == 1 && last_fb == 11);
    assert(!ctx.suspended && ctx.crtc_isset && next.releases == 0);
    assert(ctx.drm_crtc == &saved && saved.buffer_id == 3);
    flush_cb(&display, NULL, NULL);
    assert(flushes == 2 && draws == 1 && swaps == 1);
    assert(lv_linux_drm_resume(&display) == LV_RESULT_INVALID && modesets == 1);
    drm_restore_crtc(&ctx);
    assert(modesets == 2 && last_fb == 3 && frees == 1 && !ctx.drm_crtc);

    reset();
    ctx.gbm_bo_flipped = &next;
    flip_status = 0;
    assert(lv_linux_drm_suspend(&display) == LV_RESULT_INVALID);
    assert(!ctx.suspended && owns_master && drops == 0 && front.releases == 0);
    reset();
    ctx.gbm_bo_flipped = &next;
    flip_no_completion = 1;
    assert(lv_linux_drm_suspend(&display) == LV_RESULT_INVALID);
    assert(flip_waits == 2 && drops == 0);

    reset();
    drop_error = EPERM;
    assert(lv_linux_drm_suspend(&display) == LV_RESULT_INVALID);
    assert(!ctx.suspended && ctx.crtc_isset && owns_master && front.releases == 0);

    reset();
    assert(lv_linux_drm_suspend(&display) == LV_RESULT_OK);
    master_busy = 100;
    assert(lv_linux_drm_resume(&display) == LV_RESULT_INVALID);
    assert(ctx.suspended && master_calls == 100 && sleeps == 99 && modesets == 0);
    drm_restore_crtc(&ctx);
    assert(modesets == 0 && frees == 1 && !ctx.drm_crtc && front.releases == 0);

    reset();
    assert(lv_linux_drm_suspend(&display) == LV_RESULT_OK);
    master_error = ENODEV;
    assert(lv_linux_drm_resume(&display) == LV_RESULT_INVALID);
    assert(ctx.suspended && master_calls == 1 && sleeps == 0);

    reset();
    assert(lv_linux_drm_suspend(&display) == LV_RESULT_OK);
    front_fb.fb_id = 0;
    assert(lv_linux_drm_resume(&display) == LV_RESULT_INVALID && master_calls == 0);
    front_fb.fb_id = 10;
    modeset_error = EINVAL;
    assert(lv_linux_drm_resume(&display) == LV_RESULT_INVALID);
    assert(ctx.suspended && !ctx.crtc_isset && !owns_master && drops == 2);
    assert(front.releases == 0 && ctx.egl_ctx == &ctx);
    modeset_error = 0;
    assert(lv_linux_drm_resume(&display) == LV_RESULT_OK);

    reset();
    for (int cycle = 0; cycle < 20; ++cycle) {
        assert(lv_linux_drm_suspend(&display) == LV_RESULT_OK);
        assert(lv_linux_drm_resume(&display) == LV_RESULT_OK);
        assert(front.releases == 0 && ctx.drm_crtc == &saved && ctx.egl_ctx == &ctx);
    }
    assert(modesets == 20 && drops == 20);
    puts("DRM handoff, retained buffers, failure recovery and timeout tests passed");
    return 0;
}
