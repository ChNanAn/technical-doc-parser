#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_DIR="${QUALITY_BASELINE_SOURCE_DIR:-${ROOT_DIR}/data/raw/quality_baseline/sources}"

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

download() {
  local name="$1"
  local url="$2"
  local sha256="$3"
  local output="${SOURCE_DIR}/${name}"

  if [[ -s "$output" ]] && echo "${sha256}  ${output}" | sha256sum --check --status; then
    echo "Using verified source: $name"
    return
  fi

  mkdir -p "$SOURCE_DIR"
  echo "Downloading $name"
  curl -L --fail --retry 3 --output "${output}.tmp" "$url"
  echo "${sha256}  ${output}.tmp" | sha256sum --check --status || {
    rm -f "${output}.tmp"
    echo "SHA256 verification failed for $name" >&2
    exit 1
  }
  mv "${output}.tmp" "$output"
}

require_cmd curl
require_cmd sha256sum

download \
  irs_f1040_2024.pdf \
  https://www.irs.gov/pub/irs-prior/f1040--2024.pdf \
  0a7a54354283044cb41373c6faabfa50955d44bdf91b4b76fa1ca2bf13f6d718

download \
  irs_fw4_2024.pdf \
  https://www.irs.gov/pub/irs-prior/fw4--2024.pdf \
  e60e3af7512073bffbd7ef3c4f3a2fc9593d2aed2e420718d4ccf17659e2c3ed

download \
  nist_sp800_53r5.pdf \
  https://nvlpubs.nist.gov/nistpubs/SpecialPublications/NIST.SP.800-53r5.pdf \
  fc63bcd61715d0181dd8e85998b1e6201ae3515fc6626102101cab1841e11ec6

download \
  nasa_space_shuttle_reference.pdf \
  https://ntrs.nasa.gov/api/citations/19810022734/downloads/19810022734_Update.pdf \
  697707f3bd1044dee9d38f13207da4ee2b6e40605c7652a65bfd450dcfc4e1b7

PADDLEOCR_COMMIT=211989f046cc1878460f9e65574690c00a127a1a
PADDLEOCR_RAW="https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/${PADDLEOCR_COMMIT}"

download \
  paddleocr_book.jpg \
  "${PADDLEOCR_RAW}/tests/test_files/book.jpg" \
  782cc71a2730020ea8da815de3e40a964b7f150c183d1f8c01dd211de9dcbcc4

download \
  paddleocr_doc_with_formula.png \
  "${PADDLEOCR_RAW}/tests/test_files/doc_with_formula.png" \
  6b07d28527dc9e930804fa73df562f1a81599c6b8a1a8bbc2a80742fa9f26e80

download \
  paddleocr_medal_table.png \
  "${PADDLEOCR_RAW}/tests/test_files/medal_table.png" \
  6d50148ceccb2d5cecc50b084b5105e3167f2d55a8899b29e04c3ebe46e88fa8

download \
  paddleocr_small_table.jpg \
  "${PADDLEOCR_RAW}/tests/test_files/table.jpg" \
  acd113bb3a89b488941ee0962776a28e45897fa2802cd306ec3bb68d9043115c

echo "Quality baseline sources are ready under $SOURCE_DIR"
