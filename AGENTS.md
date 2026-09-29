# LILYGO UI AppKit Conventions

This repository is the independent `LilyGoUI` SDK. It must configure,
build, test, install, and package as a top-level CMake project. Consumers use
it only through `find_package(LilyGoUI CONFIG REQUIRED)`; AppKit must
not depend on Launcher or any application repository.

Public C APIs live under `include/cm0/`. Public CMake targets use the
`LilyGoUI::` namespace. The `cm0` C API identifies the currently supported
product profile and may remain until a profile-neutral API is introduced.
Implementation C and C++ sources live under
`src/`. Store original images, audio, fonts, and similar media under
`assets/`; generated LVGL C sources remain under `src/`. Keep tests, LVGL
patches, and packaging metadata in this repository.

Use Inter for Latin text, numbers, and punctuation and Source Han Sans CN for
Chinese. LVGL UI code calls `lilygo_ui_font_get()` with 14, 22, 28, 36, or 48.
An independent application may put app-specific glyphs in a private
supplemental font and set the same-size AppKit font through LVGL's native
`lv_font_t.fallback`.

The embedded Inter, Source Han Sans, and Font Awesome subsets are distributed
under SIL Open Font License 1.1. Keep `assets/fonts/licenses/NOTICE.md` and
`assets/fonts/licenses/OFL-1.1.txt` in every SDK or binary distribution
embedding them.

AppKit and LVGL are compiled statically from the source SDK. Applications may
pin the SDK as a source submodule and consume it only through find_package;
no AppKit binary SDK is required in the target sysroot. Keep application code
in its independent repository and maintain the public LilyGoUI:: target names.

The Debian package is `lilygo-ui-appkit-dev`: one architecture-independent
package containing the source SDK, bundled LVGL, patches, headers, common font
faces, and licenses. Do not install AppKit/LVGL shared objects or advertise their
former ABI capabilities. Applications link the code statically and depend on
this package for shared fonts. The former `lilygo-ui-appkit-runtime` name is
retained only in explicit package migration declarations and historical records;
the dev package declares the appropriate Provides, Conflicts, and Replaces.
The former `lilygo-cm0-appkit-dev` package and `LilyGoCM0AppKit` CMake entry point
remain migration-only compatibility interfaces.
