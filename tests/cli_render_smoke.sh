#!/usr/bin/env bash
set -euo pipefail

DOC_PARSER="${1:?usage: cli_render_smoke.sh /path/to/doc_parser}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PDF_PATH="$ROOT_DIR/tests/fixtures/pdfs/pdfjs-basicapi.pdf"
OUT_DIR="/tmp/technical-doc-parser-cli-smoke-output"
PNG_PATH="$OUT_DIR/pages/page_1.png"

rm -rf "$OUT_DIR"
"$DOC_PARSER" "$PDF_PATH" --out "$OUT_DIR" --dpi 72

test -f "$PNG_PATH"

python3 - <<'PY'
from pathlib import Path

png = Path("/tmp/technical-doc-parser-cli-smoke-output/pages/page_1.png")
expected = b"\x89PNG\r\n\x1a\n"
actual = png.read_bytes()[:8]
if actual != expected:
    raise SystemExit(f"not a PNG: {actual!r}")
PY
