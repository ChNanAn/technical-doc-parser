from __future__ import annotations

import copy
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Any

import jsonschema
import pytest


PROJECT_ROOT = Path(__file__).resolve().parents[3]
DOCUMENT_SCHEMA_PATH = PROJECT_ROOT / "schemas" / "document.v1.schema.json"
DOCUMENT_EXAMPLE_PATH = PROJECT_ROOT / "schemas" / "examples" / "document.v1.example.json"
DOCUMENT_SNAPSHOT_ROOT = PROJECT_ROOT / "tests" / "contract" / "snapshots" / "document-v1"
QUALITY_SCHEMA_PATH = PROJECT_ROOT / "schemas" / "quality-report.v1.schema.json"
QUALITY_EXAMPLE_PATH = PROJECT_ROOT / "schemas" / "examples" / "quality-report.v1.example.json"

DOCUMENT_SNAPSHOT_EXPECTATIONS = {
    "native-text.json": {
        "media_type": "application/pdf",
        "status": "complete",
        "block_ids": ["native_title", "native_body"],
    },
    "scanned-ocr.json": {
        "media_type": "image/tiff",
        "status": "partial",
        "block_ids": ["scan_heading", "scan_body"],
    },
    "complex-table.json": {
        "media_type": "application/pdf",
        "status": "complete",
        "block_ids": ["table_heading", "pump_table", "table_note"],
    },
    "multi-column.json": {
        "media_type": "application/pdf",
        "status": "complete",
        "block_ids": ["column_title", "left_1", "left_2", "right_1", "right_2"],
    },
}


def _load(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def _validator(schema_path: Path) -> jsonschema.Draft202012Validator:
    schema = _load(schema_path)
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema, format_checker=jsonschema.FormatChecker())


def _unique_by_id(values: list[dict[str, Any]], collection: str) -> dict[str, dict[str, Any]]:
    result = {value["id"]: value for value in values}
    assert len(result) == len(values), f"{collection} IDs must be unique"
    return result


def _assert_bbox_inside_page(bbox: list[float], page: dict[str, Any], owner: str) -> None:
    x0, y0, x1, y1 = bbox
    assert x0 < x1, f"{owner} bbox must have positive width"
    assert y0 < y1, f"{owner} bbox must have positive height"
    assert x1 <= page["width"], f"{owner} bbox exceeds page width"
    assert y1 <= page["height"], f"{owner} bbox exceeds page height"


def _assert_not_absolute_local_path(value: str, owner: str) -> None:
    assert not PurePosixPath(value).is_absolute(), f"{owner} must not be an absolute POSIX path"
    assert not PureWindowsPath(value).is_absolute(), f"{owner} must not be an absolute Windows path"


def _validate_source_ref(source_ref: dict[str, Any], pages: dict[str, dict[str, Any]], owner: str) -> None:
    page_id = source_ref["page_id"]
    assert page_id in pages, f"{owner} has an unresolved page"
    if "bbox" in source_ref:
        _assert_bbox_inside_page(source_ref["bbox"], pages[page_id], owner)


