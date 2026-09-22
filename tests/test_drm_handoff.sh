#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: $0 LVGL_SOURCE_DIR [C_COMPILER]" >&2
    exit 2
fi

lvgl_source=$1
compiler=${2:-cc}
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/lilygo-drm-handoff.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM

for file in \
    src/draw/opengles/lv_draw_opengles.c \
    src/draw/opengles/lv_draw_opengles.h \
    src/drivers/display/drm/lv_linux_drm.h \
    src/drivers/display/drm/lv_linux_drm_egl.c \
    src/drivers/display/drm/lv_linux_drm_egl_private.h \
    src/drivers/opengles/lv_opengles_driver.c \
    src/drivers/opengles/lv_opengles_egl.c \
    src/drivers/opengles/lv_opengles_egl_private.h
do
    mkdir -p "$scratch/source/$(dirname "$file")"
    cp "$lvgl_source/$file" "$scratch/source/$file"
done
lvgl_source="$scratch/source"
for patch_name in drm-egl drm-handoff; do
    patch_file="$test_dir/../patches/lvgl-9.5.0-$patch_name.patch"
    if patch --batch --silent --forward --dry-run -p1 -d "$lvgl_source" < "$patch_file" >/dev/null 2>&1; then
        patch --batch --silent --forward -p1 -d "$lvgl_source" < "$patch_file"
    else
        patch --batch --silent --reverse --dry-run -p1 -d "$lvgl_source" < "$patch_file" >/dev/null
    fi
done

# Exercise the actual patched functions, with deterministic DRM/GBM mocks.
extract_function() {
    awk -v declaration="$2" '
        $0 == declaration { copying = 1; found = 1 }
        copying { print }
        copying && /^}/ { exit }
        END { if (!found) exit 1 }
    ' "$1"
}
drm_source="$lvgl_source/src/drivers/display/drm/lv_linux_drm_egl.c"
extract_function "$drm_source" 'lv_result_t lv_linux_drm_suspend(lv_display_t * disp)' > "$scratch/suspend.inc"
extract_function "$drm_source" 'lv_result_t lv_linux_drm_resume(lv_display_t * disp)' > "$scratch/resume.inc"
extract_function "$drm_source" 'static void drm_on_page_flip(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void * data)' > "$scratch/page_flip.inc"
extract_function "$drm_source" 'static void drm_restore_crtc(lv_drm_ctx_t * ctx)' > "$scratch/restore_crtc.inc"
extract_function "$drm_source" 'static void flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)' > "$scratch/flush.inc"

"$compiler" -std=c99 -Wall -Wextra -Werror -I "$scratch" "$test_dir/test_drm_handoff.c" -o "$scratch/test-handoff"
"$scratch/test-handoff"
