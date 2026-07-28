#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:?usage: check_release_packaging.sh BUILD_DIR}"
TEMPORARY_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMPORARY_DIR"' EXIT

bash "${ROOT_DIR}/scripts/package_release.sh" \
  --kind all \
  --build-dir "$BUILD_DIR" \
  --output-dir "${TEMPORARY_DIR}/first"
bash "${ROOT_DIR}/scripts/package_release.sh" \
  --kind all \
  --build-dir "$BUILD_DIR" \
  --output-dir "${TEMPORARY_DIR}/second"

(
  cd "${TEMPORARY_DIR}/first"
  sha256sum --check SHA256SUMS
)

diff \
  "${TEMPORARY_DIR}/first/SHA256SUMS" \
  "${TEMPORARY_DIR}/second/SHA256SUMS"

cli_archive="$(find "${TEMPORARY_DIR}/first" -maxdepth 1 -name '*-linux-x86_64-cli.tar.gz' -print -quit)"
source_archive="$(find "${TEMPORARY_DIR}/first" -maxdepth 1 -name '*-source.tar.gz' -print -quit)"
test -n "$cli_archive"
test -n "$source_archive"

tar -tzf "$cli_archive" > "${TEMPORARY_DIR}/cli-files.txt"
tar -tzf "$source_archive" > "${TEMPORARY_DIR}/source-files.txt"

if grep -Eq '(^|/)models/|[.]onnx$' "${TEMPORARY_DIR}/cli-files.txt"; then
  echo "CLI program bundle unexpectedly contains model files" >&2
  exit 1
fi

grep -q '/bin/document_intelligence_engine$' "${TEMPORARY_DIR}/cli-files.txt"
grep -q '/PROGRAM-MANIFEST.json$' "${TEMPORARY_DIR}/cli-files.txt"
grep -q '/share/LICENSE$' "${TEMPORARY_DIR}/cli-files.txt"
grep -q '/share/licenses/pdfium/LICENSE$' "${TEMPORARY_DIR}/cli-files.txt"
grep -q '/share/licenses/onnxruntime/LICENSE$' "${TEMPORARY_DIR}/cli-files.txt"
grep -q '/share/licenses/onnxruntime/ThirdPartyNotices.txt$' "${TEMPORARY_DIR}/cli-files.txt"
grep -q '/CMakeLists.txt$' "${TEMPORARY_DIR}/source-files.txt"

mkdir -p "${TEMPORARY_DIR}/extracted"
tar -xzf "$cli_archive" -C "${TEMPORARY_DIR}/extracted"
python3 - "${TEMPORARY_DIR}/extracted" "${ROOT_DIR}" <<'PY'
import hashlib
import json
import pathlib
import sys

roots = list(pathlib.Path(sys.argv[1]).iterdir())
assert len(roots) == 1
root = roots[0]
executable = root / "bin/document_intelligence_engine"
binary = executable.read_bytes()
source_model_root = (pathlib.Path(sys.argv[2]) / "models").as_posix().encode()
assert source_model_root not in binary
assert b"models/paddleocr/baseline" in binary
manifest = json.loads((root / "PROGRAM-MANIFEST.json").read_text())
assert manifest["schema_version"] == 1
assert manifest["models_included"] is False
for entry in manifest["files"]:
    path = root / entry["path"]
    assert path.is_file(), path
    assert hashlib.sha256(path.read_bytes()).hexdigest() == entry["sha256"]
PY
