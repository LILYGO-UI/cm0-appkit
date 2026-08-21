#ifndef LILYGO_CM0_APPKIT_LV_SDL_HIGH_DPI_H
#define LILYGO_CM0_APPKIT_LV_SDL_HIGH_DPI_H

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

static inline SDL_Window *cm0_appkit_sdl_create_high_dpi_window(
    const char *title, int x, int y, int width, int height, Uint32 flags)
{
    return SDL_CreateWindow(title, x, y, width, height,
                            flags | SDL_WINDOW_ALLOW_HIGHDPI);
}

#define SDL_CreateWindow cm0_appkit_sdl_create_high_dpi_window

#endif
