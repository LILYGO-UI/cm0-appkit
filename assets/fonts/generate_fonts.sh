#!/bin/sh
set -eu

if [ "$#" -ne 0 ] && [ "$#" -ne 3 ]; then
    echo "usage: $0 [INTER_TTF SOURCE_HAN_SANS_OTF FONT_AWESOME_WOFF]" >&2
    exit 2
fi

font_conv=${LV_FONT_CONV:-lv_font_conv}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$#" -eq 3 ]; then
    inter_font=$1
    source_han_font=$2
    font_awesome=$3
else
    inter_font="$script_dir/Inter[opsz,wght].ttf"
    source_han_font="$script_dir/SourceHanSansSC-Normal.otf"
    font_awesome="$script_dir/FontAwesome5-Solid+Brands+Regular.woff"
fi
for font_file in "$inter_font" "$source_han_font" "$font_awesome"; do
    if [ ! -f "$font_file" ]; then
        echo "font input does not exist: $font_file" >&2
        exit 1
    fi
done
output_dir="$script_dir/../../src/fonts"
cjk_glyphs=$(tr -d '\r\n' < "$script_dir/glyphs.txt")
lvgl_symbols="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62189,62212,62810,63426,63650"

mkdir -p "$output_dir"
for size in 14 22 28 36 48; do
    "$font_conv" \
        --no-compress --no-prefilter --bpp 4 --size "$size" \
        --font "$inter_font" -r 0x20-0x7f,0xa0-0xff,0x2022 \
        --font "$source_han_font" --symbols "$cjk_glyphs" \
        --font "$font_awesome" -r "$lvgl_symbols" \
        --format lvgl --lv-include lvgl.h \
        --lv-font-name "cm0_font_ui_$size" \
        --force-fast-kern-format \
        -o "$output_dir/cm0_font_ui_$size.c"
done
