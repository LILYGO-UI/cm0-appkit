# Graphics Benchmark

`lilygo-ui-graphics-benchmark` exercises the actual LVGL display backend with
identical scenes and AppKit fonts in software, OpenGL ES texture-cache, and
NanoVG builds. Enable it when configuring the standalone AppKit project:

```sh
cmake -S . -B build -DLILYGO_UI_BUILD_GRAPHICS_BENCHMARK=ON
cmake --build build --target lilygo-ui-graphics-benchmark
```

Use the target toolchain and backend configuration described in the AppKit
graphics documentation when building for the board. The executable must have
access to the DRM device and exclusive DRM master ownership. Stop the active
launcher or compositor before a DRM run, and restart it after testing.

```sh
./lilygo-ui-graphics-benchmark --drm /dev/dri/card0 --seconds 15 \
  --orientation portrait --cycles 3
./lilygo-ui-graphics-benchmark --drm /dev/dri/card0 --seconds 15 \
  --orientation landscape --cycles 3
./lilygo-ui-graphics-benchmark --drm /dev/dri/card0 --seconds 15 \
  --orientation landscape --scene dynamic --cycles 3
```

Build each renderer separately with `-DLILYGO_UI_RENDERER=software`,
`-DLILYGO_UI_RENDERER=opengles`, or `-DLILYGO_UI_RENDERER=nanovg`. Compare the
same scene across all three builds and both orientations:

| Scene | Workload |
| --- | --- |
| `list` (default) | The existing `lvgl-app-list-v1` scrolling application list, static labels and icons, and moving translucent overlay. This favors reuse of cached textures. |
| `dynamic` | `lvgl-dynamic-v1`: six dashboard cards whose arc values, numeric labels, bar widths, colors, corner radii and shadows change continuously, plus a moving translucent overlay. This exercises drawing content that cannot simply reuse an unchanged texture. |

Scene IDs differ deliberately: results from different scenes are not direct
before/after measurements. The list workload is unchanged from the original
OpenGL ES benchmark. The dynamic layout uses two columns in portrait and
three in landscape, with six cards in both cases.

`--connector ID` selects a connector explicitly; `-1` is automatic selection.
`--seconds` accepts 1 through 120, and `--cycles` accepts 1 through 100. Each
cycle creates a display, warms up for one second, measures the requested
interval, then deletes the display. Multiple cycles exercise display and EGL
resource cleanup within the same process. SIGINT and SIGTERM stop the loop,
clean up the display, and return exit status 130.

Each measured cycle emits one JSON object to stdout. LVGL diagnostics go to
stderr. The fields include:

| Field | Meaning |
| --- | --- |
| `benchmark`, `scene` | Versioned workload identifier and selected scene |
| `renderer`, `display`, `orientation` | Compiled drawing backend (`software`, `opengles`, or `nanovg`) and selected display configuration |
| `width`, `height` | Logical rendering dimensions, after orientation |
| `wall_seconds` | Actual elapsed measurement interval, excluding warmup and setup |
| `cpu_seconds` | Process CPU time, including all its threads |
| `cpu_percent_one_core` | CPU time divided by elapsed time; 100% means one fully occupied core |
| `frames`, `refresh_fps` | Refreshes that actually rendered content, and their rate |
| `refresh_mean_ms`, `refresh_p95_ms`, `refresh_p99_ms`, `refresh_max_ms` | Time from `LV_EVENT_REFR_START` to `LV_EVENT_REFR_READY` for refreshes with a `LV_EVENT_RENDER_START` event |
| `interrupted` | Whether a termination signal shortened the measurement |

Refresh time includes work and waits performed synchronously during LVGL's
refresh callback. It does not measure input latency, physical scanout
completion, or asynchronous GPU completion after the callback. The refresh
timer and display synchronization can cap the reported frame rate even when
CPU use decreases. This workload is a reproducible comparison, not a claim
about the launcher's actual frame rate or every application's performance.

