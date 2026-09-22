# LILYGO UI AppKit

LILYGO UI AppKit is the standalone, LVGL-based source SDK for LILYGO Linux
products. Each application compiles and statically links its AppKit and LVGL
code. CM0 is the first product profile.

Consumers remain independent top-level CMake projects and use only:

```cmake
find_package(LilyGoUI CONFIG REQUIRED)
```

The SDK exposes `LilyGoUI::LVGL`, `LilyGoUI::UIFonts`,
`LilyGoUI::SystemStatus`, `LilyGoUI::StatusBar`, and `LilyGoUI::Runtime`.
Shared status-bar and home-indicator source code is implemented here once and
compiled into consumers through these targets. The former `LilyGoCM0AppKit`
package/targets remain migration-only aliases.

## Application builds

Applications pin AppKit as `third_party/cm0-appkit` and point `LilyGoUI_DIR`
at that source checkout in their CMake presets. Initialize submodules once:

```sh
git submodule update --init --recursive
lpm start
```

No prebuilt AppKit SDK or AppKit library is required in the host installation
or target sysroot. The host still needs SDL2/FreeType and build tools; a target
sysroot supplies the compiler and ordinary system dependencies such as DRM and
FreeType. The SDK bundles its pinned LVGL sources. A newer source SDK can be
selected explicitly with `-DLilyGoUI_DIR=/path/to/appkit-source`.

If a build directory previously selected the binary SDK, reset its generated
CMake cache once (`cmake --fresh --preset host-simulator` with CMake 3.24+).
The source preset then selects the submodule again. SDK implementation updates
require rebuilding and deploying the affected applications.

## AppKit development

Install CMake, pkg-config, SDL2, and FreeType, then run in this repository:

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
```

The standalone SDK can also install its source CMake package for consumers that
prefer a source SDK prefix:

```sh
cmake --install build/host-simulator --prefix /path/to/source-sdk
```

Consumers select that prefix with `CMAKE_PREFIX_PATH`. AppKit is still compiled
statically inside each independent consumer; installing a binary SDK is not part
of this workflow. Normal application submodule builds need no SDK installation.

## Package

Version 0.1.0 uses one architecture-independent `lilygo-ui-appkit-dev` package
for the source CMake package, public headers, implementation sources, bundled
LVGL, patches, common font faces, and licenses. It contains no AppKit/LVGL shared
object and provides no library ABI capability. Applications depend on it for
the common font files.

Applications declare their direct system-library dependencies again and do not
bundle duplicate font faces. Build and package AppKit separately:

```sh
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -G DEB
```

The preset downloads and verifies the pinned
[`0.1.0` BSP](https://github.com/LILYGO-UI/CM0BspBuilder/releases/download/0.1.0/cm0_sdk.tar.gz)
on first configuration, then reuses `.cache/cm0-bsp/0.1.0`. AppKit source builds
accept the BSP's static FreeType archive as before. GPU builds additionally need
EGL/GBM/GLES development libraries and a source SDK with the GPU patches.

When replacing the short-lived 1.3.0 dynamic deployment, rebuild every dynamic
consumer statically and install those applications together with the 0.1.0 dev
package. Version numbering has been reset to 0.1.0; existing internal 1.3.x
installations require an explicit downgrade, and applications must be repackaged
with the new minimum AppKit dependency. Its Provides, Conflicts, and Replaces
relationships migrate the former
`lilygo-ui-appkit-runtime` font package. The dev package provides no dynamic ABI
capabilities, so existing dynamic consumers must also be upgraded to satisfy
their dependencies. Review the APT plan to ensure it preserves those applications.
See [static SDK restoration](docs/static-sdk.md) for verification and migration.

## Device graphics

`--orientation landscape` rotates the displayed content 90 degrees clockwise
on the device, with touch coordinates following the same orientation. The SDL
simulator instead swaps the window dimensions for an upright landscape preview.

`LILYGO_UI_RENDERER=software` remains the default. Linux device builds can
select `-DLILYGO_UI_RENDERER=opengles` to use LVGL's cached GLES2 draw unit
and EGL/GBM scanout through DRM. This moves texture composition and display
rotation to the GPU; software drawing still generates uncached content and
serves fbdev displays. Renderer selection is a build option, not a runtime
fallback from failed EGL initialization.

DRM/EGL builds expose `lv_linux_drm_suspend()` and `lv_linux_drm_resume()` for
foreground-process handoff without destroying the host's UI or GPU caches.
The caller must pause drawing before suspend, close its input device, and keep
drawing paused until resume succeeds. Suspend finishes the pending page flip,
keeps the last scanout buffer locked and drops DRM master; resume reacquires
master and presents that retained frame before returning. The original CRTC
state is preserved for final shutdown. Both calls return `LV_RESULT_INVALID`
for an unsupported state or failed operation, allowing the caller to recreate
the display. The caller must reopen input and rebind its gesture callbacks.
Keeping EGL and GBM resources alive also keeps their memory allocated while
the foreground application runs. Software DRM continues to use full display
recreation; this API does not cache application processes.

`-DLILYGO_UI_RENDERER=nanovg` selects the experimental NanoVG GLES2 draw
unit instead of the texture-caching draw unit. It draws shapes and composites
glyphs/images on the GPU, uses full-frame rendering, and requires a window EGL
configuration with 8-bit stencil and 4x MSAA. A missing configuration fails
initialization; it does not fall back to software. The LVGL 9.5.0 integration
patch handles context recreation, native display rotation and FreeType glyph
cache ownership. Choose a separate build directory for each renderer and
compare real application scenes before selecting NanoVG for a release.

Linux builds also preserve each evdev pointer `SYN_REPORT` sample. This keeps
a queued press/release pair from collapsing into a release when drawing or
display recreation delays input polling.

NanoVG image and letter caches retain the upstream limits of 128 and 512
entries. These are entry counts, not memory budgets. Child off-screen layers
use single-sample FBOs, and gradients are limited to two colors. Verify nested
opacity, clipping and text, as well as CPU time and peak memory, on the target.
The CM0 VC4 comparison found wider shadow bands and stray arc fragments in
NanoVG's dynamic scene. Keep it experimental until these fidelity differences
are resolved; the production device builds currently select `opengles`.

The GPU build requires `libgbm-dev`, `libegl-dev`, and `libgles-dev` in the
target or cross sysroot. BSP 0.1.0 does not contain them. Use an SDK that
includes those packages and its installed toolchain directly:

```sh
cmake -S . -B build/cm0-gpu \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/sdk/usr/share/cm0-bsp/toolchain.cmake \
  -DCM0_SIMULATOR=OFF -DCMAKE_BUILD_TYPE=Release \
  -DLILYGO_UI_RENDERER=opengles -DLILYGO_UI_BUILD_GRAPHICS_BENCHMARK=ON
