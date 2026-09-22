#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: $0 LVGL_SOURCE_DIR [C_COMPILER]" >&2
    exit 2
fi

lvgl_source=$1
compiler=${2:-cc}
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/lilygo-nanovg-glyph-test.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
glyph_path=src/draw/nanovg/lv_draw_nanovg_label.c
mkdir -p "$scratch/source/$(dirname "$glyph_path")"
cp "$lvgl_source/$glyph_path" "$scratch/source/$glyph_path"

# Apply only the glyph file's hunks, so raw and patched source trees can both
# run this test without requiring a real EGL/GLES implementation on the host.
awk -v target="--- a/$glyph_path" '
    /^--- a\// { copying = ($0 == target) }
    copying { print }
' "$test_dir/../patches/lvgl-9.5.0-nanovg-drm.patch" > "$scratch/glyph.patch"
if [ ! -s "$scratch/glyph.patch" ]; then
    echo "NanoVG glyph-cache patch is missing" >&2
    exit 1
fi
if patch --batch --silent --forward --dry-run -p1 -d "$scratch/source" < "$scratch/glyph.patch" >/dev/null 2>&1; then
    patch --batch --silent --forward -p1 -d "$scratch/source" < "$scratch/glyph.patch"
else
    patch --force --silent --reverse --dry-run -p1 -d "$scratch/source" < "$scratch/glyph.patch" >/dev/null
fi

extract_function() {
    awk -v declaration="$2" '
        $0 == declaration { copying = 1; found = 1 }
        copying { print }
        copying && /^}/ { exit }
        END { if (!found) exit 1 }
    ' "$1"
}

source="$scratch/source/$glyph_path"
extract_function "$source" 'static bool letter_create_cb(letter_item_t * item, void * user_data)' > "$scratch/letter_create.inc"
extract_function "$source" 'static void letter_free_cb(letter_item_t * item, void * user_data)' > "$scratch/letter_free.inc"
extract_function "$source" 'static lv_cache_compare_res_t letter_compare_cb(const letter_item_t * lhs, const letter_item_t * rhs)' > "$scratch/letter_compare.inc"

"$compiler" -std=c99 -Wall -Wextra -I "$scratch" "$test_dir/test_nanovg_glyph_cache.c" -o "$scratch/test-nanovg-glyph"
"$scratch/test-nanovg-glyph"
