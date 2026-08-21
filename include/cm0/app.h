#ifndef CM0_APP_H
#define CM0_APP_H

#include <stdint.h>

#include <cm0/typography.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CM0_APP_API_VERSION 1U

typedef struct cm0_app_context {
    lv_obj_t *root;
    void (*request_exit)(void *user_data);
    void *user_data;
} cm0_app_context_t;

typedef struct cm0_app_descriptor {
    uint32_t api_version;
    uint32_t struct_size;
    const char *id;
    void (*open)(const cm0_app_context_t *context);
    void (*close)(void);
} cm0_app_descriptor_t;

/* Runs one complete application process, including its own LVGL lifetime. */
int cm0_app_run(int argc, char **argv, const cm0_app_descriptor_t *descriptor);

#ifdef __cplusplus
}
#endif

#endif
