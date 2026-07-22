#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

PADDLEOCR_BASELINE_DIR="${PADDLEOCR_BASELINE_DIR:-models/paddleocr/baseline}"
PADDLEOCR_PROFILE="${PADDLEOCR_PROFILE:-ppocrv5_mobile}"
PADDLEOCR_DET_MODEL="${PADDLEOCR_DET_MODEL:-${PADDLEOCR_BASELINE_DIR}/det.onnx}"
PADDLEOCR_REC_MODEL="${PADDLEOCR_REC_MODEL:-${PADDLEOCR_BASELINE_DIR}/rec.onnx}"
PADDLEOCR_DICT="${PADDLEOCR_DICT:-${PADDLEOCR_BASELINE_DIR}/ppocrv5_dict.txt}"
PADDLEOCR_DET_REVISION="${PADDLEOCR_DET_REVISION:-e6f4fa85f00e168c862bc462aebca69eef9b3d3d}"
PADDLEOCR_REC_REVISION="${PADDLEOCR_REC_REVISION:-ed152b8b495f84de93cda5709d768548a9127622}"
PADDLEOCR_DICT_REVISION="${PADDLEOCR_DICT_REVISION:-211989f046cc1878460f9e65574690c00a127a1a}"
PADDLEOCR_DET_URL="${PADDLEOCR_DET_URL:-https://huggingface.co/PaddlePaddle/PP-OCRv5_mobile_det_onnx/resolve/${PADDLEOCR_DET_REVISION}/inference.onnx}"
PADDLEOCR_REC_URL="${PADDLEOCR_REC_URL:-https://huggingface.co/PaddlePaddle/PP-OCRv5_mobile_rec_onnx/resolve/${PADDLEOCR_REC_REVISION}/inference.onnx}"
PADDLEOCR_DICT_URL="${PADDLEOCR_DICT_URL:-https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/${PADDLEOCR_DICT_REVISION}/ppocr/utils/dict/ppocrv5_dict.txt}"
PADDLEOCR_DET_SHA256="${PADDLEOCR_DET_SHA256:-a431985659dc921974177a95adcfbb90fd9e51989a5e04d70d0b75f597b6e61d}"
PADDLEOCR_REC_SHA256="${PADDLEOCR_REC_SHA256:-da72dc72ca4dc220df0dfde68c1dedc31c58d3e76a25871122e5056227d50092}"
PADDLEOCR_DICT_SHA256="${PADDLEOCR_DICT_SHA256:-d1979e9f794c464c0d2e0b70a7fe14dd978e9dc644c0e71f14158cdf8342af1b}"

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
  PADDLEOCR_PROFILE       Runtime preprocessing profile. Default: ${PADDLEOCR_PROFILE}
  PADDLEOCR_DET_MODEL     Detection model path. Default: ${PADDLEOCR_DET_MODEL}
  PADDLEOCR_REC_MODEL     Recognition model path. Default: ${PADDLEOCR_REC_MODEL}
  PADDLEOCR_DICT          Recognition dictionary path. Default: ${PADDLEOCR_DICT}
  PADDLEOCR_DET_URL       Override detection model URL.
  PADDLEOCR_REC_URL       Override recognition model URL.
  PADDLEOCR_DICT_URL      Override dictionary URL.
  PADDLEOCR_*_SHA256      Override expected file SHA256 values with custom URLs.
EOF
}

print_config() {
  cat <<EOF
PADDLEOCR_BASELINE_DIR=${PADDLEOCR_BASELINE_DIR}
PADDLEOCR_PROFILE=${PADDLEOCR_PROFILE}
PADDLEOCR_DET_MODEL=${PADDLEOCR_DET_MODEL}
PADDLEOCR_REC_MODEL=${PADDLEOCR_REC_MODEL}
PADDLEOCR_DICT=${PADDLEOCR_DICT}
PADDLEOCR_DET_URL=${PADDLEOCR_DET_URL}
PADDLEOCR_REC_URL=${PADDLEOCR_REC_URL}
PADDLEOCR_DICT_URL=${PADDLEOCR_DICT_URL}
PADDLEOCR_DET_SHA256=${PADDLEOCR_DET_SHA256}
PADDLEOCR_REC_SHA256=${PADDLEOCR_REC_SHA256}
PADDLEOCR_DICT_SHA256=${PADDLEOCR_DICT_SHA256}
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
  local expected_sha256="$3"

  if [[ "$FORCE" != "1" && -s "$output" ]]; then
    if echo "${expected_sha256}  ${output}" | sha256sum --check --status; then
      echo "Using verified file: $output"
      return
    fi
    echo "Existing file failed SHA256 verification; downloading it again: $output" >&2
  fi

  mkdir -p "$(dirname "$output")"
  echo "Downloading $output"
  echo "  $url"
  curl -L --fail --retry 3 --output "$output.tmp" "$url"
  echo "${expected_sha256}  ${output}.tmp" | sha256sum --check --status || {
    rm -f "$output.tmp"
    echo "SHA256 verification failed for $output" >&2
    exit 1
  }
  mv "$output.tmp" "$output"
}

require_cmd curl
require_cmd sha256sum

download_if_missing "$PADDLEOCR_DET_URL" "$PADDLEOCR_DET_MODEL" "$PADDLEOCR_DET_SHA256"
download_if_missing "$PADDLEOCR_REC_URL" "$PADDLEOCR_REC_MODEL" "$PADDLEOCR_REC_SHA256"
download_if_missing "$PADDLEOCR_DICT_URL" "$PADDLEOCR_DICT" "$PADDLEOCR_DICT_SHA256"

if [[ ! -s "$PADDLEOCR_DET_MODEL" || ! -s "$PADDLEOCR_REC_MODEL" || ! -s "$PADDLEOCR_DICT" ]]; then
  echo "PaddleOCR baseline setup did not produce expected model files" >&2
  exit 1
fi

cat > "${PADDLEOCR_BASELINE_DIR}/manifest.txt" <<EOF
PaddleOCR baseline
profile=${PADDLEOCR_PROFILE}
det_model=${PADDLEOCR_DET_MODEL}
rec_model=${PADDLEOCR_REC_MODEL}
dict=${PADDLEOCR_DICT}
det_url=${PADDLEOCR_DET_URL}
rec_url=${PADDLEOCR_REC_URL}
dict_url=${PADDLEOCR_DICT_URL}
det_sha256=${PADDLEOCR_DET_SHA256}
rec_sha256=${PADDLEOCR_REC_SHA256}
dict_sha256=${PADDLEOCR_DICT_SHA256}
EOF

echo "PaddleOCR baseline ready at $PADDLEOCR_BASELINE_DIR"
print_config
