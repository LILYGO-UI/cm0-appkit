# CM0 UI Fonts

The checked-in `src/fonts/cm0_font_ui_*.c` files are composite LVGL fonts:

- Latin, numbers, and punctuation: Inter Regular variable font
- Simplified Chinese: Source Han Sans SC Normal
- LVGL symbols: Font Awesome 5

The generated public font name is `CM0 UI`, not an upstream reserved font
name. Chinese glyphs are deliberately limited to `glyphs.txt` to control the
size of statically linked applications. The original font files used to
generate the subsets are stored in this directory.

Generation inputs used for this revision:

```text
Inter[opsz,wght].ttf SHA-256:
29160a80ff49ddcab2c97711247e08b1fab27a484a329ce8b813d820dc559031

SourceHanSansSC-Normal.otf SHA-256:
1ee89e1669362dee13851129c0a8a791a87521eb4148e5efbf5d26596738e25b

FontAwesome5-Solid+Brands+Regular.woff SHA-256:
f4e42f6cd69e5dbdcccc0f2f5be136cebde0e427641e45403bf9173a92da95f4
```

Run `./generate_fonts.sh` to regenerate `src/fonts/cm0_font_ui_*.c` from the
bundled assets. Set `LV_FONT_CONV` to an installed `lv_font_conv` executable
when it is not on `PATH`. Three explicit font paths may be passed to override
the bundled inputs.

Keep `licenses/NOTICE.md` and `licenses/OFL-1.1.txt` in distributions that
embed these generated fonts.
