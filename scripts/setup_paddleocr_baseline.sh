#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

PADDLEOCR_BASELINE_DIR="${PADDLEOCR_BASELINE_DIR:-models/paddleocr/baseline}"
PADDLEOCR_DET_MODEL="${PADDLEOCR_DET_MODEL:-${PADDLEOCR_BASELINE_DIR}/det.onnx}"
PADDLEOCR_REC_MODEL="${PADDLEOCR_REC_MODEL:-${PADDLEOCR_BASELINE_DIR}/rec.onnx}"
PADDLEOCR_DICT="${PADDLEOCR_DICT:-${PADDLEOCR_BASELINE_DIR}/ppocrv5_dict.txt}"
PADDLEOCR_DET_URL="${PADDLEOCR_DET_URL:-https://huggingface.co/PaddlePaddle/PP-OCRv5_mobile_det_onnx/resolve/main/inference.onnx}"
PADDLEOCR_REC_URL="${PADDLEOCR_REC_URL:-https://huggingface.co/PaddlePaddle/PP-OCRv5_mobile_rec_onnx/resolve/main/inference.onnx}"
PADDLEOCR_DICT_URL="${PADDLEOCR_DICT_URL:-https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/main/ppocr/utils/dict/ppocrv5_dict.txt}"

usage() {
  cat <<EOF
Usage:
  bash scripts/setup_paddleocr_baseline.sh [options]

Options:
  --print-config       Print the resolved PaddleOCR baseline settings and exit.
  --force              Re-download existing model and dictionary files.
  -h, --help           Show this help message.

Environment:
  PADDLEOCR_BASELINE_DIR  Install directory. Default: ${PADDLEOCR_BASELINE_DIR}
  PADDLEOCR_DET_MODEL     Detection model path. Default: ${PADDLEOCR_DET_MODEL}
  PADDLEOCR_REC_MODEL     Recognition model path. Default: ${PADDLEOCR_REC_MODEL}
  PADDLEOCR_DICT          Recognition dictionary path. Default: ${PADDLEOCR_DICT}
  PADDLEOCR_DET_URL       Override detection model URL.
  PADDLEOCR_REC_URL       Override recognition model URL.
  PADDLEOCR_DICT_URL      Override dictionary URL.
EOF
}

print_config() {
  cat <<EOF
PADDLEOCR_BASELINE_DIR=${PADDLEOCR_BASELINE_DIR}
PADDLEOCR_DET_MODEL=${PADDLEOCR_DET_MODEL}
PADDLEOCR_REC_MODEL=${PADDLEOCR_REC_MODEL}
PADDLEOCR_DICT=${PADDLEOCR_DICT}
PADDLEOCR_DET_URL=${PADDLEOCR_DET_URL}
PADDLEOCR_REC_URL=${PADDLEOCR_REC_URL}
PADDLEOCR_DICT_URL=${PADDLEOCR_DICT_URL}
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

download_if_missing() {
  local url="$1"
  local output="$2"

  if [[ "$FORCE" != "1" && -s "$output" ]]; then
    echo "Using existing file: $output"
    return
  fi

  mkdir -p "$(dirname "$output")"
  echo "Downloading $output"
  echo "  $url"
  curl -L --fail --retry 3 --output "$output.tmp" "$url"
  mv "$output.tmp" "$output"
}

require_cmd curl

download_if_missing "$PADDLEOCR_DET_URL" "$PADDLEOCR_DET_MODEL"
download_if_missing "$PADDLEOCR_REC_URL" "$PADDLEOCR_REC_MODEL"
download_if_missing "$PADDLEOCR_DICT_URL" "$PADDLEOCR_DICT"

if [[ ! -s "$PADDLEOCR_DET_MODEL" || ! -s "$PADDLEOCR_REC_MODEL" || ! -s "$PADDLEOCR_DICT" ]]; then
  echo "PaddleOCR baseline setup did not produce expected model files" >&2
  exit 1
fi

cat > "${PADDLEOCR_BASELINE_DIR}/manifest.txt" <<EOF
PaddleOCR baseline
det_model=${PADDLEOCR_DET_MODEL}
rec_model=${PADDLEOCR_REC_MODEL}
dict=${PADDLEOCR_DICT}
det_url=${PADDLEOCR_DET_URL}
rec_url=${PADDLEOCR_REC_URL}
dict_url=${PADDLEOCR_DICT_URL}
EOF

echo "PaddleOCR baseline ready at $PADDLEOCR_BASELINE_DIR"
print_config
