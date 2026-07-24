#!/usr/bin/env bash
set -euo pipefail

DOCUMENT_INTELLIGENCE_ENGINE="${1:?usage: cli_render_smoke.sh /path/to/document_intelligence_engine}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PDF_PATH="$ROOT_DIR/tests/fixtures/pdfs/pdfjs-basicapi.pdf"
OUT_DIR="/tmp/document-intelligence-engine-cli-smoke-output"
DEBUG_OUT_DIR="/tmp/document-intelligence-engine-cli-smoke-debug-output"
PNG_PATH="$OUT_DIR/pages/page_1.png"
JSON_PATH="$OUT_DIR/document.json"
DEBUG_JSON_PATH="$DEBUG_OUT_DIR/document.json"

rm -rf "$OUT_DIR"
rm -rf "$DEBUG_OUT_DIR"
"$DOCUMENT_INTELLIGENCE_ENGINE" "$PDF_PATH" --out "$OUT_DIR" --dpi 72
"$DOCUMENT_INTELLIGENCE_ENGINE" "$PDF_PATH" --out "$DEBUG_OUT_DIR" --dpi 72 --debug \
  --backend-config "$ROOT_DIR/config/backends.json"

test -f "$PNG_PATH"
test -f "$JSON_PATH"
test -f "$DEBUG_JSON_PATH"

python3 - <<'PY'
import json
from pathlib import Path

manifest = json.loads(Path("/tmp/document-intelligence-engine-cli-smoke-output/document.json").read_text(encoding="utf-8"))
debug_manifest = json.loads(
    Path("/tmp/document-intelligence-engine-cli-smoke-debug-output/document.json").read_text(encoding="utf-8")
)
schema_uri = "https://github.com/ChNanAn/technical-doc-parser/schemas/document.v1.schema.json"
if manifest["$schema"] != schema_uri or manifest["schema_version"] != 1:
    raise SystemExit("output does not identify Document Contract v1")
if manifest["status"] != "complete":
    raise SystemExit(f"unexpected document status: {manifest['status']!r}")
if manifest["source"]["filename"] != "pdfjs-basicapi.pdf":
    raise SystemExit(f"unexpected source filename: {manifest['source']['filename']!r}")
if manifest["source"]["media_type"] != "application/pdf":
    raise SystemExit(f"unexpected source media type: {manifest['source']['media_type']!r}")
if "path" in manifest["source"]:
    raise SystemExit("output leaks an internal source path")
if manifest["coordinate_space"]["dpi"] != 72:
    raise SystemExit(f"unexpected dpi: {manifest['coordinate_space']['dpi']!r}")
if not manifest["pages"]:
    raise SystemExit("pages array is empty")
first_page = manifest["pages"][0]
if first_page["id"] != "page_1" or first_page["number"] != 1:
    raise SystemExit(f"unexpected page identity: {first_page!r}")
if first_page["width"] <= 0 or first_page["height"] <= 0:
    raise SystemExit(f"invalid page dimensions: {first_page!r}")
if first_page["image"]["uri"] != "pages/page_1.png":
    raise SystemExit(f"unexpected image path: {first_page['image']['uri']!r}")
if "text" in first_page:
    raise SystemExit("text model should not be exported in normal mode")
if "extensions" in first_page:
    raise SystemExit("debug fields should not be exported without --debug")
debug_first_page = debug_manifest["pages"][0]
debug_key = "io.github.chnanan.technical-doc-parser.pipeline_debug"
text = debug_first_page["extensions"][debug_key]["text"]
if not text["has_text"]:
    raise SystemExit("expected debug text model to contain text")
if text["preferred_source"] != "pdf_text_layer":
    raise SystemExit(f"unexpected text source: {text['preferred_source']!r}")
if not text["lines"]:
    raise SystemExit("debug text lines array is empty")
first_line = text["lines"][0]
if first_line["text"] != "Table Of Content":
    raise SystemExit(f"unexpected first debug text line: {first_line['text']!r}")
if not first_line["spans"]:
    raise SystemExit("first debug text line has no spans")
span_texts = [span["text"] for span in first_line["spans"]]
if span_texts != ["Table", "Of", "Content"]:
    raise SystemExit(f"unexpected first debug text spans: {span_texts!r}")
debug_images = debug_first_page.get("extensions", {}).get(debug_key, {}).get("images", [])
if debug_images and debug_images[0]["image"] != "debug/page_1_preprocessed.png":
    raise SystemExit(f"unexpected debug image path: {debug_images[0]['image']!r}")

png = Path("/tmp/document-intelligence-engine-cli-smoke-output/pages/page_1.png")
expected = b"\x89PNG\r\n\x1a\n"
actual = png.read_bytes()[:8]
if actual != expected:
    raise SystemExit(f"not a PNG: {actual!r}")
PY
