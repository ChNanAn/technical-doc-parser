#!/usr/bin/env python3

import argparse
import json
from pathlib import Path

try:
    import pypdf
    from pypdf import PdfReader
except ImportError as error:
    raise SystemExit("Install the pinned extraction dependency with: python3 -m pip install pypdf==5.9.0") from error


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_PYPDF_VERSION = "5.9.0"
DEFAULT_MANIFEST = ROOT / "data" / "raw" / "quality_baseline" / "inputs" / "prepared_manifest.json"
DEFAULT_INPUT_ROOT = ROOT / "data" / "raw" / "quality_baseline" / "inputs"
DEFAULT_OUTPUT_ROOT = ROOT / "tests" / "benchmark" / "corpus" / "pipeline_quality" / "full_text"


def reference_filename(sample_id: str) -> str:
    return sample_id.replace(":", "_") + ".txt"


def clean_extracted_text(value: str) -> str:
    lines = [line.rstrip() for line in value.splitlines()]
    while lines and not lines[0]:
        lines.pop(0)
    while lines and not lines[-1]:
        lines.pop()
    return "\n".join(lines) + ("\n" if lines else "")


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract pinned native-PDF text references for pipeline evaluation")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--input-root", type=Path, default=DEFAULT_INPUT_ROOT)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    args = parser.parse_args()

    if pypdf.__version__ != EXPECTED_PYPDF_VERSION:
        raise SystemExit(f"pypdf {EXPECTED_PYPDF_VERSION} is required, found {pypdf.__version__}")

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    args.output_root.mkdir(parents=True, exist_ok=True)
    expected_outputs: set[Path] = set()

    extracted_pages = 0
    for document in manifest.get("documents", []):
        document_id = document["id"]
        reader = PdfReader(args.input_root / "pdf" / document["pdf"])
        if len(reader.pages) != len(document["pages"]):
            raise RuntimeError(f"page count mismatch for {document_id}")

        for page_number, page in enumerate(reader.pages, start=1):
            text = clean_extracted_text(page.extract_text() or "")
            if not text.strip():
                continue
            sample_id = f"{document_id}:p{page_number:02d}"
            output_path = args.output_root / reference_filename(sample_id)
            output_path.write_text(text, encoding="utf-8")
            expected_outputs.add(output_path)
            extracted_pages += 1
            print(f"{sample_id}: {len(text)} characters -> {output_path}")

    for stale_path in args.output_root.glob("*.txt"):
        if stale_path not in expected_outputs:
            stale_path.unlink()

    print(f"Extracted {extracted_pages} native-text page references with pypdf {pypdf.__version__}")
    return 0 if extracted_pages == 11 else 1


if __name__ == "__main__":
    raise SystemExit(main())
