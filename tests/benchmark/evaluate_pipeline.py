#!/usr/bin/env python3

"""Evaluate sampled final-text completeness and reading order."""

from __future__ import annotations

import argparse
import sys
import unicodedata
from difflib import SequenceMatcher
from pathlib import Path
from typing import Any

from benchmark_common import (
    EvaluationError,
    align_predictions,
    load_json,
    safe_ratio,
    validate_document,
    validate_prediction_metadata,
    write_report,
)


DEFAULT_GROUND_TRUTH = Path(__file__).resolve().parent / "corpus" / "pipeline_quality" / "ground_truth.json"


def normalize_text(value: str) -> str:
    return " ".join(unicodedata.normalize("NFKC", value).casefold().split())


def _prediction_blocks(sample: dict[str, Any] | None) -> list[str]:
    if sample is None:
        return []
    blocks = sample.get("blocks")
    if not isinstance(blocks, list):
        raise EvaluationError(f"prediction sample {sample.get('id')!r} must contain a blocks array")
    result = []
    for index, block in enumerate(blocks):
        if not isinstance(block, dict) or not isinstance(block.get("text"), str):
            raise EvaluationError(f"prediction sample {sample.get('id')!r} block {index} is missing text")
        result.append(normalize_text(block["text"]))
    return result


def _anchors(sample: dict[str, Any]) -> tuple[list[dict[str, str]], list[str]]:
    anchors = sample.get("anchors")
    order = sample.get("reading_order")
    if not isinstance(anchors, list) or not isinstance(order, list):
        raise EvaluationError(f"ground-truth sample {sample.get('id')!r} must contain anchors and reading_order")
    parsed = []
    ids = set()
    for index, anchor in enumerate(anchors):
        if not isinstance(anchor, dict) or not isinstance(anchor.get("id"), str) or not isinstance(anchor.get("text"), str):
            raise EvaluationError(f"ground-truth sample {sample.get('id')!r} anchor {index} is invalid")
        if not anchor["id"] or not normalize_text(anchor["text"]):
            raise EvaluationError(f"ground-truth sample {sample.get('id')!r} anchor {index} is empty")
        if anchor["id"] in ids:
            raise EvaluationError(f"ground-truth sample {sample.get('id')!r} has duplicate anchor {anchor['id']!r}")
        ids.add(anchor["id"])
        parsed.append({"id": anchor["id"], "text": anchor["text"]})
    if any(not isinstance(anchor_id, str) or anchor_id not in ids for anchor_id in order) or len(order) != len(ids):
        raise EvaluationError(f"ground-truth sample {sample.get('id')!r} reading_order must list every anchor once")
    if len(set(order)) != len(order):
        raise EvaluationError(f"ground-truth sample {sample.get('id')!r} reading_order contains duplicates")
    return parsed, order


def _best_alignment(reference: str, blocks: list[str]) -> tuple[int, float]:
    best_characters = 0
    best_position = 0.0
    for block_index, block in enumerate(blocks):
        matcher = SequenceMatcher(None, reference, block, autojunk=False)
        matching = matcher.get_matching_blocks()
        matched_characters = sum(match.size for match in matching)
        if matched_characters > best_characters:
            largest = max(matching, key=lambda match: match.size)
            best_characters = matched_characters
            best_position = block_index + safe_ratio(largest.b, max(len(block), 1))
    return best_characters, best_position


