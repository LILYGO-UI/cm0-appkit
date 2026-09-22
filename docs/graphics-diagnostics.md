# Target graphics diagnostics

Run the read-only inventory on the Linux board from an AppKit checkout, using
the same user and session as the application:

```sh
sh tools/graphics-diagnostics.sh
sh tools/graphics-diagnostics.sh --probe-egl --probe-drm
```

The inventory reports the kernel and board model where available, DRM nodes
and their kernel drivers, current-user device permissions, and EGL, GLESv2,
GBM, and DRM library metadata. Missing `pkg-config` metadata does not imply
missing runtime libraries. Libraries and device nodes alone do not establish
that a working hardware renderer exists or that the application uses it.

The optional EGL probe runs `eglinfo -B -p gbm` for the DRM/GBM platform. Inspect the renderer corresponding
to the application's display platform (GBM for direct DRM presentation).
`llvmpipe` and `softpipe` are software renderers. A hardware renderer on one
platform does not establish that another platform works. Some older or
vendor-specific `eglinfo` versions do not support `-B`; the tool reports the
failure without interpreting it as a GPU failure. Initializing a diagnostic
EGL context can load driver code; it does not change application settings.

The optional DRM probe resolves each card's module through its sysfs driver
link and runs `modetest -M NAME -c -p` once per module. It first checks the
driver's `module` link; built-in `vc4-drm` is mapped to libdrm's `vc4` name.
Use `--drm-driver NAME` when automatic resolution is unavailable or the
platform driver and libdrm use different names:

```sh
sh tools/graphics-diagnostics.sh --drm-driver vc4
sh tools/graphics-diagnostics.sh --drm-driver vc4 --full-drm > graphics-full.txt
```

`--drm-driver` and `--full-drm` both enable the DRM probe. Some libdrm
versions interpret `modetest -D` as a bus ID instead of a device path;
selecting the module avoids that ambiguity. If multiple cards use one module,
`modetest -M` selects a matching card, so inspect the connector identities in
its output rather than assuming it reports every card with that module.

By default the probe prints connector, CRTC and plane tables, display modes,
plane formats, and rotation flags and values. Large property blobs and format
modifier tables are omitted; `--full-drm` retains the complete output. The
summary recognizes libdrm's text output format, so use full output if a vendor
version differs. stderr and recognizable error messages remain visible.

The probe only queries connectors and planes; it does not set modes or properties,
acquire a display for rendering, or stop a running compositor. Permission
restrictions and display ownership may limit the result. Inspect the active
connector, CRTC and plane mapping, supported pixel formats, and any `rotation`
property with `rotate-90` or `rotate-270` values. Reported rotation support
still needs validation with the application's actual buffer format, modifier,
dimensions and plane assignment. A later implementation should use a DRM
atomic test-only commit before selecting a rotation configuration.

Both optional probes require `timeout`; otherwise they are skipped. Each
probe has a 10-second timeout and a further 2-second forced-stop allowance.
No commands install packages, use the network, change permissions, write
display settings, or restart services. The script never invokes `sudo`.

## Compare CPU and memory

Choose the current application PID from the process list and pass it
explicitly. For example, replace `1234` with the launcher PID:

```sh
sh tools/graphics-diagnostics.sh --pid 1234 --seconds 30 --interval 1
```

The script samples Linux `/proc/PID/stat` and `/proc/uptime`. CPU is the
change in process user plus system CPU time divided by actual elapsed time,
with one logical CPU equal to 100%. A multithreaded process can exceed 100%.
RSS is resident memory in MiB from the kernel page count and `getconf
PAGESIZE`. Values include the process's threads but exclude child processes,
the compositor, GPU allocation outside process RSS, and video decoder work.
The requested sampling duration is limited to 1 through 300 seconds;
intervals are limited to 1 through 60 seconds. The final interval is shortened
when needed. Inventory and probe time are additional to the measurement.
If the process exits, becomes unreadable, or its PID is reused, measurement
stops with a nonzero exit status.

Record a baseline before changing the rendering backend, then repeat with
the same application build, resolution, orientation, refresh rate, scene,
brightness, power profile and interaction sequence. Warm up the scene first
so initial font loading and texture creation do not dominate the sample.
Useful separate scenarios are idle, continuous list scrolling, page
transitions and horizontal display rotation. Keep other workloads constant;
run the probes before measuring, because probing itself consumes resources.

This script does not measure frame latency, dropped frames, GPU utilization
or total system memory. Pair CPU measurements with application frame timing
or compositor presentation feedback and visible correctness checks. A lower
CPU percentage alone does not prove smoother presentation, and GLES2 support
does not guarantee a particular frame rate. The video decoder's capabilities
are separate from LVGL rendering; validate a video application's hardware
decode and buffer-sharing path independently.
