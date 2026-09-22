# Private Mesa VC4 runtime

The `lilygo-ui-mesa-vc4` runtime is maintained in the independent
[LILYGO-UI/mesa-vc4](https://github.com/LILYGO-UI/mesa-vc4) repository.
See its [README](https://github.com/LILYGO-UI/mesa-vc4#readme) for the pinned Mesa source,
minimal build profile, build and packaging commands, target installation and
image integration.

AppKit supplies the LVGL/EGL integration and graphics diagnostics. It no longer
ships the Mesa build/package tools or service configuration example. The runtime
repository builds its DEB without an AppKit or Launcher checkout; product image
configuration selects the private runtime for the services that use it.
