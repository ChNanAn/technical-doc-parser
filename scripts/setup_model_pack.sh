#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODELS_DIR="${ROOT_DIR}/models"
FORCE=0

usage() {
  cat <<EOF
Usage:
  bash scripts/setup_model_pack.sh [options]

Options:
  --models-dir PATH    Install the complete baseline model pack under PATH.
  --force              Re-download every model file.
  -h, --help           Show this help message.

The command downloads missing files, replaces files with invalid SHA256
digests, verifies the complete model-pack manifest, and installs the manifest
and license summary into the model directory.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --models-dir)
      if [[ $# -lt 2 || -z "$2" ]]; then
        echo "--models-dir requires a path" >&2
        exit 2
      fi
      MODELS_DIR="$2"
      shift 2
      ;;
    --force)
      FORCE=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

for command in bash cp mkdir mv python3; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Missing required command: $command" >&2
    exit 1
  fi
done

if [[ "$MODELS_DIR" != /* ]]; then
  MODELS_DIR="${PWD}/${MODELS_DIR}"
fi
mkdir -p "$MODELS_DIR"
MODELS_DIR="$(cd "$MODELS_DIR" && pwd -P)"

echo "Preparing baseline model pack under $MODELS_DIR"

SYNC_ARGS=(sync --models-dir "$MODELS_DIR")
if [[ "$FORCE" == "1" ]]; then
  SYNC_ARGS+=(--force)
fi
python3 "${ROOT_DIR}/scripts/package_model_pack.py" "${SYNC_ARGS[@]}"

TEMP_FILES=()
cleanup() {
  if [[ ${#TEMP_FILES[@]} -gt 0 ]]; then
    rm -f "${TEMP_FILES[@]}"
  fi
}
trap cleanup EXIT

install_metadata() {
  local source="$1"
  local destination="$2"
  local temporary="${destination}.tmp.$$"
  TEMP_FILES+=("$temporary")
  cp "$source" "$temporary"
  chmod 0644 "$temporary"
  mv -f "$temporary" "$destination"
}

install_metadata \
  "${ROOT_DIR}/packaging/model-pack.v1.json" \
  "${MODELS_DIR}/MODEL-MANIFEST.json"
install_metadata \
  "${ROOT_DIR}/packaging/MODEL-LICENSES.md" \
  "${MODELS_DIR}/MODEL-LICENSES.md"

echo "Verified baseline model pack is ready under $MODELS_DIR"
