# CM0 UI Fonts

AppKit loads the bundled font files at runtime through LVGL's FreeType backend:

- Latin, numbers, and punctuation: Inter Regular variable font
- Simplified Chinese: Source Han Sans SC Normal
- LVGL symbols: Font Awesome 5

Fonts returned by `lilygo_ui_font_get()` use Inter as the primary face, Source
Han Sans SC as the Chinese fallback, and Font Awesome as the symbol fallback.
Installed systems read them from `/usr/share/lilygo-ui/fonts`; source builds
fall back to this directory. `LILYGO_UI_FONT_DIR` can override that location
for tests.

Generation inputs used for this revision:

```text
Inter[opsz,wght].ttf SHA-256:
29160a80ff49ddcab2c97711247e08b1fab27a484a329ce8b813d820dc559031

SourceHanSansSC-Normal.otf SHA-256:
1ee89e1669362dee13851129c0a8a791a87521eb4148e5efbf5d26596738e25b

FontAwesome5-Solid+Brands+Regular.woff SHA-256:
f4e42f6cd69e5dbdcccc0f2f5be136cebde0e427641e45403bf9173a92da95f4
```

The runtime assets occupy 17,644,748 bytes. Most of that is the 16,414,944-byte
Source Han Sans SC face. This is an intentional platform storage tradeoff: one
shared `lilygo-ui-appkit-dev` package replaces per-application bitmap subsets and
supports UI text that is not known at build time.

Keep `licenses/NOTICE.md` and `licenses/OFL-1.1.txt` with every distribution of
the runtime fonts.
