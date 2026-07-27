#!/usr/bin/env bash
set -euo pipefail

PRODUCER="${1:?missing partial document producer}"
PYTHON="${2:?missing Python interpreter}"
VALIDATOR="${3:?missing document validator}"
SCHEMA="${4:?missing document schema}"
INPUT_PDF="${5:?missing input PDF}"
OUTPUT_DIRECTORY="${6:?missing output directory}"
DOCUMENT_JSON="$OUTPUT_DIRECTORY/document.json"

rm -rf "$OUTPUT_DIRECTORY"
"$PRODUCER" "$INPUT_PDF" "$OUTPUT_DIRECTORY"
"$PYTHON" "$VALIDATOR" --schema "$SCHEMA" "$DOCUMENT_JSON"

"$PYTHON" - "$DOCUMENT_JSON" <<'PY'
import json
import sys
from pathlib import Path

document = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
if document["status"] != "partial":
    raise SystemExit(f"expected a partial document, got {document['status']!r}")
warnings = document.get("warnings", [])
if not warnings:
    raise SystemExit("partial document has no warnings")
if {warning["code"] for warning in warnings} != {"LAYOUT_BACKEND_FALLBACK"}:
    raise SystemExit(f"unexpected warning codes: {warnings!r}")
if document["producer"].get("run_id") != "run_partial_contract":
    raise SystemExit(f"missing run provenance: {document['producer']!r}")
PY