def evaluate_pipeline(
    ground_truth: dict[str, Any], predictions: dict[str, Any], anchor_match_threshold: float = 0.8
) -> dict[str, Any]:
    if not 0.0 < anchor_match_threshold <= 1.0:
        raise EvaluationError("anchor match threshold must be in (0, 1]")
    references = validate_document(ground_truth, "pipeline_text_order", "ground truth")
    prediction_samples = validate_document(predictions, "pipeline_text_order", "predictions")
    metadata = validate_prediction_metadata(ground_truth, predictions)
    aligned = align_predictions(references, prediction_samples)

    total_reference_characters = 0
    total_aligned_characters = 0
    total_anchors = 0
    total_matched_anchors = 0
    total_pairs = 0
    total_correct_pairs = 0
    sample_reports = []

    for reference_sample, prediction_sample in aligned:
        blocks = _prediction_blocks(prediction_sample)
        anchors, reading_order = _anchors(reference_sample)
        alignments = {}
        anchor_reports = []
        for anchor in anchors:
            reference = normalize_text(anchor["text"])
            aligned_characters, position = _best_alignment(reference, blocks)
            completeness = safe_ratio(aligned_characters, len(reference))
            matched = completeness >= anchor_match_threshold
            alignments[anchor["id"]] = {"position": position, "matched": matched}
            total_reference_characters += len(reference)
            total_aligned_characters += aligned_characters
            total_anchors += 1
            total_matched_anchors += int(matched)
            anchor_reports.append(
                {
                    "id": anchor["id"],
                    "reference_characters": len(reference),
                    "aligned_characters": aligned_characters,
                    "completeness": completeness,
                    "matched_for_order": matched,
                }
            )

        comparable_pairs = 0
        correct_pairs = 0
        for left_index, left_id in enumerate(reading_order):
            for right_id in reading_order[left_index + 1 :]:
                left = alignments[left_id]
                right = alignments[right_id]
                if left["matched"] and right["matched"]:
                    comparable_pairs += 1
                    correct_pairs += int(left["position"] < right["position"])
        total_pairs += comparable_pairs
        total_correct_pairs += correct_pairs
        sample_reports.append(
            {
                "id": reference_sample["id"],
                "missing_prediction": prediction_sample is None,
                "text_completeness": safe_ratio(
                    sum(item["aligned_characters"] for item in anchor_reports),
                    sum(item["reference_characters"] for item in anchor_reports),
                ),
                "reading_order_anchor_recall": safe_ratio(
                    sum(int(item["matched_for_order"]) for item in anchor_reports), len(anchor_reports)
                ),
                "reading_order_score": safe_ratio(correct_pairs, comparable_pairs),
                "comparable_pairs": comparable_pairs,
                "correct_pairs": correct_pairs,
                "anchors": anchor_reports,
            }
        )

    return {
        "version": 1,
        "task": "pipeline_text_order",
        "dataset": ground_truth.get("dataset"),
        "prediction_metadata": metadata,
        "config": {
            "reference_scope": ground_truth.get("annotation", {}).get("reference_scope"),
            "unicode_normalization": "NFKC",
            "whitespace": "collapsed",
            "ignore_case": True,
            "anchor_match_threshold": anchor_match_threshold,
        },
        "summary": {
            "samples": len(references),
            "reference_characters": total_reference_characters,
            "aligned_characters": total_aligned_characters,
            "text_completeness": safe_ratio(total_aligned_characters, total_reference_characters),
            "anchors": total_anchors,
            "matched_anchors": total_matched_anchors,
            "reading_order_anchor_recall": safe_ratio(total_matched_anchors, total_anchors),
            "comparable_pairs": total_pairs,
            "correct_pairs": total_correct_pairs,
            "reading_order_score": safe_ratio(total_correct_pairs, total_pairs),
        },
        "samples": sample_reports,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--predictions", required=True, type=Path)
    parser.add_argument("--ground-truth", type=Path, default=DEFAULT_GROUND_TRUTH)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--anchor-match-threshold", type=float, default=0.8)
    parser.add_argument("--minimum-text-completeness", type=float)
    parser.add_argument("--minimum-reading-order-anchor-recall", type=float)
    parser.add_argument("--minimum-reading-order-score", type=float)
    parser.add_argument("--quiet", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        for name, value in (
            ("--minimum-text-completeness", args.minimum_text_completeness),
            ("--minimum-reading-order-anchor-recall", args.minimum_reading_order_anchor_recall),
            ("--minimum-reading-order-score", args.minimum_reading_order_score),
        ):
            if value is not None and not 0.0 <= value <= 1.0:
                raise EvaluationError(f"{name} must be in [0, 1]")
        report = evaluate_pipeline(
            load_json(args.ground_truth), load_json(args.predictions), args.anchor_match_threshold
        )
        write_report(report, args.output, args.quiet)
        if (
            args.minimum_text_completeness is not None
            and report["summary"]["text_completeness"] < args.minimum_text_completeness
        ):
            print("error: text completeness is below the regression floor", file=sys.stderr)
            return 1
        if (
            args.minimum_reading_order_anchor_recall is not None
            and report["summary"]["reading_order_anchor_recall"] < args.minimum_reading_order_anchor_recall
        ):
            print("error: reading order anchor recall is below the regression floor", file=sys.stderr)
            return 1
        if (
            args.minimum_reading_order_score is not None
            and report["summary"]["reading_order_score"] < args.minimum_reading_order_score
        ):
            print("error: reading order score is below the regression floor", file=sys.stderr)
            return 1
    except EvaluationError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