def _validate_document_semantics(document: dict[str, Any]) -> None:
    pages = _unique_by_id(document["pages"], "page")
    blocks = _unique_by_id(document["blocks"], "block")

    page_numbers = [page["number"] for page in pages.values()]
    assert len(set(page_numbers)) == len(page_numbers), "page numbers must be unique"

    filename = document["source"].get("filename")
    if filename is not None:
        assert "/" not in filename and "\\" not in filename, "source.filename must be a basename"

    for page in pages.values():
        image = page.get("image")
        if image is not None and "uri" in image:
            _assert_not_absolute_local_path(image["uri"], f"page {page['id']} image URI")

    for block in blocks.values():
        page_id = block.get("page_id")
        if page_id is not None:
            assert page_id in pages, f"block {block['id']} has an unresolved page"
        if "bbox" in block:
            assert page_id is not None, f"block {block['id']} bbox requires a page"
            _assert_bbox_inside_page(block["bbox"], pages[page_id], f"block {block['id']}")

        for index, source_ref in enumerate(block.get("source_refs", [])):
            _validate_source_ref(source_ref, pages, f"block {block['id']} source_refs[{index}]")

        table = block.get("table")
        if table is None:
            continue
        for row in table.get("rows", []):
            if "bbox" in row:
                assert page_id is not None, f"table row in {block['id']} bbox requires a page"
                _assert_bbox_inside_page(row["bbox"], pages[page_id], f"table row in {block['id']}")
            for cell in row["cells"]:
                if "bbox" in cell:
                    assert page_id is not None, f"table cell in {block['id']} bbox requires a page"
                    _assert_bbox_inside_page(cell["bbox"], pages[page_id], f"table cell in {block['id']}")
                for index, source_ref in enumerate(cell.get("source_refs", [])):
                    _validate_source_ref(source_ref, pages, f"table cell source_refs[{index}] in {block['id']}")

    for relation in document.get("relations", []):
        relation_id = relation.get("id", relation["type"])
        assert relation["from_block_id"] in blocks, f"relation {relation_id} has an unresolved source block"
        assert relation["to_block_id"] in blocks, f"relation {relation_id} has an unresolved target block"
        assert relation["from_block_id"] != relation["to_block_id"], f"relation {relation_id} is self-referential"

    for warning in document["warnings"]:
        if "page_id" in warning:
            assert warning["page_id"] in pages, f"warning {warning['code']} has an unresolved page"
        if "block_id" in warning:
            assert warning["block_id"] in blocks, f"warning {warning['code']} has an unresolved block"

    if document["status"] == "partial":
        assert document["warnings"], "a partial document must contain at least one warning"


def _validate_quality_semantics(report: dict[str, Any]) -> None:
    for layer_name, layer in report["layers"].items():
        names = [metric["name"] for metric in layer["metrics"]]
        assert len(set(names)) == len(names), f"{layer_name} metric names must be unique"
        for metric in layer["metrics"]:
            if metric["unit"] == "ratio":
                assert 0 <= metric["value"] <= 1, f"{metric['name']} ratio must be between zero and one"


def test_document_v1_schema_and_example_are_valid() -> None:
    example = _load(DOCUMENT_EXAMPLE_PATH)
    _validator(DOCUMENT_SCHEMA_PATH).validate(example)
    _validate_document_semantics(example)


def test_document_v1_reviewed_snapshots_are_valid() -> None:
    snapshot_paths = sorted(DOCUMENT_SNAPSHOT_ROOT.glob("*.json"))
    assert [path.name for path in snapshot_paths] == sorted(DOCUMENT_SNAPSHOT_EXPECTATIONS)

    validator = _validator(DOCUMENT_SCHEMA_PATH)
    for snapshot_path in snapshot_paths:
        snapshot = _load(snapshot_path)
        expected = DOCUMENT_SNAPSHOT_EXPECTATIONS[snapshot_path.name]

        validator.validate(snapshot)
        _validate_document_semantics(snapshot)
        assert snapshot["source"]["media_type"] == expected["media_type"]
        assert snapshot["status"] == expected["status"]
        assert [block["id"] for block in snapshot["blocks"]] == expected["block_ids"]


def test_document_v1_snapshots_cover_source_and_structure_evidence() -> None:
    native_text = _load(DOCUMENT_SNAPSHOT_ROOT / "native-text.json")
    assert {
        source_ref["kind"]
        for block in native_text["blocks"]
        for source_ref in block.get("source_refs", [])
    } == {"pdf_text_layer"}

    scanned_ocr = _load(DOCUMENT_SNAPSHOT_ROOT / "scanned-ocr.json")
    assert {warning["code"] for warning in scanned_ocr["warnings"]} == {"LAYOUT_BACKEND_FALLBACK"}
    assert {
        source_ref["kind"]
        for block in scanned_ocr["blocks"]
        for source_ref in block.get("source_refs", [])
    } == {"ocr"}

    complex_table = _load(DOCUMENT_SNAPSHOT_ROOT / "complex-table.json")
    table = complex_table["blocks"][1]["table"]
    assert len(table["rows"]) == 3
    assert table["rows"][2]["cells"][0]["column_span"] == 3

    multi_column = _load(DOCUMENT_SNAPSHOT_ROOT / "multi-column.json")
    assert [block["metadata"].get("column") for block in multi_column["blocks"][1:]] == [
        "left",
        "left",
        "right",
        "right",
    ]


