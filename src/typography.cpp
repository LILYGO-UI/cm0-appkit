#include <cm0/typography.h>

#include <array>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef LILYGO_UI_FONT_INSTALL_DIR
#define LILYGO_UI_FONT_INSTALL_DIR "/usr/share/lilygo-ui/fonts"
#endif

#ifndef LILYGO_UI_FONT_SOURCE_DIR
#define LILYGO_UI_FONT_SOURCE_DIR ""
#endif

namespace {

constexpr const char *kInterFile = "Inter[opsz,wght].ttf";
constexpr const char *kSourceHanFile = "SourceHanSansSC-Normal.otf";
constexpr const char *kSymbolsFile = "FontAwesome5-Solid+Brands+Regular.woff";

struct FontSet {
  uint32_t size;
  lv_font_t *inter = nullptr;
  lv_font_t *source_han = nullptr;
  lv_font_t *symbols = nullptr;
};

std::array<FontSet, 5> fonts{{{14}, {22}, {28}, {36}, {48}}};

bool readable_font(const char *directory, const char *filename) {
  if (!directory || !directory[0])
    return false;
  char path[1024];
  const int length = snprintf(path, sizeof(path), "%s/%s", directory, filename);
  return length > 0 && static_cast<size_t>(length) < sizeof(path) &&
         access(path, R_OK) == 0;
}

const char *font_directory() {
  const char *override_directory = getenv("LILYGO_UI_FONT_DIR");
  const char *candidates[] = {override_directory, LILYGO_UI_FONT_INSTALL_DIR,
                              LILYGO_UI_FONT_SOURCE_DIR};
  for (const char *candidate : candidates) {
    if (readable_font(candidate, kInterFile) &&
        readable_font(candidate, kSourceHanFile) &&
        readable_font(candidate, kSymbolsFile))
      return candidate;
  }
  return nullptr;
}

lv_font_t *create_font(const char *directory, const char *filename,
                       uint32_t size) {
  char path[1024];
  const int length = snprintf(path, sizeof(path), "%s/%s", directory, filename);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(path))
    return nullptr;
  return lv_freetype_font_create(path, LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                 size, LV_FREETYPE_FONT_STYLE_NORMAL);
}

void delete_font(lv_font_t *&font) {
  if (!font)
    return;
  font->fallback = nullptr;
  lv_freetype_font_delete(font);
  font = nullptr;
}

bool initialize(FontSet &set) {
  if (set.inter)
    return true;
  if (!lv_is_initialized()) {
    fprintf(stderr, "[appkit] fonts requested before lv_init()\n");
    return false;
  }

  const char *directory = font_directory();
  if (!directory) {
    fprintf(stderr,
            "[appkit] cannot find runtime fonts; install lilygo-ui-appkit-dev or "
            "set LILYGO_UI_FONT_DIR\n");
    return false;
  }

  set.symbols = create_font(directory, kSymbolsFile, set.size);
  set.source_han = create_font(directory, kSourceHanFile, set.size);
  set.inter = create_font(directory, kInterFile, set.size);
  if (!set.inter || !set.source_han || !set.symbols) {
    fprintf(stderr, "[appkit] cannot load FreeType fonts at %u px from %s\n",
            set.size, directory);
    delete_font(set.inter);
    delete_font(set.source_han);
    delete_font(set.symbols);
    return false;
  }

  set.inter->fallback = set.source_han;
  set.source_han->fallback = set.symbols;
  return true;
}

} // namespace

extern "C" const lv_font_t *lilygo_ui_font_get(uint32_t size) {
  for (FontSet &set : fonts) {
    if (set.size == size)
      return initialize(set) ? set.inter : nullptr;
  }
  fprintf(stderr, "[appkit] unsupported UI font size: %u\n", size);
  return nullptr;
}

extern "C" void lilygo_ui_fonts_deinit(void) {
  if (!lv_is_initialized())
    return;
  for (FontSet &set : fonts) {
    delete_font(set.inter);
    delete_font(set.source_han);
    delete_font(set.symbols);
  }
}