Compare the backends on the same board, screen mode, orientation, power and
clock policy, and build optimization level. Keep background activity stable.
Check the GPU diagnostics for a hardware renderer before interpreting an
OpenGL ES or NanoVG result. Compare CPU use and refresh latency together; also watch
memory usage and repeated-cycle failures. A nonzero exit means invalid
arguments, initialization failure, missing rendered frames, capture failure,
or an interrupted run.

## Capture

Add `--output /tmp/lilygo-ui-benchmark.ppm` to a DRM run to save a composed
frame as a binary P6 PPM after the final cycle's timing ends. An additional
refresh and all readback and file I/O occur outside the measured interval.
OpenGL ES
capture attaches LVGL's composed texture to a temporary framebuffer, reads it
once, then restores the framebuffer and pixel-pack state. It normalizes row
order and the LVGL texture's red/blue channel convention. NanoVG renders into
the EGL window framebuffer, so capture reads that framebuffer during
`LV_EVENT_FLUSH_START`, before buffer swap can invalidate its contents. It
reads native panel dimensions, reverses the display rotation, and converts
RGBA to the same logical image orientation and colors. Software capture
reads the last flushed full-size 32-bit draw buffer. Capture errors make the
command fail. The capture verifies composed content in logical screen orientation;
it does not prove correct physical scanout or panel orientation.

For visual comparisons, add `--snapshot-ms 1000` with `--output` to freeze
each renderer's captured animation at the same elapsed time. The setting
accepts 0 through 120000 ms and affects only the final capture, not timing
or the measured animation. Without it, capture uses the final animated state.
Inspect both scenes and both orientations for missing text, incorrect colors,
clipping, transparency, and rotation before interpreting performance results.

A software build can render into memory without SDL or DRM ownership. This
is useful for checking layout and font loading on a development machine:

```sh
./build/lilygo-ui-graphics-benchmark --headless --seconds 1 \
  --orientation portrait --output /tmp/lilygo-ui-benchmark-portrait.ppm
./build/lilygo-ui-graphics-benchmark --headless --seconds 1 \
  --orientation landscape --output /tmp/lilygo-ui-benchmark-landscape.ppm
```

Headless dimensions are 568 by 1232, swapped for landscape. Headless mode is
accepted only by software builds. Headless results exclude DRM, display rotation,
page-flip waits, and GPU work, so they must not be compared against on-board
DRM measurements. Physical scanout capture should be performed externally.

## Bounded comparison runner

`tools/run-graphics-comparison.py` runs the scene/orientation matrix sequentially
using Python 3 and the standard library. Arrange exclusive DRM ownership before
running it and restore your launcher afterwards; the runner does not stop,
start, or install any service.

```sh
python3 tools/run-graphics-comparison.py \
  --binary software=/tmp/benchmark-software \
  --binary opengles=/tmp/benchmark-opengles \
  --binary nanovg=/tmp/benchmark-nanovg \
  --connector 44 --seconds 8 --cycles 3 \
  --output-dir /tmp/graphics-comparison
```

Supply one or more `--binary renderer=path` arguments; omitted renderers are
skipped. The default eight measured seconds and three display lifetimes per
command give 36 cycle records when all three backends run. The output directory
must be new or empty. Each command has a timeout of
`cycles * (seconds + 1) + 30` seconds unless `--timeout` overrides it; timeout
or interruption sends SIGTERM, allows three seconds for cleanup, then uses
SIGKILL if necessary. Any failed command, inconsistent renderer/scene labels,
missing cycle, or incomplete capture stops the comparison.

Outputs include `metadata.json` with executable hashes, aggregate
`results.jsonl`, and per-command `.stdout.log`, `.stderr.log`, `.jsonl`, and
`.ppm` files. Captures use `--snapshot-ms 1000`. `resources.jsonl` reports each
child process's user/system CPU time, wall time, return status, and peak RSS
in KiB from `wait4`. These process measurements include startup, warmup, all
cycles, cleanup, and the final screenshot. Use the benchmark cycle JSON fields
for timed rendering CPU comparisons. Peak RSS is process memory rather than
complete GPU allocation accounting. Inspect the captured images before treating
a faster result as a rendering improvement.
