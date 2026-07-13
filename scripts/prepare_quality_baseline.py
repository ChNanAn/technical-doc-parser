#!/usr/bin/env python3

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from io import BytesIO
from pathlib import Path

try:
    from PIL import Image
    from pypdf import PdfReader, PdfWriter
except ImportError as error:
    raise SystemExit("Install preparation dependencies with: python3 -m pip install pypdf pillow") from error


ROOT = Path(__file__).resolve().parents[1]
RAW_ROOT = ROOT / "data" / "raw" / "quality_baseline"
SOURCE_DIR = RAW_ROOT / "sources"
INPUT_DIR = RAW_ROOT / "inputs"
PDF_DIR = INPUT_DIR / "pdf"
IMAGE_DIR = INPUT_DIR / "images"


PDF_SELECTIONS = [
    ("irs_f1040_2024.pdf", "irs_f1040_2024_selected.pdf", [1, 2]),
    ("irs_fw4_2024.pdf", "irs_fw4_2024_selected.pdf", [1, 3]),
    ("nist_sp800_53r5.pdf", "nist_sp800_53r5_selected.pdf", [1, 15, 35, 36]),
    ("nasa_space_shuttle_reference.pdf", "nasa_space_shuttle_selected.pdf", [5, 14, 27]),
]

IMAGE_SELECTIONS = [
    ("paddleocr_book.jpg", "paddleocr_book_photo.pdf"),
    ("paddleocr_doc_with_formula.png", "paddleocr_formula_two_column.pdf"),
    ("paddleocr_medal_table.png", "paddleocr_medal_table_zh.pdf"),
    ("paddleocr_small_table.jpg", "paddleocr_small_mixed_table.pdf"),
]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def extract_pages(source_path: Path, output_path: Path, page_numbers: list[int]) -> None:
    print(f"Selecting pages {page_numbers} from {source_path.name}")
    reader = PdfReader(source_path)
    writer = PdfWriter()
    for page_number in page_numbers:
        if page_number < 1 or page_number > len(reader.pages):
            raise ValueError(f"page {page_number} is outside {source_path.name}")
        # NIST's table of contents has a dense, cyclic link-annotation graph. The
        # benchmark evaluates rendered page content, not interactive PDF links,
        # so excluding annotations keeps page extraction narrow and deterministic.
        writer.add_page(reader.pages[page_number - 1], excluded_keys=("/Annots",))
    with output_path.open("wb") as output:
        writer.write(output)


def image_to_pdf(source_path: Path, output_path: Path) -> None:
    with Image.open(source_path) as source:
        image = source.convert("RGB")
        pillow_pdf = BytesIO()
        image.save(pillow_pdf, "PDF", resolution=200.0)
        pillow_pdf.seek(0)

        # Pillow writes the current time into PDF metadata. Rewriting the page
        # with fixed metadata makes derived PDF hashes reproducible.
        reader = PdfReader(pillow_pdf)
        writer = PdfWriter()
        writer.add_page(reader.pages[0], excluded_keys=("/Annots",))
        writer.add_metadata(
            {
                "/Producer": "technical-doc-parser quality baseline",
                "/CreationDate": "D:20240101000000Z",
                "/ModDate": "D:20240101000000Z",
            }
        )
        with output_path.open("wb") as output:
            writer.write(output)


def render_pdf(engine: Path, pdf_path: Path) -> list[dict]:
    rendered_pages = []
    with tempfile.TemporaryDirectory(prefix="quality-baseline-") as temporary_directory:
        output_dir = Path(temporary_directory) / "output"
        subprocess.run(
            [
                str(engine),
                str(pdf_path),
                "--out",
                str(output_dir),
                "--dpi",
                "200",
                "--ocr-backend",
                "noop",
            ],
            check=True,
        )
        page_paths = sorted(
            (output_dir / "pages").glob("page_*.png"),
            key=lambda path: int(path.stem.split("_")[-1]),
        )
        for page_index, source_image in enumerate(page_paths, start=1):
            output_name = f"{pdf_path.stem}_p{page_index:02d}.png"
            output_path = IMAGE_DIR / output_name
            shutil.copyfile(source_image, output_path)
            with Image.open(output_path) as image:
                width, height = image.size
            rendered_pages.append(
                {
                    "page": page_index,
                    "image": output_name,
                    "width": width,
                    "height": height,
                    "sha256": sha256(output_path),
                }
            )
    return rendered_pages


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare the fixed 15-page quality baseline")
    parser.add_argument(
        "--engine",
        type=Path,
        default=ROOT / "build" / "cpp" / "app" / "document_intelligence_engine",
    )
    args = parser.parse_args()

    if not args.engine.is_file():
        raise SystemExit(f"parser executable not found: {args.engine}")

    PDF_DIR.mkdir(parents=True, exist_ok=True)
    IMAGE_DIR.mkdir(parents=True, exist_ok=True)
    for path in PDF_DIR.glob("*"):
        path.unlink()
    for path in IMAGE_DIR.glob("*"):
        path.unlink()

    documents = []
    for source_name, output_name, source_pages in PDF_SELECTIONS:
        source_path = SOURCE_DIR / source_name
        output_path = PDF_DIR / output_name
        extract_pages(source_path, output_path, source_pages)
        documents.append(
            {
                "id": output_path.stem,
                "source": source_name,
                "source_pages": source_pages,
                "pdf": output_name,
                "sha256": sha256(output_path),
                "pages": render_pdf(args.engine, output_path),
            }
        )

    for source_name, output_name in IMAGE_SELECTIONS:
        source_path = SOURCE_DIR / source_name
        output_path = PDF_DIR / output_name
        image_to_pdf(source_path, output_path)
        documents.append(
            {
                "id": output_path.stem,
                "source": source_name,
                "source_pages": [1],
                "pdf": output_name,
                "sha256": sha256(output_path),
                "pages": render_pdf(args.engine, output_path),
            }
        )

    page_count = sum(len(document["pages"]) for document in documents)
    manifest = {
        "version": 1,
        "dpi": 200,
        "page_count": page_count,
        "documents": documents,
    }
    manifest_path = INPUT_DIR / "prepared_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Prepared {page_count} pages under {INPUT_DIR}")
    print(f"Manifest: {manifest_path}")
    return 0 if page_count == 15 else 1


if __name__ == "__main__":
    sys.exit(main())
