#!/usr/bin/env bash
set -euo pipefail

DOC_PARSER="${1:?usage: cli_render_smoke.sh /path/to/doc_parser}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PDF_PATH="$ROOT_DIR/tests/fixtures/pdfs/pdfjs-basicapi.pdf"
OUT_DIR="/tmp/technical-doc-parser-cli-smoke-output"
DEBUG_OUT_DIR="/tmp/technical-doc-parser-cli-smoke-debug-output"
PNG_PATH="$OUT_DIR/pages/page_1.png"
JSON_PATH="$OUT_DIR/document.json"
DEBUG_JSON_PATH="$DEBUG_OUT_DIR/document.json"

rm -rf "$OUT_DIR"
rm -rf "$DEBUG_OUT_DIR"
"$DOC_PARSER" "$PDF_PATH" --out "$OUT_DIR" --dpi 72
"$DOC_PARSER" "$PDF_PATH" --out "$DEBUG_OUT_DIR" --dpi 72 --debug

test -f "$PNG_PATH"
test -f "$JSON_PATH"
test -f "$DEBUG_JSON_PATH"

python3 - <<'PY'
import json
from pathlib import Path

manifest = json.loads(Path("/tmp/technical-doc-parser-cli-smoke-output/document.json").read_text())
debug_manifest = json.loads(Path("/tmp/technical-doc-parser-cli-smoke-debug-output/document.json").read_text())
if manifest["source"]["path"] == "":
    raise SystemExit("source path is empty")
if manifest["source"]["type"] != "pdf":
    raise SystemExit(f"unexpected source type: {manifest['source']['type']!r}")
if manifest["render"]["dpi"] != 72:
    raise SystemExit(f"unexpected dpi: {manifest['render']['dpi']!r}")
if not manifest["pages"]:
    raise SystemExit("pages array is empty")
first_page = manifest["pages"][0]
if first_page["page_index"] != 0:
    raise SystemExit(f"unexpected page_index: {first_page['page_index']!r}")
if first_page["page_number"] != 1:
    raise SystemExit(f"unexpected page_number: {first_page['page_number']!r}")
if first_page["image"] != "pages/page_1.png":
    raise SystemExit(f"unexpected image path: {first_page['image']!r}")
if "text" in first_page:
    raise SystemExit("text model should not be exported in normal mode")
if "debug" in first_page:
    raise SystemExit("debug fields should not be exported without --debug")
debug_first_page = debug_manifest["pages"][0]
text = debug_first_page["debug"]["text"]
if not text["has_text"]:
    raise SystemExit("expected debug text model to contain text")
if text["preferred_source"] != "pdf_text_layer":
    raise SystemExit(f"unexpected text source: {text['preferred_source']!r}")
if not text["lines"]:
    raise SystemExit("debug text lines array is empty")
if not text["lines"][0]["spans"]:
    raise SystemExit("first debug text line has no spans")

png = Path("/tmp/technical-doc-parser-cli-smoke-output/pages/page_1.png")
expected = b"\x89PNG\r\n\x1a\n"
actual = png.read_bytes()[:8]
if actual != expected:
    raise SystemExit(f"not a PNG: {actual!r}")
PY
