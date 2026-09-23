#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: $0 LVGL_SOURCE_DIR [C_COMPILER]" >&2
    exit 2
fi

lvgl_source=$1
compiler=${2:-cc}
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/lilygo-drm-buffer.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM

driver=src/drivers/display/drm/lv_linux_drm.c
mkdir -p "$scratch/source/$(dirname "$driver")"
cp "$lvgl_source/$driver" "$scratch/source/$driver"
for patch_name in drm-recovery drm-software-rotation; do
    patch_file="$test_dir/../patches/lvgl-9.5.0-$patch_name.patch"
    if patch --batch --silent --forward --dry-run -p1 -d "$scratch/source" < "$patch_file" >/dev/null 2>&1; then
        patch --batch --silent --forward -p1 -d "$scratch/source" < "$patch_file"
    else
        patch --batch --silent --reverse --dry-run -p1 -d "$scratch/source" < "$patch_file" >/dev/null
    fi
done

# Compile the actual driver ownership paths; only DRM and the LVGL interface
# are mocked. Render storage uses real anonymous mappings on the host.
extract_function() {
    awk -v declaration="$2" '
        $0 == declaration { copying = 1; found = 1 }
        copying { print }
        copying && /^}/ { exit }
        END { if (!found) exit 1 }
    ' "$1"
}
source_file="$scratch/source/$driver"
extract_function "$source_file" 'lv_result_t lv_linux_drm_set_file(lv_display_t * disp, const char * file, int64_t connector_id)' > "$scratch/set_file.inc"
extract_function "$source_file" 'static void drm_rotation_event_cb(lv_event_t * event)' > "$scratch/rotation.inc"
extract_function "$source_file" 'static void drm_del_event_cb(lv_event_t * e)' > "$scratch/delete.inc"

"$compiler" -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I "$scratch" \
    "$test_dir/test_drm_software_buffer.c" -o "$scratch/test-buffer"
"$scratch/test-buffer"
