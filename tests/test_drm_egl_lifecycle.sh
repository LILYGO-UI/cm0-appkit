#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: $0 LVGL_SOURCE_DIR [C_COMPILER]" >&2
    exit 2
fi

lvgl_source=$1
compiler=${2:-cc}
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/lilygo-egl-test.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM

patch_file="$test_dir/../patches/lvgl-9.5.0-drm-egl.patch"
for file in \
    src/draw/opengles/lv_draw_opengles.c \
    src/draw/opengles/lv_draw_opengles.h \
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
if patch --batch --silent --forward --dry-run -p1 -d "$lvgl_source" < "$patch_file" >/dev/null 2>&1; then
    patch --batch --silent --forward -p1 -d "$lvgl_source" < "$patch_file"
else
    patch --batch --silent --reverse --dry-run -p1 -d "$lvgl_source" < "$patch_file" >/dev/null
fi

# Compile the real patched lifecycle functions with deterministic device mocks.
extract_function() {
    awk -v declaration="$2" '
        $0 == declaration { copying = 1; found = 1 }
        copying { print }
        copying && /^}/ { exit }
        END { if (!found) exit 1 }
    ' "$1"
}

draw_source="$lvgl_source/src/draw/opengles/lv_draw_opengles.c"
egl_source="$lvgl_source/src/drivers/opengles/lv_opengles_egl.c"
drm_source="$lvgl_source/src/drivers/display/drm/lv_linux_drm_egl.c"
extract_function "$draw_source" 'void lv_draw_opengles_reset(void)' > "$scratch/draw_reset.inc"
extract_function "$draw_source" 'void lv_draw_opengles_deinit(void)' > "$scratch/draw_deinit.inc"
extract_function "$draw_source" 'static int32_t evaluate(lv_draw_unit_t * draw_unit, lv_draw_task_t * task)' > "$scratch/evaluate.inc"
extract_function "$egl_source" 'void lv_opengles_egl_context_destroy(lv_opengles_egl_t * ctx)' > "$scratch/egl_destroy.inc"
extract_function "$drm_source" 'static void event_cb(lv_event_t * e)' > "$scratch/drm_event.inc"

"$compiler" -std=c99 -Wall -Wextra -I "$scratch" "$test_dir/test_drm_egl_lifecycle.c" -o "$scratch/test-egl"
"$scratch/test-egl"