def test_quality_report_v1_schema_and_example_are_valid() -> None:
    example = _load(QUALITY_EXAMPLE_PATH)
    _validator(QUALITY_SCHEMA_PATH).validate(example)
    _validate_quality_semantics(example)


def test_document_v1_accepts_open_vocabulary_and_extensions() -> None:
    example = copy.deepcopy(_load(DOCUMENT_EXAMPLE_PATH))
    example["blocks"][0]["type"] = "electrical_schematic"
    example["blocks"][0]["provider_label"] = "schematic-v2"
    example["future_top_level_field"] = {"enabled": True}

    _validator(DOCUMENT_SCHEMA_PATH).validate(example)


def test_document_v1_is_not_limited_to_pdf_sources() -> None:
    example = copy.deepcopy(_load(DOCUMENT_EXAMPLE_PATH))
    example["source"] = {
        "filename": "scan.tiff",
        "media_type": "image/tiff",
    }

    _validator(DOCUMENT_SCHEMA_PATH).validate(example)


def test_document_v1_rejects_malformed_core_fields() -> None:
    example = copy.deepcopy(_load(DOCUMENT_EXAMPLE_PATH))
    example["coordinate_space"]["origin"] = "bottom_left"

    with pytest.raises(jsonschema.ValidationError):
        _validator(DOCUMENT_SCHEMA_PATH).validate(example)


def test_document_v1_bbox_requires_a_page() -> None:
    example = copy.deepcopy(_load(DOCUMENT_EXAMPLE_PATH))
    del example["blocks"][0]["page_id"]

    with pytest.raises(jsonschema.ValidationError, match="page_id"):
        _validator(DOCUMENT_SCHEMA_PATH).validate(example)


def test_document_v1_semantics_reject_unresolved_optional_references() -> None:
    example = copy.deepcopy(_load(DOCUMENT_EXAMPLE_PATH))
    example["blocks"][0]["source_refs"][0]["page_id"] = "missing_page"

    with pytest.raises(AssertionError, match="unresolved page"):
        _validate_document_semantics(example)


def test_document_v1_semantics_reject_invalid_bbox() -> None:
    example = copy.deepcopy(_load(DOCUMENT_EXAMPLE_PATH))
    example["blocks"][0]["bbox"] = [100, 100, 50, 120]

    with pytest.raises(AssertionError, match="positive width"):
        _validate_document_semantics(example)


def test_document_v1_partial_result_requires_warning() -> None:
    example = copy.deepcopy(_load(DOCUMENT_EXAMPLE_PATH))
    example["status"] = "partial"
    example["warnings"] = []

    with pytest.raises(jsonschema.ValidationError):
        _validator(DOCUMENT_SCHEMA_PATH).validate(example)
    with pytest.raises(AssertionError, match="partial document"):
        _validate_document_semantics(example)


def test_quality_report_accepts_new_metric_names() -> None:
    example = copy.deepcopy(_load(QUALITY_EXAMPLE_PATH))
    example["layers"]["pipeline"]["metrics"].append(
        {
            "name": "formula_symbol_accuracy",
            "value": 0.83,
            "unit": "ratio",
            "direction": "higher_is_better",
        }
    )

    _validator(QUALITY_SCHEMA_PATH).validate(example)
    _validate_quality_semantics(example)


def test_quality_report_semantics_reject_duplicate_metrics() -> None:
    example = copy.deepcopy(_load(QUALITY_EXAMPLE_PATH))
    duplicate = copy.deepcopy(example["layers"]["backend"]["metrics"][0])
    example["layers"]["backend"]["metrics"].append(duplicate)

    with pytest.raises(AssertionError, match="metric names must be unique"):
        _validate_quality_semantics(example)
