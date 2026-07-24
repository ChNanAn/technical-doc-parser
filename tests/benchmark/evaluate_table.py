#!/usr/bin/env python3

"""Evaluate table structure predictions with class-aware IoU matching."""

from __future__ import annotations

import argparse
import sys
import unicodedata
from pathlib import Path
from typing import Any

from benchmark_common import (
    EvaluationError,
    align_predictions,
    error_rate,
    evaluate_object_detection,
    levenshtein_distance,
    load_json,
    maximum_iou_matching,
    safe_ratio,
    validate_bbox,
    validate_document,
    write_report,
)


DEFAULT_GROUND_TRUTH = Path(__file__).resolve().parent / "corpus" / "table_pubtables" / "ground_truth.json"


def normalize_text(text: str, ignore_case: bool) -> str:
    normalized = " ".join(unicodedata.normalize("NFKC", text).split())
    return normalized.casefold() if ignore_case else normalized


def parse_cells(sample: dict[str, Any], description: str) -> list[dict[str, Any]]:
    cells = sample.get("cells", [])
    if not isinstance(cells, list):
        raise EvaluationError(f"{description} cells must be an array")
    parsed = []
    for cell_index, cell in enumerate(cells):
        if not isinstance(cell, dict):
            raise EvaluationError(f"{description} cell {cell_index} must be an object")
        text = cell.get("text")
        if not isinstance(text, str):
            raise EvaluationError(f"{description} cell {cell_index} is missing text")
        parsed.append(
            {
                "bbox": validate_bbox(cell.get("bbox"), f"{description} cell {cell_index}"),
                "text": text,
            }
        )
    return parsed


