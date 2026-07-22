#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

DOCLAYNET_MODEL_DIR="${DOCLAYNET_MODEL_DIR:-models/layout/doclaynet}"
DOCLAYNET_MODEL="${DOCLAYNET_MODEL:-${DOCLAYNET_MODEL_DIR}/model.onnx}"
DOCLAYNET_REVISION="${DOCLAYNET_REVISION:-7d56a0712e75ac09aeae31550711b5c1950eb61f}"
DOCLAYNET_MODEL_URL="${DOCLAYNET_MODEL_URL:-https://huggingface.co/neka-nat/rfdetr-doclaynet-onnx/resolve/${DOCLAYNET_REVISION}/checkpoint_best_total.onnx}"
DOCLAYNET_MODEL_SHA256="${DOCLAYNET_MODEL_SHA256:-9d22128b3f3239abd0eca5bdd5a82de26d5d5194baf8887a5de2ea70fe748794}"

usage() {
  cat <<EOF
Usage:
  bash scripts/setup_doclaynet_layout.sh [options]

Options:
  --print-config       Print the resolved model settings and exit.
  --force              Re-download an existing model.
  -h, --help           Show this help message.

Environment:
  DOCLAYNET_MODEL_DIR      Install directory. Default: ${DOCLAYNET_MODEL_DIR}
  DOCLAYNET_MODEL          Model path. Default: ${DOCLAYNET_MODEL}
  DOCLAYNET_MODEL_URL      Override the immutable model URL.
  DOCLAYNET_MODEL_SHA256   Expected SHA256; required with a custom URL.
EOF
}

print_config() {
  cat <<EOF
DOCLAYNET_MODEL_DIR=${DOCLAYNET_MODEL_DIR}
DOCLAYNET_MODEL=${DOCLAYNET_MODEL}
DOCLAYNET_REVISION=${DOCLAYNET_REVISION}
DOCLAYNET_MODEL_URL=${DOCLAYNET_MODEL_URL}
DOCLAYNET_MODEL_SHA256=${DOCLAYNET_MODEL_SHA256}
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

for command in curl sha256sum; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Missing required command: $command" >&2
    exit 1
  fi
done

USE_EXISTING=0
if [[ "$FORCE" != "1" && -s "$DOCLAYNET_MODEL" ]]; then
  if echo "${DOCLAYNET_MODEL_SHA256}  ${DOCLAYNET_MODEL}" | sha256sum --check --status; then
    echo "Using verified file: $DOCLAYNET_MODEL"
    USE_EXISTING=1
  else
    echo "Existing model failed SHA256 verification; downloading it again: $DOCLAYNET_MODEL" >&2
  fi
fi

mkdir -p "$DOCLAYNET_MODEL_DIR" "$(dirname "$DOCLAYNET_MODEL")"
if [[ "$USE_EXISTING" != "1" ]]; then
  echo "Downloading $DOCLAYNET_MODEL"
  echo "  $DOCLAYNET_MODEL_URL"
  curl -L --fail --retry 3 --output "${DOCLAYNET_MODEL}.tmp" "$DOCLAYNET_MODEL_URL"
  echo "${DOCLAYNET_MODEL_SHA256}  ${DOCLAYNET_MODEL}.tmp" | sha256sum --check --status || {
    rm -f "${DOCLAYNET_MODEL}.tmp"
    echo "SHA256 verification failed for $DOCLAYNET_MODEL" >&2
    exit 1
  }
  mv "${DOCLAYNET_MODEL}.tmp" "$DOCLAYNET_MODEL"
fi

cat > "${DOCLAYNET_MODEL_DIR}/manifest.txt" <<EOF
DocLayNet RF-DETR ONNX layout baseline
repository=neka-nat/rfdetr-doclaynet-onnx
revision=${DOCLAYNET_REVISION}
model=${DOCLAYNET_MODEL}
url=${DOCLAYNET_MODEL_URL}
sha256=${DOCLAYNET_MODEL_SHA256}
license=MIT
input=1x3x576x576
labels=Caption,Footnote,Formula,List-item,Page-footer,Page-header,Picture,Section-header,Table,Text,Title
EOF

echo "DocLayNet layout model ready at $DOCLAYNET_MODEL"
print_config
