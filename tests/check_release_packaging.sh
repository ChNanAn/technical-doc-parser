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

if grep -Eq '(^|/)models/|\\.onnx$' "${TEMPORARY_DIR}/cli-files.txt"; then
  echo "CLI program bundle unexpectedly contains model files" >&2
  exit 1
fi

grep -q '/bin/document_intelligence_engine$' "${TEMPORARY_DIR}/cli-files.txt"
grep -q '/share/LICENSE$' "${TEMPORARY_DIR}/cli-files.txt"
grep -q '/CMakeLists.txt$' "${TEMPORARY_DIR}/source-files.txt"