cmake --build build/cm0-gpu --parallel
```

Applications select renderer and simulator options when compiling their source
SDK through `find_package(LilyGoUI CONFIG REQUIRED)`. GPU application packages
include the `libgbm1`, `libegl1`, and `libgles2` dependencies. The device also needs a working vendor EGL/GLES
implementation (Mesa VC4 on CM0); library presence alone does not establish
hardware acceleration. SDL simulator builds remain software-only.

The optional private `lilygo-ui-mesa-vc4` runtime is built and packaged in the
independent [LILYGO-UI/mesa-vc4](https://github.com/LILYGO-UI/mesa-vc4) repository.
AppKit owns the LVGL/EGL integration and diagnostics;
Mesa sources, build tools, Debian packaging and service configuration examples
belong to that runtime repository. See the [runtime repository guide](docs/mesa-vc4.md).

Use [graphics diagnostics](docs/graphics-diagnostics.md) to inspect the
actual renderer and display rotation properties, and the
[graphics benchmark](docs/graphics-benchmark.md) to compare identical
software/GPU workloads and exercise display recreation. Video codecs are
independent of this rendering path.

## Fonts

Use `lilygo_ui_font_get()` with 14, 22, 28, 36, or 48 for the public LVGL
fonts. See
`assets/fonts/README.md` for the FreeType fallback chain, runtime paths, and
documented storage budget. Device applications depend on the shared
`lilygo-ui-appkit-dev` package. Font distributions must retain
`assets/fonts/licenses/NOTICE.md` and `assets/fonts/licenses/OFL-1.1.txt`.

Original images, audio, fonts, and similar media belong under `assets/`.
Generated LVGL C sources remain under `src/`.