def evaluate_cell_text(
    ground_truth: dict[str, Any],
    predictions: dict[str, Any],
    iou_threshold: float,
    ignore_case: bool,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    references = validate_document(ground_truth, "table_structure", "ground truth")
    prediction_samples = validate_document(predictions, "table_structure", "predictions")
    aligned = align_predictions(references, prediction_samples)
    totals = {
        "cell_text_reference_samples": 0,
        "reference_cells": 0,
        "predicted_cells": 0,
        "matched_cells": 0,
        "unmatched_reference_cells": 0,
        "unmatched_prediction_cells": 0,
        "exact_text_cells": 0,
        "reference_characters": 0,
        "predicted_characters": 0,
        "character_edit_distance": 0,
    }
    sample_reports = []

    for reference_sample, prediction_sample in aligned:
        if "cells" not in reference_sample:
            sample_reports.append({"available": False})
            continue
        reference_cells = parse_cells(reference_sample, f"ground-truth sample {reference_sample['id']!r}")
        prediction_cells = (
            []
            if prediction_sample is None
            else parse_cells(prediction_sample, f"prediction sample {prediction_sample['id']!r}")
        )
        matches = maximum_iou_matching(
            [cell["bbox"] for cell in reference_cells],
            [cell["bbox"] for cell in prediction_cells],
            iou_threshold,
        )
        prediction_by_reference = {
            reference_index: prediction_index for reference_index, prediction_index, _ in matches
        }
        reference_characters = 0
        predicted_characters = 0
        character_edit_distance = 0
        exact_text_cells = 0
        for reference_index, reference_cell in enumerate(reference_cells):
            reference_text = normalize_text(reference_cell["text"], ignore_case)
            prediction_index = prediction_by_reference.get(reference_index)
            prediction_text = (
                ""
                if prediction_index is None
                else normalize_text(prediction_cells[prediction_index]["text"], ignore_case)
            )
            reference_characters += len(reference_text)
            predicted_characters += len(prediction_text)
            character_edit_distance += levenshtein_distance(reference_text, prediction_text)
            exact_text_cells += prediction_index is not None and reference_text == prediction_text

        matched_cells = len(matches)
        sample_report = {
            "available": True,
            "reference_cells": len(reference_cells),
            "predicted_cells": len(prediction_cells),
            "matched_cells": matched_cells,
            "unmatched_reference_cells": len(reference_cells) - matched_cells,
            "unmatched_prediction_cells": len(prediction_cells) - matched_cells,
            "cell_coverage": safe_ratio(matched_cells, len(reference_cells)),
            "exact_text_cells": exact_text_cells,
            "exact_text_cell_rate": safe_ratio(exact_text_cells, len(reference_cells)),
            "reference_characters": reference_characters,
            "predicted_characters": predicted_characters,
            "character_edit_distance": character_edit_distance,
            "table_text_cer": error_rate(character_edit_distance, reference_characters),
        }
        sample_reports.append(sample_report)
        totals["cell_text_reference_samples"] += 1
        for field in (
            "reference_cells",
            "predicted_cells",
            "matched_cells",
            "unmatched_reference_cells",
            "unmatched_prediction_cells",
            "exact_text_cells",
            "reference_characters",
            "predicted_characters",
            "character_edit_distance",
        ):
            totals[field] += sample_report[field]

    if totals["cell_text_reference_samples"] == 0:
        summary = {
            **totals,
            "cell_coverage": None,
            "exact_text_cell_rate": None,
            "table_text_cer": None,
        }
    else:
        summary = {
            **totals,
            "cell_coverage": safe_ratio(totals["matched_cells"], totals["reference_cells"]),
            "exact_text_cell_rate": safe_ratio(totals["exact_text_cells"], totals["reference_cells"]),
            "table_text_cer": error_rate(totals["character_edit_distance"], totals["reference_characters"]),
        }
    return summary, sample_reports


def evaluate_table(
    ground_truth: dict[str, Any],
    predictions: dict[str, Any],
    iou_threshold: float = 0.5,
    ignore_case: bool = False,
) -> dict[str, Any]:
    report = evaluate_object_detection(
        ground_truth,
        predictions,
        task="table_structure",
        reference_label_fields=("label",),
        prediction_label_fields=("label",),
        iou_threshold=iou_threshold,
    )
    cell_summary, cell_sample_reports = evaluate_cell_text(
        ground_truth,
        predictions,
        iou_threshold,
        ignore_case,
    )
    report["config"]["cell_text"] = {
        "matching": "maximum_cardinality_by_bbox",
        "iou_threshold": iou_threshold,
        "unicode_normalization": "NFKC",
        "whitespace": "collapsed",
        "ignore_case": ignore_case,
        "unmatched_reference": "empty_prediction",
    }
    report["summary"].update(cell_summary)
    for sample_report, cell_report in zip(report["samples"], cell_sample_reports):
        sample_report["cell_text"] = cell_report
    return report


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--predictions", required=True, type=Path, help="Prediction JSON file")
    parser.add_argument("--ground-truth", type=Path, default=DEFAULT_GROUND_TRUTH, help="Ground-truth JSON file")
    parser.add_argument("--output", type=Path, help="Write the metric report to this JSON file")
    parser.add_argument("--iou-threshold", type=float, default=0.5, help="Minimum IoU for a true positive")
    parser.add_argument("--minimum-micro-f1", type=float, help="Fail if micro F1 is below this regression floor")
    parser.add_argument("--ignore-case", action="store_true", help="Case-fold cell text before calculating CER")
    parser.add_argument(
        "--maximum-table-text-cer",
        type=float,
        help="Fail if structure-matched table text CER exceeds this regression ceiling",
    )
    parser.add_argument("--quiet", action="store_true", help="Do not print the metric report to stdout")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.minimum_micro_f1 is not None and not 0.0 <= args.minimum_micro_f1 <= 1.0:
            raise EvaluationError("--minimum-micro-f1 must be in [0, 1]")
        if args.maximum_table_text_cer is not None and args.maximum_table_text_cer < 0.0:
            raise EvaluationError("--maximum-table-text-cer must be non-negative")
        report = evaluate_table(
            load_json(args.ground_truth),
            load_json(args.predictions),
            args.iou_threshold,
            args.ignore_case,
        )
        write_report(report, args.output, args.quiet)
        if args.minimum_micro_f1 is not None and report["summary"]["micro_f1"] < args.minimum_micro_f1:
            print(
                f"error: table micro F1 {report['summary']['micro_f1']:.6f} is below "
                f"{args.minimum_micro_f1:.6f}",
                file=sys.stderr,
            )
            return 1
        table_text_cer = report["summary"]["table_text_cer"]
        if args.maximum_table_text_cer is not None and table_text_cer is None:
            raise EvaluationError("--maximum-table-text-cer requires ground-truth cells")
        if args.maximum_table_text_cer is not None and table_text_cer > args.maximum_table_text_cer:
            print(
                f"error: table text CER {table_text_cer:.6f} exceeds maximum "
                f"{args.maximum_table_text_cer:.6f}",
                file=sys.stderr,
            )
            return 1
    except EvaluationError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
