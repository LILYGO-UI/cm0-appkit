#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: $0 LVGL_SOURCE_DIR [C_COMPILER]" >&2
    exit 2
fi

lvgl_source=$1
compiler=${2:-cc}
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
patch_dir="$test_dir/../patches"
scratch=$(mktemp -d "${TMPDIR:-/tmp}/lilygo-nanovg-test.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM

base_patch="$patch_dir/lvgl-9.5.0-drm-egl.patch"
nanovg_patch="$patch_dir/lvgl-9.5.0-nanovg-drm.patch"

# Work on a copy: the same test runs against raw FetchContent sources and an
# already patched cross build, without modifying either source tree.
awk '/^\+\+\+ b\// { sub(/^b\//, "", $2); print $2 }' \
    "$base_patch" "$nanovg_patch" | sort -u > "$scratch/files"
while IFS= read -r file; do
    mkdir -p "$scratch/source/$(dirname "$file")"
    cp "$lvgl_source/$file" "$scratch/source/$file"
done < "$scratch/files"
lvgl_source="$scratch/source"

if ! patch --force --silent --reverse --dry-run -p1 -d "$lvgl_source" < "$nanovg_patch" >/dev/null 2>&1; then
    if patch --batch --silent --forward --dry-run -p1 -d "$lvgl_source" < "$base_patch" >/dev/null 2>&1; then
        patch --batch --silent --forward -p1 -d "$lvgl_source" < "$base_patch"
    else
        patch --force --silent --reverse --dry-run -p1 -d "$lvgl_source" < "$base_patch" >/dev/null
    fi
    patch --batch --silent --forward -p1 -d "$lvgl_source" < "$nanovg_patch"
fi

extract_function() {
    awk -v declaration="$2" '
        $0 == declaration { copying = 1; found = 1 }
        copying { print }
        copying && /^}/ { exit }
        END { if (!found) exit 1 }
    ' "$1"
}

draw_source="$lvgl_source/src/draw/nanovg/lv_draw_nanovg.c"
driver_source="$lvgl_source/src/drivers/opengles/lv_opengles_driver.c"
egl_source="$lvgl_source/src/drivers/opengles/lv_opengles_egl.c"
extract_function "$draw_source" 'void lv_draw_nanovg_init(void)' > "$scratch/nanovg_init.inc"
extract_function "$draw_source" 'void lv_draw_nanovg_reset(void)' > "$scratch/nanovg_reset.inc"
extract_function "$draw_source" 'static int32_t draw_delete(lv_draw_unit_t * draw_unit)' > "$scratch/nanovg_delete.inc"
extract_function "$draw_source" 'static int32_t draw_evaluate(lv_draw_unit_t * draw_unit, lv_draw_task_t * task)' > "$scratch/nanovg_evaluate.inc"
extract_function "$driver_source" 'void lv_opengles_deinit(void)' > "$scratch/opengles_deinit.inc"
extract_function "$egl_source" 'void lv_opengles_egl_context_destroy(lv_opengles_egl_t * ctx)' > "$scratch/egl_destroy.inc"

"$compiler" -std=c99 -Wall -Wextra -I "$scratch" "$test_dir/test_nanovg_lifecycle.c" -o "$scratch/test-nanovg"
"$scratch/test-nanovg"
