#!/usr/bin/env python3
"""Run bounded graphics comparisons with exclusive DRM ownership already arranged."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


RENDERERS = ("software", "opengles", "nanovg")


def bounded_integer(minimum, maximum):
    def parse(value):
        try:
            result = int(value)
        except ValueError as error:
            raise argparse.ArgumentTypeError("expected an integer") from error
        if not minimum <= result <= maximum:
            raise argparse.ArgumentTypeError(f"expected {minimum}..{maximum}")
        return result

    return parse


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", action="append", required=True,
                        metavar="RENDERER=PATH",
                        help="Repeat for software, opengles and/or nanovg builds")
    parser.add_argument("--drm", default="/dev/dri/card0")
    parser.add_argument("--connector", type=bounded_integer(-1, 2**32 - 1), default=-1)
    parser.add_argument("--seconds", type=bounded_integer(1, 120), default=8)
    parser.add_argument("--cycles", type=bounded_integer(1, 100), default=3)
    parser.add_argument("--timeout", type=bounded_integer(1, 20000),
                        help="Seconds per command; default cycles*(seconds+1)+30")
    parser.add_argument("--output-dir", type=Path, required=True,
                        help="New or empty directory for logs, JSONL and PPM captures")
    args = parser.parse_args()
    if not hasattr(os, "wait4"):
        parser.error("POSIX wait4 is required; run this tool on the target Linux board")
    binaries = {}
    for specification in args.binary:
        renderer, separator, filename = specification.partition("=")
        if not separator or renderer not in RENDERERS or not filename:
            parser.error("--binary requires software=PATH, opengles=PATH or nanovg=PATH")
        if renderer in binaries:
            parser.error(f"duplicate renderer: {renderer}")
        path = Path(filename).resolve()
        if not path.is_file() or not os.access(path, os.X_OK):
            parser.error(f"binary is not executable: {path}")
        binaries[renderer] = path
    args.binaries = binaries
    args.timeout = args.timeout or args.cycles * (args.seconds + 1) + 30
    args.output_dir = args.output_dir.resolve()
    if args.output_dir.exists() and (not args.output_dir.is_dir() or
                                   any(args.output_dir.iterdir())):
        parser.error("--output-dir must be new or empty; existing results are not overwritten")
    return args


def wait_until(pid, deadline):
    while True:
        try:
            result = os.wait4(pid, os.WNOHANG)
        except InterruptedError:
            continue
        if result[0]:
            return result
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return None
        time.sleep(min(0.05, remaining))


def signal_group(pid, signum):
    try:
        os.killpg(pid, signum)
    except ProcessLookupError:
        pass


def run_command(command, stdout_path, stderr_path, timeout):
    started = time.monotonic()
    interrupted = False
    timed_out = False
    with stdout_path.open("w") as stdout, stderr_path.open("w") as stderr:
        process = subprocess.Popen(command, stdout=stdout, stderr=stderr,
                                   start_new_session=True)
        try:
            result = wait_until(process.pid, started + timeout)
            timed_out = result is None
        except KeyboardInterrupt:
            interrupted = True
            result = None
        if result is None:
            # Permit LVGL to release DRM/GPU resources, then bound a hung exit.
            signal_group(process.pid, signal.SIGTERM)
            result = wait_until(process.pid, time.monotonic() + 3)
            if result is None:
                signal_group(process.pid, signal.SIGKILL)
                result = os.wait4(process.pid, 0)
        _, status, usage = result
        process.returncode = os.waitstatus_to_exitcode(status)
    wall = time.monotonic() - started
    cpu = usage.ru_utime + usage.ru_stime
    # Linux reports KiB; macOS reports bytes (useful for local mock validation).
    rss_kib = usage.ru_maxrss / 1024 if sys.platform == "darwin" else usage.ru_maxrss
    return {
        "command": command,
        "exit_code": process.returncode,
        "timed_out": timed_out,
        "interrupted": interrupted,
        "process_wall_seconds": wall,
        "process_user_cpu_seconds": usage.ru_utime,
        "process_system_cpu_seconds": usage.ru_stime,
        "process_cpu_percent_one_core": cpu * 100 / wall if wall else 0,
        "process_max_rss_kib": rss_kib,
    }


def read_samples(path, renderer, scene, orientation, cycles):
    samples = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    if len(samples) != cycles:
        raise ValueError(f"expected {cycles} cycle records, got {len(samples)}")
    for cycle, sample in enumerate(samples, 1):
        expected = {"renderer": renderer, "scene": scene, "orientation": orientation,
                    "display": "drm", "cycle": cycle, "interrupted": False}
        for key, value in expected.items():
            if sample.get(key) != value:
                raise ValueError(f"cycle {cycle}: expected {key}={value!r}, got {sample.get(key)!r}")
        if sample.get("frames", 0) <= 0 or sample.get("wall_seconds", 0) <= 0:
            raise ValueError(f"cycle {cycle}: no measured frames or elapsed time")
    return samples


def validate_ppm(path, sample):
    with path.open("rb") as image:
        if image.readline().strip() != b"P6":
            raise ValueError("capture is not a binary P6 PPM")
        dimensions = image.readline().split()
        expected = [str(sample["width"]).encode(), str(sample["height"]).encode()]
        if dimensions != expected or image.readline().strip() != b"255":
            raise ValueError("capture dimensions or color depth do not match the benchmark")
        if len(image.read()) != sample["width"] * sample["height"] * 3:
            raise ValueError("capture pixel data is incomplete")


def stop_on_signal(signum, frame):
    raise KeyboardInterrupt


def main():
    args = arguments()
    signal.signal(signal.SIGTERM, stop_on_signal)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    metadata = {
        "comparison": "lilygo-ui-graphics-v1",
        "platform": list(os.uname()),
        "seconds": args.seconds,
        "cycles": args.cycles,
        "timeout_seconds": args.timeout,
        "drm": args.drm,
        "connector": args.connector,
        "snapshot_ms": 1000,
        "binaries": {
            name: {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
            for name, path in args.binaries.items()
        },
    }
    (args.output_dir / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    with (args.output_dir / "results.jsonl").open("w") as results, \
         (args.output_dir / "resources.jsonl").open("w") as resources:
        # Group identical scene/orientation cases together; never share DRM concurrently.
        for scene in ("list", "dynamic"):
            for orientation in ("portrait", "landscape"):
                for renderer in RENDERERS:
                    if renderer not in args.binaries:
                        continue
                    stem = f"{renderer}-{scene}-{orientation}"
                    prefix = args.output_dir / stem
                    stdout_path = prefix.with_suffix(".stdout.log")
                    stderr_path = prefix.with_suffix(".stderr.log")
                    image_path = prefix.with_suffix(".ppm")
                    command = [str(args.binaries[renderer]), "--drm", args.drm,
                               "--connector", str(args.connector), "--seconds", str(args.seconds),
                               "--cycles", str(args.cycles), "--scene", scene,
                               "--orientation", orientation, "--snapshot-ms", "1000",
                               "--output", str(image_path)]
                    print(f"Running {stem} ({args.cycles} cycles)", flush=True)
                    resource = run_command(command, stdout_path, stderr_path, args.timeout)
                    resource.update(renderer=renderer, scene=scene, orientation=orientation)
                    resources.write(json.dumps(resource) + "\n")
                    resources.flush()
                    if resource["exit_code"] or resource["timed_out"] or resource["interrupted"]:
                        print(f"Failed {stem}: exit={resource['exit_code']}, "
                              f"timeout={resource['timed_out']}; see {stderr_path}", file=sys.stderr)
                        return 130 if resource["interrupted"] else 1
                    try:
                        samples = read_samples(stdout_path, renderer, scene, orientation, args.cycles)
                        validate_ppm(image_path, samples[-1])
                    except (OSError, ValueError, KeyError, TypeError) as error:
                        print(f"Invalid result for {stem}: {error}; see {stdout_path}", file=sys.stderr)
                        return 1
                    with prefix.with_suffix(".jsonl").open("w") as per_run:
                        for sample in samples:
                            line = json.dumps(sample) + "\n"
                            per_run.write(line)
                            results.write(line)
                    results.flush()
    print(f"Comparison complete: {args.output_dir}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except OSError as error:
        print(f"Cannot run graphics comparison: {error}", file=sys.stderr)
        sys.exit(1)
