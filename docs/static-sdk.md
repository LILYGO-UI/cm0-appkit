# Static source SDK and shared fonts — 0.1.0

AppKit/LVGL implementation code is compiled into each application again. The
consumer's `find_package(LilyGoUI CONFIG REQUIRED)` loads the pinned source
submodule, or an explicitly selected source SDK. It does not import AppKit
binaries from a sysroot. AppKit remains an independent top-level project.

## Build inputs

Native presets require build tools, SDL2, FreeType, and the AppKit submodule.
CM0 presets require the ordinary BSP compiler/sysroot, plus the source SDK.
The base BSP supports the software renderer. OpenGLES additionally needs
EGL/GBM/GLES development packages and a source SDK with the GPU integration;
the original pinned SDK revision in some applications predates that integration.
Select an appropriate source SDK with `-DLilyGoUI_DIR=/path/to/appkit-source`.
No application builds another application or includes Launcher-private code.

The old user-installed binary SDK and its CMake registry entry are unnecessary.
After the source presets are restored, reset a stale build cache once. Existing
source checkouts, patches, and local changes must be preserved when restoring
the Git submodule references. The normal application workflow is `lpm start`,
`lpm test`, or `lpm pack` after submodule initialization.

## Fonts and package ownership

The latest project convention keeps Inter, Source Han Sans CN, and Font Awesome
in the architecture-independent `lilygo-ui-appkit-dev` package together with the
source SDK, public headers, bundled LVGL, and patches. Version 0.1.0 emits this
single package. It installs no AppKit or LVGL shared library. Font licenses remain
with the SDK and common fonts, and applications retain license notices where
required for embedded glyph assets.

Statically linked applications depend on `lilygo-ui-appkit-dev` for shared fonts
and on ordinary system libraries. They do not declare the withdrawn `lilygo-ui-appkit-abi-*`
capabilities. AppKit implementation updates require rebuilding those applications;
font asset updates continue to be shared.

## Transition from the deployed dynamic experiment

The AppKit package version and application minimum AppKit versions have been
reset to 0.1.0. Debian considers this lower than the earlier internal 1.2.0 and
1.3.x packages, so existing test devices require an explicit downgrade rather
than a normal upgrade. Repackage all applications that still require AppKit
1.3.x with the new minimum dependency before installing them together with
the reset SDK. Old package artifacts retain their original versions.

The dev package declares Provides, Conflicts, and Replaces for the former
`lilygo-ui-appkit-runtime` package, which is no longer emitted. Rebuild all
applications that require the dynamic ABI and install their static packages
together with the new dev package in a single APT transaction. Inspect installed
consumers and simulate the transaction first. The retired runtime package is
replaced; verify that the planned transaction keeps all applications installed.
The dev package intentionally does not provide
binary ABI capabilities, so it cannot satisfy an unconverted dynamic consumer's
ABI dependency. Keep configuration and backup packages during the transition.

Verify native and ARM64 executables have no AppKit/LVGL dynamic dependency,
while still linking normal platform libraries dynamically. Verify cross builds
with AppKit headers, CMake imports, and shared objects absent from the sysroot.
The package regression checker verifies source SDK content, font ownership,
licenses and static application dependencies:

```sh
python3 tests/test_debian_packages.py \
  --dev dist/lilygo-ui-appkit-dev_0.1.0_all.deb \
  --app /path/to/static-application.deb \
  --app /path/to/another-static-application.deb
```

Repeat `--app` for each application package being validated.
