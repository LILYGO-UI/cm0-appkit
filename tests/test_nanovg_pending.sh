#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: $0 LVGL_SOURCE_DIR [C_COMPILER]" >&2
    exit 2
fi

lvgl_source=$1
compiler=${2:-cc}
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/lilygo-nanovg-pending-test.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM

for file in lv_draw_nanovg_label.c lv_nanovg_image_cache.c lv_nanovg_utils.c lv_nanovg_utils.h; do
    source_path="src/draw/nanovg/$file"
    mkdir -p "$scratch/source/$(dirname "$source_path")"
    cp "$lvgl_source/$source_path" "$scratch/source/$source_path"
    awk -v target="--- a/$source_path" '
        /^--- a\// { copying = ($0 == target) }
        copying { print }
    ' "$test_dir/../patches/lvgl-9.5.0-nanovg-drm.patch" > "$scratch/$file.patch"
    if [ ! -s "$scratch/$file.patch" ]; then
        echo "NanoVG patch for $file is missing" >&2
        exit 1
    fi
    if patch --batch --silent --forward --dry-run -p1 -d "$scratch/source" < "$scratch/$file.patch" >/dev/null 2>&1; then
        patch --batch --silent --forward -p1 -d "$scratch/source" < "$scratch/$file.patch"
    else
        patch --force --silent --reverse --dry-run -p1 -d "$scratch/source" < "$scratch/$file.patch" >/dev/null
    fi
done

extract_function() {
    awk -v declaration="$2" '
        $0 == declaration { copying = 1; found = 1 }
        copying { print }
        copying && /^}/ { exit }
        END { if (!found) exit 1 }
    ' "$1"
}

source_dir="$scratch/source/src/draw/nanovg"
extract_function "$source_dir/lv_nanovg_utils.c" 'void lv_nanovg_end_frame(struct _lv_draw_nanovg_unit_t * u)' > "$scratch/end_frame.inc"
extract_function "$source_dir/lv_nanovg_utils.c" 'void lv_nanovg_clean_up(struct _lv_draw_nanovg_unit_t * u)' > "$scratch/clean_up.inc"
extract_function "$source_dir/lv_nanovg_utils.c" 'void lv_nanovg_flush_pending(struct _lv_draw_nanovg_unit_t * u)' > "$scratch/flush_pending.inc"
extract_function "$source_dir/lv_draw_nanovg_label.c" 'static inline int letter_get_image_handle(lv_draw_nanovg_unit_t * u, lv_font_glyph_dsc_t * g_dsc)' > "$scratch/letter_get.inc"
extract_function "$source_dir/lv_nanovg_image_cache.c" 'int lv_nanovg_image_cache_get_handle(struct _lv_draw_nanovg_unit_t * u,' > "$scratch/image_get.inc"

"$compiler" -std=c99 -Wall -Wextra -I "$scratch" "$test_dir/test_nanovg_pending.c" -o "$scratch/test-nanovg-pending"
"$scratch/test-nanovg-pending"
