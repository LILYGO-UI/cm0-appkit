#include <cm0/typography.h>

#include <assert.h>

static void verify_fonts() {
  const lv_font_t *font = lilygo_ui_font_get(22);
  assert(font);
  assert(font == lv_font_get_default() ||
         lv_font_get_default() == lilygo_ui_font_get(14));

  lv_font_glyph_dsc_t glyph{};
  assert(lv_font_get_glyph_dsc(font, &glyph, 'A', 0));
  assert(glyph.resolved_font == font);

  assert(lv_font_get_glyph_dsc(font, &glyph, 0x8bbe, 0));
  assert(glyph.resolved_font == font->fallback);

  assert(lv_font_get_glyph_dsc(font, &glyph, 0xf00c, 0));
  assert(glyph.resolved_font == font->fallback->fallback);
}

static void verify_dynamic_font_size() {
  const lv_font_t *font = lilygo_ui_font_get(18);
  assert(font);
  assert(font == lilygo_ui_font_get(18));

  lv_font_glyph_dsc_t glyph{};
  assert(lv_font_get_glyph_dsc(font, &glyph, 'A', 0));
  assert(glyph.resolved_font == font);

  assert(lv_font_get_glyph_dsc(font, &glyph, 0x8bbe, 0));
  assert(glyph.resolved_font == font->fallback);

  assert(lv_font_get_glyph_dsc(font, &glyph, 0xf00c, 0));
  assert(glyph.resolved_font == font->fallback->fallback);
}

int main() {
  lv_init();
  verify_fonts();
  verify_dynamic_font_size();
  lilygo_ui_fonts_deinit();
  lv_deinit();

  lv_init();
  verify_fonts();
  verify_dynamic_font_size();
  lilygo_ui_fonts_deinit();
  lv_deinit();
  return 0;
}
