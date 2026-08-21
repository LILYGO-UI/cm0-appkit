# Bundled LVGL source

`lvgl-9.5.0.tar.gz` is a build-only source distribution derived from the
upstream LVGL `v9.5.0` tag at commit
`85aa60d18b3d5e5588d7b247abf90198f07c8a63`.

The archive contains the upstream CMake entry point, desktop CMake support,
public metadata and headers, and the complete `src/` tree. Examples, demos,
tests, documentation, and build-system integrations unused by CM0 are omitted.
The upstream `LICENCE.txt` and `COPYRIGHTS.md` files are included in the
archive. LVGL is distributed under the MIT license.

Archive SHA256:

```text
280253ea1dbb9dab9aa2cdab56fb8d646c98b04c171ad52956784d1ffde8152e
```

AppKit extracts this archive into the consumer's build directory, applies the
CM0 patches there, and statically links the resulting `lvgl` target into the
final application. The archive itself is never installed by an application
package.
