#!/usr/bin/env python3

"""Evaluate page-level OCR predictions with CER and WER."""

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
    levenshtein_distance,
    load_json,
    safe_ratio,
    validate_document,
    validate_prediction_metadata,
    write_report,
)


DEFAULT_GROUND_TRUTH = Path(__file__).resolve().parent / "corpus" / "ocr_tesseract" / "ground_truth.json"


def normalize_text(text: str, ignore_case: bool) -> str:
    normalized = unicodedata.normalize("NFKC", text)
    normalized = " ".join(normalized.split())
    return normalized.casefold() if ignore_case else normalized


def evaluate_ocr(
    ground_truth: dict[str, Any], predictions: dict[str, Any], ignore_case: bool = False
) -> dict[str, Any]:
    references = validate_document(ground_truth, "ocr_text", "ground truth")
    prediction_samples = validate_document(predictions, "ocr_text", "predictions")
    prediction_metadata = validate_prediction_metadata(ground_truth, predictions)
    aligned = align_predictions(references, prediction_samples)

    sample_reports = []
    totals = {
        "reference_chars": 0,
        "predicted_chars": 0,
        "char_edit_distance": 0,
        "reference_words": 0,
        "predicted_words": 0,
        "word_edit_distance": 0,
        "missing_predictions": 0,
    }

    for reference_sample, prediction_sample in aligned:
        reference_value = reference_sample.get("text")
        if not isinstance(reference_value, str):
            raise EvaluationError(f"ground-truth sample {reference_sample['id']!r} is missing text")
        if prediction_sample is None:
            prediction_value = ""
            totals["missing_predictions"] += 1
        else:
            prediction_value = prediction_sample.get("text")
            if not isinstance(prediction_value, str):
                raise EvaluationError(f"prediction sample {prediction_sample['id']!r} is missing text")

        reference_text = normalize_text(reference_value, ignore_case)
        prediction_text = normalize_text(prediction_value, ignore_case)
        reference_words = reference_text.split()
        prediction_words = prediction_text.split()
        char_distance = levenshtein_distance(reference_text, prediction_text)
        word_distance = levenshtein_distance(reference_words, prediction_words)

        sample_report = {
            "id": reference_sample["id"],
            "missing_prediction": prediction_sample is None,
            "reference_chars": len(reference_text),
            "predicted_chars": len(prediction_text),
            "char_edit_distance": char_distance,
            "cer": error_rate(char_distance, len(reference_text)),
            "reference_words": len(reference_words),
            "predicted_words": len(prediction_words),
            "word_edit_distance": word_distance,
            "wer": error_rate(word_distance, len(reference_words)),
        }
        sample_reports.append(sample_report)
        for field in (
            "reference_chars",
            "predicted_chars",
            "char_edit_distance",
            "reference_words",
            "predicted_words",
            "word_edit_distance",
        ):
            totals[field] += sample_report[field]

    return {
        "version": 1,
        "task": "ocr_text",
        "dataset": ground_truth.get("dataset"),
        "prediction_metadata": prediction_metadata,
        "config": {"unicode_normalization": "NFKC", "whitespace": "collapsed", "ignore_case": ignore_case},
        "summary": {
            "samples": len(references),
            **totals,
            "character_count_ratio": safe_ratio(totals["predicted_chars"], totals["reference_chars"]),
            "cer": error_rate(totals["char_edit_distance"], totals["reference_chars"]),
            "wer": error_rate(totals["word_edit_distance"], totals["reference_words"]),
        },
        "samples": sample_reports,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--predictions", required=True, type=Path, help="Prediction JSON file")
    parser.add_argument("--ground-truth", type=Path, default=DEFAULT_GROUND_TRUTH, help="Ground-truth JSON file")
    parser.add_argument("--output", type=Path, help="Write the metric report to this JSON file")
    parser.add_argument("--ignore-case", action="store_true", help="Case-fold text before calculating metrics")
    parser.add_argument("--quiet", action="store_true", help="Do not print the metric report to stdout")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        report = evaluate_ocr(load_json(args.ground_truth), load_json(args.predictions), args.ignore_case)
        write_report(report, args.output, args.quiet)
    except EvaluationError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
