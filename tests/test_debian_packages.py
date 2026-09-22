#!/usr/bin/env python3
"""Verify the combined source SDK/font package and optional static applications.

Example:
  python3 tests/test_debian_packages.py \
    --dev dist/lilygo-ui-appkit-dev_0.1.0_all.deb \
    --app /path/to/lilygo-ui-launcher_0.1.0_arm64.deb
"""

from __future__ import annotations

import argparse
from email.message import Message
from email.parser import Parser
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


FONT_FILES = {
    "Inter[opsz,wght].ttf", "SourceHanSansSC-Normal.otf",
    "FontAwesome5-Solid+Brands+Regular.woff",
}
FONT_SUFFIXES = {".ttf", ".otf", ".woff", ".woff2"}
FONT_LICENSES = {"NOTICE.md", "OFL-1.1.txt"}
APPKIT_DEV_MIN_VERSION = "0.1.0"
MIGRATED_PACKAGES = {
    "lilygo-cm0-appkit-dev", "lilygo-ui-fonts", "lilygo-ui-appkit-runtime",
}


class PackageError(Exception):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise PackageError(message)


def command(*args: str | Path) -> str:
    result = subprocess.run(
        [str(arg) for arg in args], capture_output=True, text=True,
        env={**os.environ, "LC_ALL": "C"},
    )
    if result.returncode:
        raise PackageError(
            f"Command failed ({result.returncode}): {' '.join(map(str, args))}\n"
            f"{result.stdout}{result.stderr}"
        )
    return result.stdout


def package_files(root: Path) -> dict[str, Path]:
    return {path.relative_to(root).as_posix(): path for path in root.rglob("*")
            if path.is_file() or path.is_symlink()}


def fields(package: Path) -> Message:
    return Parser().parsestr(command("dpkg-deb", "-f", package))


def relation(metadata: Message, field: str, expected: str) -> None:
    values = {value.strip() for value in metadata.get(field, "").split(",")}
    require(expected in values, f"{metadata['Package']}: missing {field}: {expected}")


def is_elf(path: Path) -> bool:
    if path.is_symlink():
        return False
    with path.open("rb") as stream:
        return stream.read(4) == b"\x7fELF"


def is_library_output(path: Path) -> bool:
    return bool(re.search(r"\.(?:so(?:\..*)?|a|dylib|dll|o)$", path.name))


def check_data_only(files: dict[str, Path], component: str) -> None:
    for name, path in files.items():
        require(not is_library_output(path), f"{component} contains a compiled library/object: {name}")
        require(not is_elf(path), f"{component} contains an ELF binary: {name}")


def check_no_public_fonts(files: dict[str, Path], component: str) -> None:
    for name, path in files.items():
        require(path.name not in FONT_FILES and not name.startswith("usr/share/lilygo-ui/fonts/"),
                f"{component} duplicates a shared font: {name}")


