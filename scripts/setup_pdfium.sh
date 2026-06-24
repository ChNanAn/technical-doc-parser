#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

PDFIUM_VERSION="${PDFIUM_VERSION:-151.0.7906.0}"
PDFIUM_RELEASE="${PDFIUM_RELEASE:-chromium/7906}"
PDFIUM_PLATFORM="${PDFIUM_PLATFORM:-linux-x64}"
PDFIUM_PACKAGE="pdfium-${PDFIUM_PLATFORM}.tgz"
PDFIUM_DIR="${PDFIUM_DIR:-third_party/pdfium}"
PDFIUM_URL="${PDFIUM_URL:-https://github.com/bblanchon/pdfium-binaries/releases/download/${PDFIUM_RELEASE//\//%2F}/${PDFIUM_PACKAGE}}"

usage() {
  cat <<EOF
Usage:
  bash scripts/setup_pdfium.sh [options]

Options:
  --print-config       Print the resolved PDFium settings and exit.
  --force              Remove the existing PDFium directory before downloading.
  -h, --help           Show this help message.

Environment:
  PDFIUM_VERSION       Expected PDFium version. Default: ${PDFIUM_VERSION}
  PDFIUM_RELEASE       GitHub release tag. Default: ${PDFIUM_RELEASE}
  PDFIUM_PLATFORM      Package platform. Default: ${PDFIUM_PLATFORM}
  PDFIUM_DIR           Install directory. Default: ${PDFIUM_DIR}
  PDFIUM_URL           Override download URL.
EOF
}

print_config() {
  cat <<EOF
PDFIUM_VERSION=${PDFIUM_VERSION}
PDFIUM_PACKAGE=${PDFIUM_PACKAGE}
PDFIUM_DIR=${PDFIUM_DIR}
PDFIUM_URL=${PDFIUM_URL}
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
  rm -rf "$PDFIUM_DIR"
fi

if [[ -f "$PDFIUM_DIR/lib/libpdfium.so" && -d "$PDFIUM_DIR/include" ]]; then
  echo "PDFium already installed at $PDFIUM_DIR"
  print_config
  exit 0
fi

require_cmd curl
require_cmd tar

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

archive="$tmp_dir/$PDFIUM_PACKAGE"

echo "Downloading PDFium ${PDFIUM_VERSION} (${PDFIUM_PLATFORM})"
echo "  ${PDFIUM_URL}"
curl -L --fail --retry 3 --output "$archive" "$PDFIUM_URL"

mkdir -p "$PDFIUM_DIR"
tar -xzf "$archive" -C "$PDFIUM_DIR"

if [[ ! -f "$PDFIUM_DIR/lib/libpdfium.so" || ! -d "$PDFIUM_DIR/include" ]]; then
  echo "Downloaded PDFium package did not contain expected include/ and lib/libpdfium.so" >&2
  exit 1
fi

echo "PDFium installed at $PDFIUM_DIR"
print_config
