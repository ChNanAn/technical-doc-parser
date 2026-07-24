#!/usr/bin/env python3

import hashlib
import json
from pathlib import Path


CORPUS_ROOT = Path(__file__).resolve().parent / "corpus"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    manifest = json.loads((CORPUS_ROOT / "manifest.json").read_text(encoding="utf-8"))
    if manifest["sample_count"] != 15 or len(manifest["datasets"]) != 3:
        raise RuntimeError("the distributed corpus must contain 3 datasets and 15 samples")

    image_count = 0
    table_cell_count = 0
    table_text_reference_samples = 0
    for dataset in manifest["datasets"]:
        ground_truth_path = CORPUS_ROOT / dataset["ground_truth"]
        if sha256(ground_truth_path) != dataset["ground_truth_sha256"]:
            raise RuntimeError(f"ground-truth SHA256 mismatch: {ground_truth_path}")
        ground_truth = json.loads(ground_truth_path.read_text(encoding="utf-8"))
        if len(ground_truth["samples"]) != dataset["samples"]:
            raise RuntimeError(f"sample count mismatch: {ground_truth_path}")

        dataset_root = ground_truth_path.parent
        for sample in ground_truth["samples"]:
            image_path = dataset_root / sample["image"]
            if sha256(image_path) != sample["image_sha256"]:
                raise RuntimeError(f"image SHA256 mismatch: {image_path}")
            image_count += 1

            if "transcript" in sample:
                transcript_path = dataset_root / sample["transcript"]
                if sha256(transcript_path) != sample["transcript_sha256"]:
                    raise RuntimeError(f"transcript SHA256 mismatch: {transcript_path}")
            if "annotation" in sample:
                annotation_path = dataset_root / sample["annotation"]
                if sha256(annotation_path) != sample["annotation_sha256"]:
                    raise RuntimeError(f"annotation SHA256 mismatch: {annotation_path}")

            for item in sample.get("objects", []) + sample.get("cells", []):
                x0, y0, x1, y1 = item["bbox"]
                if not (0 <= x0 <= x1 <= sample["width"] + 1 and 0 <= y0 <= y1 <= sample["height"] + 1):
                    raise RuntimeError(f"bbox outside image for {sample['id']}: {item['bbox']}")

            if dataset["task"] == "table_structure":
                cells = sample.get("cells")
                source = sample.get("cell_text_source")
                if not isinstance(cells, list) or not cells:
                    raise RuntimeError(f"missing table cell references for {sample['id']}")
                if not isinstance(source, dict) or source.get("provider") != "Europe PMC":
                    raise RuntimeError(f"missing table cell provenance for {sample['id']}")
                if not all(
                    isinstance(source.get(field), str) and source[field]
                    for field in (
                        "pmcid",
                        "url",
                        "full_text_xml_sha256",
                        "license",
                        "license_url",
                        "table_wrap_id",
                        "table_label",
                        "extraction",
                    )
                ):
                    raise RuntimeError(f"incomplete table cell provenance for {sample['id']}")

                row_count = sum(item["label"] == "table row" for item in sample["objects"])
                column_count = sum(item["label"] == "table column" for item in sample["objects"])
                occupied = set()
                for cell in cells:
                    if not isinstance(cell.get("text"), str):
                        raise RuntimeError(f"table cell text must be a string for {sample['id']}")
                    values = [cell.get(field) for field in ("row_index", "column_index", "row_span", "column_span")]
                    if any(isinstance(value, bool) or not isinstance(value, int) for value in values):
                        raise RuntimeError(f"table cell grid values must be integers for {sample['id']}")
                    row_index, column_index, row_span, column_span = values
                    if (
                        row_index < 0
                        or column_index < 0
                        or row_span <= 0
                        or column_span <= 0
                        or row_index + row_span > row_count
                        or column_index + column_span > column_count
                    ):
                        raise RuntimeError(f"table cell exceeds the grid for {sample['id']}: {cell}")
                    slots = {
                        (row, column)
                        for row in range(row_index, row_index + row_span)
                        for column in range(column_index, column_index + column_span)
                    }
                    if slots & occupied:
                        raise RuntimeError(f"overlapping table cells for {sample['id']}: {cell}")
                    occupied.update(slots)
                if len(occupied) != row_count * column_count:
                    raise RuntimeError(f"table cells do not cover the grid for {sample['id']}")
                table_cell_count += len(cells)
                table_text_reference_samples += 1

    if image_count != 15:
        raise RuntimeError(f"expected 15 images, found {image_count}")
    if table_text_reference_samples != 5 or table_cell_count != 384:
        raise RuntimeError(
            f"expected 384 table cells across 5 text references, found "
            f"{table_cell_count} across {table_text_reference_samples}"
        )

    pipeline_ground_truth_path = CORPUS_ROOT / "pipeline_quality" / "ground_truth.json"
    pipeline_ground_truth = json.loads(pipeline_ground_truth_path.read_text(encoding="utf-8"))
    if len(pipeline_ground_truth["samples"]) != 15:
        raise RuntimeError("the pipeline corpus must contain 15 samples")
    full_text_references = 0
    for sample in pipeline_ground_truth["samples"]:
        relative_path = sample.get("full_text_reference")
        if relative_path is None:
            continue
        reference_path = pipeline_ground_truth_path.parent / relative_path
        if sha256(reference_path) != sample.get("full_text_reference_sha256"):
            raise RuntimeError(f"full-text reference SHA256 mismatch: {reference_path}")
        full_text_references += 1
    if full_text_references != 11:
        raise RuntimeError(f"expected 11 pipeline full-text references, found {full_text_references}")

    print(
        f"Validated {image_count} distributed benchmark images across 3 datasets "
        f"with {table_cell_count} table cells and {full_text_references} pipeline full-text references"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