def verify(dev: Path, apps: list[Path], readelf: str | None) -> None:
    dev_metadata = fields(dev)
    require(dev_metadata["Package"] == "lilygo-ui-appkit-dev", "Wrong source SDK package")
    version = dev_metadata["Version"]
    require(bool(version), "Source SDK package is missing its version")
    require(dev_metadata["Architecture"] == "all", "Source SDK must be architecture-independent")
    require("lilygo-ui-appkit-abi-" not in str(dev_metadata),
            "Source SDK retains the removed shared-library ABI capability")
    depends = dev_metadata.get("Depends", "")
    require("lilygo-ui-appkit-runtime" not in depends and "lilygo-ui-appkit-dev" not in depends,
            "Source SDK retains a runtime-package or self dependency")
    for dependency in ("cmake", "pkg-config", "libdrm-dev", "libfreetype6-dev"):
        relation(dev_metadata, "Depends", dependency)
    for package in MIGRATED_PACKAGES:
        relation(dev_metadata, "Provides", f"{package} (= {version})")
        for field in ("Conflicts", "Replaces"):
            relation(dev_metadata, field, package)

    with tempfile.TemporaryDirectory(prefix="lilygo-static-package-test-") as temporary:
        work = Path(temporary)
        dev_root = work / "dev"
        command("dpkg-deb", "-x", dev, dev_root)
        dev_files = package_files(dev_root)
        check_data_only(dev_files, "Source SDK")
        expected_fonts = {f"usr/share/lilygo-ui/fonts/{font}" for font in FONT_FILES}
        for font in FONT_FILES:
            path = dev_files.get(f"usr/share/lilygo-ui/fonts/{font}")
            require(path is not None and path.stat().st_size > 0, f"Source SDK is missing shared font {font}")
        for license_name in FONT_LICENSES:
            require(f"usr/share/doc/lilygo-ui-appkit-dev/{license_name}" in dev_files,
                    f"Source SDK is missing {license_name}")
        actual_fonts = {name for name, path in dev_files.items() if path.suffix.lower() in FONT_SUFFIXES}
        require(actual_fonts == expected_fonts, "Source SDK contains unexpected or duplicate font binaries")
        require(not any(name.startswith("usr/share/doc/lilygo-ui-appkit-runtime/") for name in dev_files),
                "Source SDK retains the obsolete runtime documentation directory")

        configs = [path for path in dev_files.values() if path.name == "LilyGoUIConfig.cmake"]
        require(len(configs) == 1, "Source SDK must contain one LilyGoUI CMake package")
        config_dir = configs[0].parent
        for name in (
            "CM0AppKit.cmake", "LilyGoUIConfigVersion.cmake", "config/lv_conf.h",
            "include/cm0/app.h", "include/cm0/typography.h", "include/cm0/status_bar.h",
            "include/cm0/system_status.h", "src/runtime.cpp", "src/typography.cpp",
            "src/status_bar.cpp", "src/system_status.cpp", "third_party/lvgl-9.5.0.tar.gz",
            "patches/lvgl-9.5.0-drm-recovery.patch", "patches/lvgl-9.5.0-drm-software-rotation.patch",
            "patches/lvgl-9.5.0-drm-egl.patch", "patches/lvgl-9.5.0-drm-handoff.patch",
            "patches/lvgl-9.5.0-evdev-sync.patch", "patches/lvgl-9.5.0-gles2-shaders.patch",
            "patches/lvgl-9.5.0-nanovg-drm.patch",
        ):
            require((config_dir / name).is_file(), f"Source SDK is missing {name}")
        for license_name in FONT_LICENSES:
            require((config_dir / "assets/fonts/licenses" / license_name).is_file(),
                    f"Source SDK font-license helper is missing {license_name}")
        require(not list(config_dir.glob("LilyGoUITargets*.cmake")),
                "Source SDK still exports imported shared-library targets")
        controls = work / "dev-control"
        command("dpkg-deb", "-e", dev, controls)
        require(not (controls / "shlibs").exists(), "Source SDK retains obsolete shlibs metadata")
        require(not (controls / "triggers").exists(), "Source SDK retains obsolete shared-library triggers")

        for index, app in enumerate(apps):
            require(bool(readelf), "--app checks require readelf; supply --readelf /path/to/readelf")
            app_metadata = fields(app)
            name = app_metadata["Package"]
            require(bool(name) and name.startswith("lilygo-ui-"), f"Invalid application package: {name}")
            relation(app_metadata, "Depends", f"lilygo-ui-appkit-dev (>= {APPKIT_DEV_MIN_VERSION})")
            require("lilygo-ui-appkit-runtime" not in app_metadata.get("Depends", ""),
                    f"{name} still requires the obsolete runtime package")
            require("lilygo-ui-appkit-abi-" not in str(app_metadata),
                    f"{name} still requires a shared-library ABI capability")
            app_root = work / f"app-{index}"
            command("dpkg-deb", "-x", app, app_root)
            app_files = package_files(app_root)
            check_no_public_fonts(app_files, name)
            require(not any(is_library_output(path) and
                            ("appkit" in path.name.lower() or "lvgl" in path.name.lower())
                            for path in app_files.values()),
                    f"{name} bundles an AppKit/LVGL compiled library")
            elf_files = [path for path in app_files.values() if is_elf(path)]
            require(bool(elf_files), f"{name} does not contain an ELF application binary")
            for binary in elf_files:
                dynamic = command(readelf, "-d", "--wide", binary)
                needed = re.findall(r"\(NEEDED\).*\[([^]]+)\]", dynamic)
                require(not any("lilygo-ui-appkit" in dependency or "lvgl" in dependency.lower()
                                for dependency in needed),
                        f"{name}/{binary.name} still depends on a shared AppKit/LVGL library")
            print(f"PASS: {name} {app_metadata['Version']}; static AppKit/LVGL and dev package dependency")
    print(f"PASS: {version}; single architecture-independent source SDK/font package, licenses, and migration")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dev", required=True, type=Path)
    parser.add_argument("--app", action="append", type=Path, default=[],
                        help="Optional application package; may be repeated")
    parser.add_argument("--readelf", default=shutil.which("aarch64-linux-gnu-readelf") or shutil.which("readelf"),
                        help="ELF inspector for --app; defaults to AArch64 cross readelf or native readelf")
    args = parser.parse_args()
    try:
        verify(args.dev.resolve(), [path.resolve() for path in args.app], args.readelf)
    except (PackageError, OSError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
