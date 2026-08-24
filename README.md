# LILYGO UI AppKit

LILYGO UI AppKit is the standalone, LVGL-based CMake SDK for LILYGO Linux
products. It owns the UI runtime, shared system chrome, UI fonts, device status
helpers, and application packaging helpers. CM0 is its first product profile.

Consumers use only the CMake package:

```cmake
find_package(LilyGoUI CONFIG REQUIRED)
```

The SDK exports these targets:

- `LilyGoUI::LVGL`
- `LilyGoUI::UIFonts`
- `LilyGoUI::SystemStatus`
- `LilyGoUI::StatusBar`
- `LilyGoUI::Runtime`

`LilyGoUI::StatusBar` is the single implementation of the shared status bar
and home indicator. Applications reserve system-chrome layout space but do
not copy or compile separate preview implementations.

The former `LilyGoCM0AppKit` CMake package and targets remain available as a
compatibility layer during migration.

## Host build

Install CMake, pkg-config, and SDL2, then run:

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
```

## Install the SDK

```sh
cmake --install build/host-simulator --prefix /path/to/lilygo-ui-sdk
```

Applications can then configure with:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/lilygo-ui-sdk
```

## Package

The Debian development package is `lilygo-ui-appkit-dev`:

```sh
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -G DEB
```

The cross preset downloads the pinned
[`0.1.0` CM0 BSP](https://github.com/LILYGO-UI/CM0BspBuilder/releases/download/0.1.0/cm0_sdk.tar.gz)
on its first configuration, verifies its SHA-256 checksum, and extracts it
under `.cache/cm0-bsp/0.1.0`. Later builds reuse that cache; no local
CM0BspBuilder checkout is required.

The package installs AppKit's source CMake package. Applications remain
independent top-level projects and statically compile the SDK through its
exported targets.

The package provides, conflicts with, and replaces the former
`lilygo-cm0-appkit-dev` package.

## Fonts

Use `lilygo_ui_font_get()` with 14, 22, 28, 36, or 48 for the public LVGL
fonts. See
`assets/fonts/README.md` for the FreeType fallback chain, runtime paths, and
documented storage budget. Device applications depend on the shared
`lilygo-ui-appkit-dev` package. Font distributions must retain
`assets/fonts/licenses/NOTICE.md` and `assets/fonts/licenses/OFL-1.1.txt`.

Original images, audio, fonts, and similar media belong under `assets/`.
Generated LVGL C sources remain under `src/`.
