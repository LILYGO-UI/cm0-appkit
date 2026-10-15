#!/usr/bin/env python3
"""Fixture tests for tools/check_appkit_compatibility.py."""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOL = Path(__file__).resolve().parents[1] / "tools" / "check_appkit_compatibility.py"
SPEC = importlib.util.spec_from_file_location("appkit_compatibility", TOOL)
assert SPEC and SPEC.loader
CHECKER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = CHECKER
SPEC.loader.exec_module(CHECKER)


def run(cwd: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args], cwd=str(cwd), text=True, capture_output=True, check=True
    )
    return result.stdout.strip()


def init_repo(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)
    run(path, "init", "-q")
    run(path, "config", "user.email", "fixture@example.invalid")
    run(path, "config", "user.name", "AppKit Fixture")


def write_sdk_files(root: Path, marker: str) -> None:
    files = {
        "include/cm0/input.h": f"// input {marker}\n",
        "src/input.cpp": f"// input implementation {marker}\n",
        "src/runtime.cpp": f"// runtime {marker}\n",
        "patches/lvgl-9.5.0-keyboard.patch": f"# keyboard {marker}\n",
        "tests/test_runtime_input.cpp": f"// test {marker}\n",
        "docs/keyboard-input-contract.md": f"# Keyboard {marker}\n",
        "README.md": f"# SDK {marker}\n",
        "lpm.toml": f"version = \"{marker}\"\n",
    }
    for relative, contents in files.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents)


class CompatibilityFixtureTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.workspace = self.root / "workspace"
        self.workspace.mkdir()
        self.canonical = self.root / "canonical"
        init_repo(self.canonical)
        write_sdk_files(self.canonical, "baseline")
        run(self.canonical, "add", ".")
        run(self.canonical, "commit", "-qm", "baseline")

        self.sdk_source = self.root / "sdk-source"
        init_repo(self.sdk_source)
        write_sdk_files(self.sdk_source, "baseline")
        run(self.sdk_source, "add", ".")
        run(self.sdk_source, "commit", "-qm", "sdk baseline")

    def tearDown(self) -> None:
        self.temp.cleanup()

    def make_submodule_like_app(self, name: str, sdk_commit: str | None = None) -> tuple[Path, Path]:
        app = self.workspace / name
        init_repo(app)
        sdk = app / "third_party/cm0-appkit"
        sdk.parent.mkdir(parents=True, exist_ok=True)
        run(self.root, "clone", "-q", str(self.sdk_source), str(sdk))
        sdk_commit = sdk_commit or run(sdk, "rev-parse", "HEAD")
        run(app, "update-index", "--add", "--cacheinfo", "160000", sdk_commit, str(CHECKER.SDK_RELATIVE))
        run(app, "commit", "-qm", "pin SDK")
        return app, sdk

    def test_old_gitlink_is_distinct_from_worktree_content(self) -> None:
        app, sdk = self.make_submodule_like_app("lilygo-ui-old")
        # Advance canonical HEAD.  The SDK remains clean and reproducible, but
        # it is pinned to the old commit and does not match current content.
        (self.canonical / "src/runtime.cpp").write_text("// runtime canonical update\n")
        run(self.canonical, "add", "src/runtime.cpp")
        run(self.canonical, "commit", "-qm", "canonical update")

        result = CHECKER.audit_workspace(self.workspace, self.canonical)
        record = result["projects"][0]
        self.assertEqual(record["pin_state"], "clean-pinned-other")
        self.assertFalse(record["content"]["worktree_consistent"])
        self.assertTrue(record["head_content"]["matches"] is False)
        self.assertEqual(record["index_gitlink"], record["sdk_head"])
        self.assertFalse(record["sdk_dirty"])

    def test_dirty_copy_and_missing_copy_are_reported(self) -> None:
        _, sdk = self.make_submodule_like_app("lilygo-ui-dirty")
        (sdk / "src/input.cpp").write_text("// local dirty change\n")
        (sdk / "lpm.toml").write_text("version = \"local-wip\"\n")
        missing = self.workspace / "lilygo-ui-missing"
        init_repo(missing)

        result = CHECKER.audit_workspace(self.workspace, self.canonical)
        by_name = {record["name"]: record for record in result["projects"]}
        dirty = by_name["lilygo-ui-dirty"]
        self.assertEqual(dirty["pin_state"], "dirty")
        self.assertTrue(dirty["sdk_dirty"])
        self.assertIn("src/input.cpp", dirty["content"]["different"])
        self.assertIn("lpm.toml", dirty["content"]["ignored_metadata_different"])
        self.assertEqual(by_name["lilygo-ui-missing"]["sdk_checkout"], "missing")
        self.assertEqual(by_name["lilygo-ui-missing"]["pin_state"], "no-gitlink")

    def test_ordinary_directory_does_not_inherit_parent_head(self) -> None:
        app = self.workspace / "lilygo-ui-ordinary"
        init_repo(app)
        sdk = app / "third_party/cm0-appkit"
        write_sdk_files(sdk, "baseline")
        run(app, "add", ".")
        run(app, "commit", "-qm", "ordinary SDK directory")

        # git -C sdk rev-parse HEAD would resolve the parent repository.  The
        # checker must classify this as an embedded copy with no SDK HEAD.
        result = CHECKER.audit_workspace(self.workspace, self.canonical)
        record = result["projects"][0]
        self.assertEqual(record["sdk_checkout"], "embedded-copy")
        self.assertIsNone(record["sdk_head"])
        self.assertEqual(record["pin_state"], "no-gitlink")
        self.assertTrue(record["content"]["worktree_consistent"])

    def test_json_cli_output(self) -> None:
        app = self.workspace / "lilygo-ui-json"
        init_repo(app)
        process = subprocess.run(
            [
                "python3",
                str(TOOL),
                "--workspace",
                str(self.workspace),
                "--canonical",
                str(self.canonical),
                "--app",
                app.name,
                "--json",
            ],
            text=True,
            capture_output=True,
            check=True,
        )
        payload = json.loads(process.stdout)
        self.assertEqual(payload["projects"][0]["name"], app.name)
        self.assertEqual(payload["summary"]["projects"], 1)


if __name__ == "__main__":
    unittest.main()
