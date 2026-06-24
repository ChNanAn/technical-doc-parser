#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

output="$(bash scripts/setup_pdfium.sh --print-config)"

grep -q '^PDFIUM_VERSION=151.0.7906.0$' <<<"$output"
grep -q '^PDFIUM_PACKAGE=pdfium-linux-x64.tgz$' <<<"$output"
grep -q '^PDFIUM_DIR=third_party/pdfium$' <<<"$output"
grep -q '^PDFIUM_URL=https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F7906/pdfium-linux-x64.tgz$' <<<"$output"
