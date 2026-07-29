#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:?usage: check_portable_cli_builder.sh ROOT_DIR}"

python3 - "$ROOT_DIR" <<'PY'
import json
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
manifest = json.loads((root / "vcpkg.json").read_text())
dockerfile = (root / "packaging/portable-cli.Dockerfile").read_text()
workflow = (root / ".github/workflows/release.yml").read_text()

baseline = manifest["builtin-baseline"]
match = re.search(r"^ARG VCPKG_COMMIT=([0-9a-f]{40})$", dockerfile, re.MULTILINE)
assert match, "portable builder must pin a full vcpkg commit"
assert match.group(1) == baseline, "portable builder and vcpkg.json baselines differ"

required_fragments = (
    "FROM ubuntu:20.04 AS build",
    "-DVCPKG_TARGET_TRIPLET=x64-linux",
    "-DBUILD_SHARED_LIBS=OFF",
    "--require-portable-linux",
    "FROM scratch AS artifacts",
)
for fragment in required_fragments:
    assert fragment in dockerfile, f"portable builder is missing: {fragment}"

required_workflow_fragments = (
    "name: Build portable Linux CLI candidate",
    "file: packaging/portable-cli.Dockerfile",
    "target: artifacts",
    "outputs: type=local,dest=build/portable-artifacts",
    "bash scripts/check_linux_cli_compatibility.sh",
    "name: portable-linux-cli-${{ github.sha }}",
    "--kind source",
)
for fragment in required_workflow_fragments:
    assert fragment in workflow, f"release workflow is missing: {fragment}"

assert "--kind all" not in workflow, "release workflow must not publish the host-built CLI"
PY
