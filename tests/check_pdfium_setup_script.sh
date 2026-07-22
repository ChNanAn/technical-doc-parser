#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

output="$(bash scripts/setup_pdfium.sh --print-config)"

grep -q '^PDFIUM_VERSION=151.0.7906.0$' <<<"$output"
grep -q '^PDFIUM_PACKAGE=pdfium-linux-x64.tgz$' <<<"$output"
grep -q '^PDFIUM_DIR=third_party/pdfium$' <<<"$output"
grep -q '^PDFIUM_URL=https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F7906/pdfium-linux-x64.tgz$' <<<"$output"
grep -q '^PDFIUM_SHA256=e07bc44c4e422c50eb01da742dc1ec59ad6780ce64ed91955533da8e9fe1a363$' <<<"$output"

onnxruntime_output="$(bash scripts/setup_onnxruntime.sh --print-config)"
grep -q '^ONNXRUNTIME_VERSION=1.18.1$' <<<"$onnxruntime_output"
grep -q '^ONNXRUNTIME_SHA256=a0994512ec1e1debc00c18bfc7a5f16249f6ebd6a6128ff2034464cc380ea211$' <<<"$onnxruntime_output"

paddleocr_output="$(bash scripts/setup_paddleocr_baseline.sh --print-config)"
if grep -Eq '/resolve/main/|PaddleOCR/main/' <<<"$paddleocr_output"; then
    echo "PaddleOCR dependencies must use immutable revisions" >&2
    exit 1
fi
grep -q '^PADDLEOCR_DET_SHA256=a431985659dc921974177a95adcfbb90fd9e51989a5e04d70d0b75f597b6e61d$' <<<"$paddleocr_output"
grep -q '^PADDLEOCR_REC_SHA256=da72dc72ca4dc220df0dfde68c1dedc31c58d3e76a25871122e5056227d50092$' <<<"$paddleocr_output"
grep -q '^PADDLEOCR_DICT_SHA256=d1979e9f794c464c0d2e0b70a7fe14dd978e9dc644c0e71f14158cdf8342af1b$' <<<"$paddleocr_output"

doclaynet_output="$(bash scripts/setup_doclaynet_layout.sh --print-config)"
if grep -Eq '/resolve/main/' <<<"$doclaynet_output"; then
    echo "DocLayNet dependencies must use an immutable revision" >&2
    exit 1
fi
grep -q '^DOCLAYNET_REVISION=7d56a0712e75ac09aeae31550711b5c1950eb61f$' <<<"$doclaynet_output"
grep -q '^DOCLAYNET_MODEL_SHA256=9d22128b3f3239abd0eca5bdd5a82de26d5d5194baf8887a5de2ea70fe748794$' <<<"$doclaynet_output"

paddle_layout_output="$(bash scripts/setup_paddle_layout.sh --print-config)"
if grep -Eq '/resolve/main/' <<<"$paddle_layout_output"; then
    echo "Paddle Layout dependencies must use an immutable revision" >&2
    exit 1
fi
grep -q '^PADDLE_LAYOUT_REVISION=46bbdf188bb0a772c08aed74882ce7e51a8f1ea6$' <<<"$paddle_layout_output"
grep -q '^PADDLE_LAYOUT_MODEL_SHA256=45bf71750b00739a41fc209f132eb104a4d6b5bb29483c9078164d8b87cf28ba$' <<<"$paddle_layout_output"
grep -q '^PADDLE_LAYOUT_CONFIG_SHA256=506fcfac13b3b546ae40d7886b44126420f392adb694e3f8bb6a6286a1f90fdc$' <<<"$paddle_layout_output"
