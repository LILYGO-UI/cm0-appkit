#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define LV_USE_LINUX_DRM_GBM_BUFFERS 0
#define LV_RESULT_OK 0
#define LV_RESULT_INVALID 1
#define LV_EVENT_DELETE 1
#define LV_EVENT_RESOLUTION_CHANGED 2
#define LV_EVENT_REFR_START 3
#define LV_DISPLAY_RENDER_MODE_DIRECT 0
#define LV_DISPLAY_RENDER_MODE_FULL 1
#define LV_DISPLAY_ROTATION_0 0
#define LV_LOG_ERROR(...) ((void)0)
#define LV_LOG_INFO(...) ((void)0)
#define LV_MIN(a, b) ((a) < (b) ? (a) : (b))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define DRM_FOURCC 0
#define DRM_IOCTL_MODE_DESTROY_DUMB 0
#define BUFFER_CNT 2

typedef int lv_result_t;
typedef int lv_color_format_t;
typedef struct { uint32_t crtc_id, buffer_id; int mode; } drmModeCrtc;
typedef struct {
    uint32_t handle, fb_handle, pitch;
    size_t size;
    uint8_t *map;
} drm_buffer_t;
typedef struct {
    int fd;
    uint32_t width, height, mmWidth, blob_id;
    drmModeCrtc *saved_crtc;
    void *req, *conn, *crtc, *plane;
    uint32_t count_plane_props, count_crtc_props, count_conn_props;
    void *plane_props[1], *crtc_props[1], *conn_props[1];
    drm_buffer_t drm_bufs[BUFFER_CNT];
    uint8_t *render_buf;
    size_t render_buf_size;
} drm_dev_t;
typedef struct {
    drm_dev_t *driver_data;
    int32_t width, height;
    int rotation, mode, dpi;
    void *first_buffer, *second_buffer;
    size_t buffer_size;
} lv_display_t;
typedef struct { lv_display_t *display; drm_dev_t *user_data; int code; } lv_event_t;
struct drm_mode_destroy_dumb { uint32_t handle; };

static bool fail_render_mapping;
static bool fail_setup;
static int scratch_maps, scratch_unmaps, scanout_maps, scanout_unmaps;
static int device_closes, device_frees, heap_buffer_attempts;
static void *scratch_pointer;
static size_t scratch_size;
static void *scanout_pointers[BUFFER_CNT];
static size_t scanout_sizes[BUFFER_CNT];

/* Simulate the allocator condition seen after font/cache allocations fragment
 * LVGL's 8 MiB heap: small UI allocations fit, a full-screen block does not. */
void *lv_malloc(size_t size)
{
    if(size >= 512 * 1024) {
        heap_buffer_attempts++;
        return NULL;
    }
    return malloc(size);
}

static void lv_free(void *pointer)
{
    assert(pointer != scratch_pointer);
    device_frees++;
    free(pointer);
}

static void *render_mmap(void *address, size_t size, int protection, int flags, int fd, off_t offset)
{
    assert(!scratch_pointer && address == NULL && fd == -1 && offset == 0);
    assert((flags & MAP_ANONYMOUS) && (flags & MAP_PRIVATE));
    assert(protection == (PROT_READ | PROT_WRITE));
    if(fail_render_mapping) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    void *mapping = mmap(address, size, protection, flags, fd, offset);
    assert(mapping != MAP_FAILED);
    scratch_pointer = mapping;
    scratch_size = size;
    scratch_maps++;
    return mapping;
}

static int buffer_munmap(void *pointer, size_t size)
{
    assert(pointer && pointer != MAP_FAILED);
    if(pointer == scratch_pointer) {
        assert(size == scratch_size);
        scratch_pointer = NULL;
        scratch_unmaps++;
    }
    else {
        int index = pointer == scanout_pointers[0] ? 0 : 1;
        assert(pointer == scanout_pointers[index] && size == scanout_sizes[index]);
        scanout_pointers[index] = NULL;
        scanout_unmaps++;
    }
    return munmap(pointer, size);
}

static int close_device(int fd) { assert(fd == 17); device_closes++; return 0; }
static void *lv_display_get_driver_data(lv_display_t *display) { return display->driver_data; }
static void lv_display_set_driver_data(lv_display_t *display, void *data) { display->driver_data = data; }
static lv_display_t *lv_event_get_current_target(lv_event_t *event) { return event->display; }
static void *lv_event_get_user_data(lv_event_t *event) { return event->user_data; }
static int lv_event_get_code(lv_event_t *event) { return event->code; }
static int lv_display_get_rotation(lv_display_t *display) { return display->rotation; }
static lv_color_format_t lv_display_get_color_format(lv_display_t *display) { (void)display; return 4; }
static uint32_t lv_draw_buf_width_to_stride(int32_t width, lv_color_format_t format) { return width * format; }
static void lv_display_set_resolution(lv_display_t *display, int32_t width, int32_t height)
{
    display->width = width;
    display->height = height;
}
static void lv_display_set_buffers_with_stride(lv_display_t *display, void *first, void *second,
                                               size_t size, uint32_t stride, int mode)
{
    assert(stride == 568 * 4 && size >= (size_t)stride * 1232);
    assert(first == display->driver_data->drm_bufs[1].map);
    assert(second == display->driver_data->drm_bufs[0].map);
    display->first_buffer = first;
    display->second_buffer = second;
    display->buffer_size = size;
    display->mode = mode;
}
static void lv_display_set_buffers(lv_display_t *display, void *first, void *second, size_t size, int mode)
{
    assert(first == scratch_pointer && second == NULL && size == scratch_size);
    assert(size == (size_t)568 * 1232 * 4 && mode == LV_DISPLAY_RENDER_MODE_FULL);
    display->first_buffer = first;
    display->second_buffer = second;
    display->buffer_size = size;
    display->mode = mode;
}
static void lv_display_set_dpi(lv_display_t *display, int dpi) { display->dpi = dpi; }
static void drm_rotation_event_cb(lv_event_t *event);
static void drm_dmabuf_set_active_buf(lv_event_t *event) { (void)event; }
static void lv_display_add_event_cb(lv_display_t *display, void (*callback)(lv_event_t *), int code, void *data)
{
    assert(data == display->driver_data);
    assert((code == LV_EVENT_RESOLUTION_CHANGED && callback == drm_rotation_event_cb) ||
           (code == LV_EVENT_REFR_START && callback == drm_dmabuf_set_active_buf));
}
static void lv_display_set_flush_cb(lv_display_t *display, void *callback)
{
    assert(display && callback == NULL);
}

