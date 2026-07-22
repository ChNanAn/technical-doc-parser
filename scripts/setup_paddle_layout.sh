#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

PADDLE_LAYOUT_DIR="${PADDLE_LAYOUT_DIR:-models/layout/paddle}"
PADDLE_LAYOUT_MODEL="${PADDLE_LAYOUT_MODEL:-${PADDLE_LAYOUT_DIR}/pp-doclayout-v3.onnx}"
PADDLE_LAYOUT_CONFIG="${PADDLE_LAYOUT_CONFIG:-${PADDLE_LAYOUT_DIR}/inference.yml}"
PADDLE_LAYOUT_REVISION="${PADDLE_LAYOUT_REVISION:-46bbdf188bb0a772c08aed74882ce7e51a8f1ea6}"
PADDLE_LAYOUT_BASE_URL="${PADDLE_LAYOUT_BASE_URL:-https://huggingface.co/PaddlePaddle/PP-DocLayoutV3_onnx/resolve/${PADDLE_LAYOUT_REVISION}}"
PADDLE_LAYOUT_MODEL_URL="${PADDLE_LAYOUT_MODEL_URL:-${PADDLE_LAYOUT_BASE_URL}/inference.onnx}"
PADDLE_LAYOUT_CONFIG_URL="${PADDLE_LAYOUT_CONFIG_URL:-${PADDLE_LAYOUT_BASE_URL}/inference.yml}"
PADDLE_LAYOUT_MODEL_SHA256="${PADDLE_LAYOUT_MODEL_SHA256:-45bf71750b00739a41fc209f132eb104a4d6b5bb29483c9078164d8b87cf28ba}"
PADDLE_LAYOUT_CONFIG_SHA256="${PADDLE_LAYOUT_CONFIG_SHA256:-506fcfac13b3b546ae40d7886b44126420f392adb694e3f8bb6a6286a1f90fdc}"

usage() {
  cat <<EOF
Usage:
  bash scripts/setup_paddle_layout.sh [options]

Options:
  --print-config       Print the resolved settings and exit.
  --force              Re-download existing files.
  -h, --help           Show this help message.
EOF
}

print_config() {
  cat <<EOF
PADDLE_LAYOUT_DIR=${PADDLE_LAYOUT_DIR}
PADDLE_LAYOUT_MODEL=${PADDLE_LAYOUT_MODEL}
PADDLE_LAYOUT_CONFIG=${PADDLE_LAYOUT_CONFIG}
PADDLE_LAYOUT_REVISION=${PADDLE_LAYOUT_REVISION}
PADDLE_LAYOUT_MODEL_URL=${PADDLE_LAYOUT_MODEL_URL}
PADDLE_LAYOUT_CONFIG_URL=${PADDLE_LAYOUT_CONFIG_URL}
PADDLE_LAYOUT_MODEL_SHA256=${PADDLE_LAYOUT_MODEL_SHA256}
PADDLE_LAYOUT_CONFIG_SHA256=${PADDLE_LAYOUT_CONFIG_SHA256}
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

download_verified() {
  local url="$1"
  local output="$2"
  local sha256="$3"
  if [[ "$FORCE" != "1" && -s "$output" ]] && echo "${sha256}  ${output}" | sha256sum --check --status; then
    echo "Using verified file: $output"
    return
  fi
  mkdir -p "$(dirname "$output")"
  echo "Downloading $output"
  echo "  $url"
  curl -L --fail --retry 3 --output "${output}.tmp" "$url"
  echo "${sha256}  ${output}.tmp" | sha256sum --check --status || {
    rm -f "${output}.tmp"
    echo "SHA256 verification failed for $output" >&2
    exit 1
  }
  mv "${output}.tmp" "$output"
}

mkdir -p "$PADDLE_LAYOUT_DIR"
download_verified "$PADDLE_LAYOUT_MODEL_URL" "$PADDLE_LAYOUT_MODEL" "$PADDLE_LAYOUT_MODEL_SHA256"
download_verified "$PADDLE_LAYOUT_CONFIG_URL" "$PADDLE_LAYOUT_CONFIG" "$PADDLE_LAYOUT_CONFIG_SHA256"

cat > "${PADDLE_LAYOUT_DIR}/manifest.txt" <<EOF
Paddle PP-DocLayoutV3 ONNX layout baseline
repository=PaddlePaddle/PP-DocLayoutV3_onnx
revision=${PADDLE_LAYOUT_REVISION}
model=${PADDLE_LAYOUT_MODEL}
config=${PADDLE_LAYOUT_CONFIG}
model_url=${PADDLE_LAYOUT_MODEL_URL}
config_url=${PADDLE_LAYOUT_CONFIG_URL}
model_sha256=${PADDLE_LAYOUT_MODEL_SHA256}
config_sha256=${PADDLE_LAYOUT_CONFIG_SHA256}
license=Apache-2.0
input=image:1x3x800x800,im_shape:1x2,scale_factor:1x2
EOF

echo "Paddle Layout model ready at $PADDLE_LAYOUT_MODEL"
print_config
