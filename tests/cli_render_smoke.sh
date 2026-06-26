#!/usr/bin/env bash
set -euo pipefail

DOC_PARSER="${1:?usage: cli_render_smoke.sh /path/to/doc_parser}"
PDF_PATH="/tmp/technical-doc-parser-cli-smoke.pdf"
OUT_DIR="/tmp/technical-doc-parser-cli-smoke-output"
PNG_PATH="$OUT_DIR/pages/page_1.png"

python3 - <<'PY'
from pathlib import Path

objects = [
    b"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
    b"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n",
    b"3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>\nendobj\n",
]

out = bytearray(b"%PDF-1.4\n")
offsets = [0]
for obj in objects:
    offsets.append(len(out))
    out.extend(obj)

xref_offset = len(out)
out.extend(f"xref\n0 {len(objects) + 1}\n".encode())
out.extend(b"0000000000 65535 f \n")
for offset in offsets[1:]:
    out.extend(f"{offset:010d} 00000 n \n".encode())
out.extend(b"trailer\n")
out.extend(f"<< /Size {len(objects) + 1} /Root 1 0 R >>\n".encode())
out.extend(b"startxref\n")
out.extend(f"{xref_offset}\n".encode())
out.extend(b"%%EOF\n")

Path("/tmp/technical-doc-parser-cli-smoke.pdf").write_bytes(out)
PY

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