static int drm_setup(drm_dev_t *device, const char *file, int64_t connector, unsigned int fourcc)
{
    assert(file && connector == -1 && fourcc == DRM_FOURCC);
    if(fail_setup) return -1;
    device->fd = 17;
    device->width = 568;
    device->height = 1232;
    device->mmWidth = 70;
    return 0;
}
static int drm_setup_buffers(drm_dev_t *device)
{
    for(int i = 0; i < BUFFER_CNT; i++) {
        assert(!scanout_pointers[i]);
        device->drm_bufs[i].pitch = device->width * 4;
        device->drm_bufs[i].size = (size_t)device->drm_bufs[i].pitch * device->height;
        void *mapping = mmap(NULL, device->drm_bufs[i].size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(mapping != MAP_FAILED);
        device->drm_bufs[i].map = scanout_pointers[i] = mapping;
        scanout_sizes[i] = device->drm_bufs[i].size;
        scanout_maps++;
    }
    return 0;
}

/* No kernel DRM objects are created by these mocks. A cleanup attempt for an
 * unowned object indicates the tested driver passed an invalid handle. */
#define drmModeSetCrtc(fd, crtc, ...) do { (void)(crtc); assert(false); } while(0)
#define drmModeFreeCrtc(pointer) do { (void)(pointer); assert(false); } while(0)
#define drmModeAtomicFree(...) assert(false)
#define drmModeRmFB(...) assert(false)
#define drmIoctl(fd, request, argument) do { (void)(argument); assert(false); } while(0)
#define drmModeFreeProperty(...) assert(false)
#define drmModeDestroyPropertyBlob(...) assert(false)
#define drmModeFreeConnector(...) assert(false)
#define drmModeFreePlane(...) assert(false)
#define mmap render_mmap
#define munmap buffer_munmap
#define close close_device
#include "set_file.inc"
#include "rotation.inc"
#include "delete.inc"

static lv_display_t new_display(void)
{
    lv_display_t display = {0};
    display.driver_data = calloc(1, sizeof(drm_dev_t));
    assert(display.driver_data);
    display.driver_data->fd = -1;
    for(int i = 0; i < BUFFER_CNT; i++) display.driver_data->drm_bufs[i].map = MAP_FAILED;
    return display;
}

static void destroy_display(lv_display_t *display)
{
    lv_event_t event = {display, display->driver_data, LV_EVENT_DELETE};
    drm_del_event_cb(&event);
    assert(!display->driver_data && !scratch_pointer);
    assert(!scanout_pointers[0] && !scanout_pointers[1]);
    drm_del_event_cb(&event); /* Partial cleanup remains safe to repeat. */
}

int main(void)
{
    (void)render_mmap; /* Keep the same harness compilable against the old driver. */
    for(int cycle = 0; cycle < 64; cycle++) {
        lv_display_t display = new_display();
        assert(lv_linux_drm_set_file(&display, "/dev/dri/card0", -1) == LV_RESULT_OK);
        assert(heap_buffer_attempts == 0 && display.mode == LV_DISPLAY_RENDER_MODE_DIRECT);
        assert(scratch_size == (size_t)568 * 1232 * 4);
        lv_event_t event = {&display, display.driver_data, LV_EVENT_RESOLUTION_CHANGED};
        for(int rotation = 1; rotation <= 3; rotation++) {
            display.rotation = rotation;
            drm_rotation_event_cb(&event);
            assert(display.mode == LV_DISPLAY_RENDER_MODE_FULL);
            /* The complete logical framebuffer is writable at every rotation. */
            memset(display.first_buffer, cycle, display.buffer_size);
        }
        display.rotation = LV_DISPLAY_ROTATION_0;
        drm_rotation_event_cb(&event);
        assert(display.mode == LV_DISPLAY_RENDER_MODE_DIRECT);
        destroy_display(&display);
        assert(scratch_maps == scratch_unmaps && scanout_maps == scanout_unmaps);
    }
    assert(device_closes == 64 && device_frees == 64);

    fail_render_mapping = true;
    lv_display_t failed = new_display();
    assert(lv_linux_drm_set_file(&failed, "/dev/dri/card0", -1) == LV_RESULT_INVALID);
    assert(!failed.driver_data->render_buf);
    destroy_display(&failed);
    assert(device_closes == 65 && device_frees == 65);
    assert(scratch_maps == scratch_unmaps && scanout_maps == scanout_unmaps);

    fail_setup = true;
    lv_display_t unopened = new_display();
    assert(lv_linux_drm_set_file(&unopened, "/dev/dri/card0", -1) == LV_RESULT_INVALID);
    destroy_display(&unopened);
    assert(device_closes == 65 && device_frees == 66 && heap_buffer_attempts == 0);
    puts("Software DRM buffer ownership, constrained LVGL heap, 64 recreation cycles and mapping failure cleanup passed");
    return 0;
}
