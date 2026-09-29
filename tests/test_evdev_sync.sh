#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: $0 LVGL_SOURCE_DIR [C_COMPILER]" >&2
    exit 2
fi

lvgl_source=$1
compiler=${2:-cc}
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/lilygo-evdev-sync-test.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
source_path=src/drivers/evdev/lv_evdev.c
mkdir -p "$scratch/source/$(dirname "$source_path")"
cp "$lvgl_source/$source_path" "$scratch/source/$source_path"
for patch_name in evdev-sync keyboard; do
    patch_file="$test_dir/../patches/lvgl-9.5.0-$patch_name.patch"
    if patch --batch --silent --forward --dry-run -p1 -d "$scratch/source" < "$patch_file" >/dev/null 2>&1; then
        patch --batch --silent --forward -p1 -d "$scratch/source" < "$patch_file"
    else
        patch --force --silent --reverse --dry-run -p1 -d "$scratch/source" < "$patch_file" >/dev/null
    fi
done

# Compile the actual event reader with a deterministic nonblocking event queue.
awk '
    $0 == "static void _evdev_read(lv_indev_t * indev, lv_indev_data_t * data)" { copying = 1; found = 1 }
    copying { print }
    copying && /^}/ { exit }
    END { if (!found) exit 1 }
' "$scratch/source/$source_path" > "$scratch/evdev_read.inc"
for gestures in 0 1; do
    "$compiler" -std=c99 -Wall -Wextra -DLV_USE_GESTURE_RECOGNITION="$gestures" \
        -I "$scratch" "$test_dir/test_evdev_sync.c" -o "$scratch/test-evdev-sync"
    "$scratch/test-evdev-sync"
done
