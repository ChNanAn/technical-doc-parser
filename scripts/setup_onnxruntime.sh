#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

ONNXRUNTIME_VERSION="${ONNXRUNTIME_VERSION:-1.18.1}"
ONNXRUNTIME_PLATFORM="${ONNXRUNTIME_PLATFORM:-linux-x64}"
ONNXRUNTIME_PACKAGE="onnxruntime-${ONNXRUNTIME_PLATFORM}-${ONNXRUNTIME_VERSION}.tgz"
ONNXRUNTIME_ROOT="${ONNXRUNTIME_ROOT:-third_party/onnxruntime-${ONNXRUNTIME_PLATFORM}-${ONNXRUNTIME_VERSION}}"
ONNXRUNTIME_URL="${ONNXRUNTIME_URL:-https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/${ONNXRUNTIME_PACKAGE}}"
ONNXRUNTIME_SHA256="${ONNXRUNTIME_SHA256:-a0994512ec1e1debc00c18bfc7a5f16249f6ebd6a6128ff2034464cc380ea211}"

usage() {
  cat <<EOF
Usage:
  bash scripts/setup_onnxruntime.sh [options]

Options:
  --print-config       Print the resolved ONNX Runtime settings and exit.
  --force              Remove the existing ONNX Runtime directory before downloading.
  -h, --help           Show this help message.

Environment:
  ONNXRUNTIME_VERSION  ONNX Runtime version. Default: ${ONNXRUNTIME_VERSION}
  ONNXRUNTIME_PLATFORM Package platform. Default: ${ONNXRUNTIME_PLATFORM}
  ONNXRUNTIME_ROOT     Install directory. Default: ${ONNXRUNTIME_ROOT}
  ONNXRUNTIME_URL      Override download URL.
  ONNXRUNTIME_SHA256   Expected archive SHA256.
EOF
}

print_config() {
  cat <<EOF
ONNXRUNTIME_VERSION=${ONNXRUNTIME_VERSION}
ONNXRUNTIME_PACKAGE=${ONNXRUNTIME_PACKAGE}
ONNXRUNTIME_ROOT=${ONNXRUNTIME_ROOT}
ONNXRUNTIME_URL=${ONNXRUNTIME_URL}
ONNXRUNTIME_SHA256=${ONNXRUNTIME_SHA256}
EOF
}

FORCE=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --print-config)
      print_config
      exit 0
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

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

if [[ "$FORCE" == "1" ]]; then
  rm -rf "$ONNXRUNTIME_ROOT"
fi

if [[ -f "$ONNXRUNTIME_ROOT/lib/libonnxruntime.so" && -d "$ONNXRUNTIME_ROOT/include" ]]; then
  echo "ONNX Runtime already installed at $ONNXRUNTIME_ROOT"
  print_config
  exit 0
fi

require_cmd curl
require_cmd tar
require_cmd sha256sum

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

archive="$tmp_dir/$ONNXRUNTIME_PACKAGE"
extract_dir="$tmp_dir/extract"

echo "Downloading ONNX Runtime ${ONNXRUNTIME_VERSION} (${ONNXRUNTIME_PLATFORM})"
echo "  ${ONNXRUNTIME_URL}"
curl -L --fail --retry 3 --output "$archive" "$ONNXRUNTIME_URL"

echo "${ONNXRUNTIME_SHA256}  ${archive}" | sha256sum --check --status || {
  echo "ONNX Runtime archive SHA256 verification failed" >&2
  exit 1
}

while IFS= read -r entry; do
  if [[ "$entry" == /* || "$entry" == ".." || "$entry" == ../* || "$entry" == */../* || "$entry" == */.. ]]; then
    echo "Unsafe path in ONNX Runtime archive: $entry" >&2
    exit 1
  fi
done < <(tar -tzf "$archive")

mkdir -p "$extract_dir"
tar -xzf "$archive" -C "$extract_dir"

package_dir="$extract_dir/onnxruntime-${ONNXRUNTIME_PLATFORM}-${ONNXRUNTIME_VERSION}"
if [[ ! -d "$package_dir" ]]; then
  echo "Downloaded ONNX Runtime package did not contain $package_dir" >&2
  exit 1
fi

mkdir -p "$(dirname "$ONNXRUNTIME_ROOT")"
rm -rf "$ONNXRUNTIME_ROOT"
mv "$package_dir" "$ONNXRUNTIME_ROOT"

if [[ ! -f "$ONNXRUNTIME_ROOT/lib/libonnxruntime.so" || ! -d "$ONNXRUNTIME_ROOT/include" ]]; then
  echo "Downloaded ONNX Runtime package did not contain expected include/ and lib/libonnxruntime.so" >&2
  exit 1
fi

echo "ONNX Runtime installed at $ONNXRUNTIME_ROOT"
print_config
