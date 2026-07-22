#!/usr/bin/env python3

"""Evaluate table structure predictions with class-aware IoU matching."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

from benchmark_common import EvaluationError, evaluate_object_detection, load_json, write_report


DEFAULT_GROUND_TRUTH = Path(__file__).resolve().parent / "corpus" / "table_pubtables" / "ground_truth.json"


def evaluate_table(
    ground_truth: dict[str, Any], predictions: dict[str, Any], iou_threshold: float = 0.5
) -> dict[str, Any]:
    return evaluate_object_detection(
        ground_truth,
        predictions,
        task="table_structure",
        reference_label_fields=("label",),
        prediction_label_fields=("label",),
        iou_threshold=iou_threshold,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--predictions", required=True, type=Path, help="Prediction JSON file")
    parser.add_argument("--ground-truth", type=Path, default=DEFAULT_GROUND_TRUTH, help="Ground-truth JSON file")
    parser.add_argument("--output", type=Path, help="Write the metric report to this JSON file")
    parser.add_argument("--iou-threshold", type=float, default=0.5, help="Minimum IoU for a true positive")
    parser.add_argument("--quiet", action="store_true", help="Do not print the metric report to stdout")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        report = evaluate_table(load_json(args.ground_truth), load_json(args.predictions), args.iou_threshold)
        write_report(report, args.output, args.quiet)
    except EvaluationError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
