#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

TABLE_TRANSFORMER_DIR="${TABLE_TRANSFORMER_DIR:-models/table/table-transformer}"
TABLE_DETECTION_MODEL="${TABLE_DETECTION_MODEL:-${TABLE_TRANSFORMER_DIR}/detection.onnx}"
TABLE_STRUCTURE_MODEL="${TABLE_STRUCTURE_MODEL:-${TABLE_TRANSFORMER_DIR}/structure.onnx}"

TABLE_DETECTION_REVISION="${TABLE_DETECTION_REVISION:-187ac355617c8fee3d69c00d461ecf8eb8a4a5b7}"
TABLE_STRUCTURE_REVISION="${TABLE_STRUCTURE_REVISION:-5387550de655512721e1b88e4e42117001ba4813}"
TABLE_DETECTION_URL="${TABLE_DETECTION_URL:-https://huggingface.co/Xenova/table-transformer-detection/resolve/${TABLE_DETECTION_REVISION}/onnx/model.onnx}"
TABLE_STRUCTURE_URL="${TABLE_STRUCTURE_URL:-https://huggingface.co/Xenova/table-transformer-structure-recognition/resolve/${TABLE_STRUCTURE_REVISION}/onnx/model.onnx}"
TABLE_DETECTION_SHA256="${TABLE_DETECTION_SHA256:-5be82ec9d157814ea8616588398d7baec17aed0780b870f7adf24b280ee1b5aa}"
TABLE_STRUCTURE_SHA256="${TABLE_STRUCTURE_SHA256:-2c90a63298df61006a45267932f47b345a8b104ce53fd504eacf11aee3c05a41}"

usage() {
  cat <<EOF
Usage:
  bash scripts/setup_table_transformer.sh [options]

Options:
  --print-config       Print the resolved settings and exit.
  --force              Re-download existing files.
  -h, --help           Show this help message.
EOF
}

print_config() {
  cat <<EOF
TABLE_TRANSFORMER_DIR=${TABLE_TRANSFORMER_DIR}
TABLE_DETECTION_MODEL=${TABLE_DETECTION_MODEL}
TABLE_STRUCTURE_MODEL=${TABLE_STRUCTURE_MODEL}
TABLE_DETECTION_REVISION=${TABLE_DETECTION_REVISION}
TABLE_STRUCTURE_REVISION=${TABLE_STRUCTURE_REVISION}
TABLE_DETECTION_URL=${TABLE_DETECTION_URL}
TABLE_STRUCTURE_URL=${TABLE_STRUCTURE_URL}
TABLE_DETECTION_SHA256=${TABLE_DETECTION_SHA256}
TABLE_STRUCTURE_SHA256=${TABLE_STRUCTURE_SHA256}
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

mkdir -p "$TABLE_TRANSFORMER_DIR"
download_verified "$TABLE_DETECTION_URL" "$TABLE_DETECTION_MODEL" "$TABLE_DETECTION_SHA256"
download_verified "$TABLE_STRUCTURE_URL" "$TABLE_STRUCTURE_MODEL" "$TABLE_STRUCTURE_SHA256"

cat > "${TABLE_TRANSFORMER_DIR}/manifest.txt" <<EOF
Table Transformer ONNX table detection and structure models
detection_repository=Xenova/table-transformer-detection
detection_revision=${TABLE_DETECTION_REVISION}
detection_sha256=${TABLE_DETECTION_SHA256}
structure_repository=Xenova/table-transformer-structure-recognition
structure_revision=${TABLE_STRUCTURE_REVISION}
structure_sha256=${TABLE_STRUCTURE_SHA256}
upstream=microsoft/table-transformer
license=MIT
EOF

echo "Table Transformer models ready under $TABLE_TRANSFORMER_DIR"
print_config
