#!/usr/bin/env python3

"""Evaluate final-text completeness, duplication, and reading order."""

from __future__ import annotations

import argparse
import hashlib
import sys
import unicodedata
from collections import Counter
from difflib import SequenceMatcher
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


def _full_text_reference(sample: dict[str, Any], reference_root: Path | None) -> str | None:
    inline_reference = sample.get("reference_text")
    reference_file = sample.get("full_text_reference")
    if inline_reference is not None and reference_file is not None:
        raise EvaluationError(f"ground-truth sample {sample.get('id')!r} has two full-text references")
    if inline_reference is not None:
        if not isinstance(inline_reference, str) or not normalize_text(inline_reference):
            raise EvaluationError(f"ground-truth sample {sample.get('id')!r} has invalid reference_text")
        return normalize_text(inline_reference)
    if reference_file is None:
        return None
    if not isinstance(reference_file, str) or not reference_file:
        raise EvaluationError(f"ground-truth sample {sample.get('id')!r} has invalid full_text_reference")
    expected_sha256 = sample.get("full_text_reference_sha256")
    if not isinstance(expected_sha256, str) or len(expected_sha256) != 64:
        raise EvaluationError(f"ground-truth sample {sample.get('id')!r} is missing a full-text reference SHA256")
    relative_path = Path(reference_file)
    if reference_root is None:
        raise EvaluationError("reference_root is required for file-backed full-text references")
    if relative_path.is_absolute() or ".." in relative_path.parts:
        raise EvaluationError(f"ground-truth sample {sample.get('id')!r} has an unsafe full-text reference path")
    try:
        reference_bytes = (reference_root / relative_path).read_bytes()
        reference = reference_bytes.decode("utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise EvaluationError(
            f"failed to read full-text reference for sample {sample.get('id')!r}: {error}"
        ) from error
    if hashlib.sha256(reference_bytes).hexdigest() != expected_sha256:
        raise EvaluationError(f"full-text reference SHA256 mismatch for sample {sample.get('id')!r}")
    if not normalize_text(reference):
        raise EvaluationError(f"ground-truth sample {sample.get('id')!r} has an empty full-text reference")
    return normalize_text(reference)


def _full_text_metrics(reference: str, prediction: str) -> dict[str, int | float]:
    reference_counts = Counter(reference)
    prediction_counts = Counter(prediction)
    extra_characters = sum(
        max(predicted_count - reference_counts[character], 0)
        for character, predicted_count in prediction_counts.items()
    )
    edit_distance = levenshtein_distance(reference, prediction)
    return {
        "reference_characters": len(reference),
        "predicted_characters": len(prediction),
        "extra_characters": extra_characters,
        "text_duplication_rate": safe_ratio(extra_characters, len(prediction)),
        "char_edit_distance": edit_distance,
        "full_text_cer": error_rate(edit_distance, len(reference)),
    }


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
    ground_truth: dict[str, Any],
    predictions: dict[str, Any],
    anchor_match_threshold: float = 0.8,
    reference_root: Path | None = None,
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
    full_text_reference_samples = 0
    total_full_text_reference_characters = 0
    total_full_text_predicted_characters = 0
    total_extra_characters = 0
    total_full_text_edit_distance = 0
    sample_reports = []

    for reference_sample, prediction_sample in aligned:
        blocks = _prediction_blocks(prediction_sample)
        prediction_text = " ".join(block for block in blocks if block)
        anchors, reading_order = _anchors(reference_sample)
        full_text_reference = _full_text_reference(reference_sample, reference_root)
        full_text_metrics = (
            _full_text_metrics(full_text_reference, prediction_text) if full_text_reference is not None else None
        )
        if full_text_metrics is not None:
            full_text_reference_samples += 1
            total_full_text_reference_characters += int(full_text_metrics["reference_characters"])
            total_full_text_predicted_characters += int(full_text_metrics["predicted_characters"])
            total_extra_characters += int(full_text_metrics["extra_characters"])
            total_full_text_edit_distance += int(full_text_metrics["char_edit_distance"])
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
                "full_text_reference_available": full_text_metrics is not None,
                "text_duplication_rate": (
                    full_text_metrics["text_duplication_rate"] if full_text_metrics is not None else None
                ),
                "full_text_cer": full_text_metrics["full_text_cer"] if full_text_metrics is not None else None,
                "full_text": full_text_metrics,
                "comparable_pairs": comparable_pairs,
                "correct_pairs": correct_pairs,
                "anchors": anchor_reports,
            }
        )

    return {
        "version": 2,
        "task": "pipeline_text_order",
        "dataset": ground_truth.get("dataset"),
        "prediction_metadata": metadata,
        "config": {
            "reference_scope": ground_truth.get("annotation", {}).get("reference_scope"),
            "unicode_normalization": "NFKC",
            "whitespace": "collapsed",
            "ignore_case": True,
            "anchor_match_threshold": anchor_match_threshold,
            "text_duplication_matching": "normalized_character_multiset",
            "full_text_unavailable_policy": "exclude_and_report_coverage",
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
            "full_text_reference_samples": full_text_reference_samples,
            "full_text_reference_coverage": safe_ratio(full_text_reference_samples, len(references)),
            "full_text_reference_characters": total_full_text_reference_characters,
            "full_text_predicted_characters": total_full_text_predicted_characters,
            "extra_characters": total_extra_characters,
            "text_duplication_rate": (
                safe_ratio(total_extra_characters, total_full_text_predicted_characters)
                if full_text_reference_samples > 0
                else None
            ),
            "full_text_char_edit_distance": total_full_text_edit_distance,
            "full_text_cer": (
                error_rate(total_full_text_edit_distance, total_full_text_reference_characters)
                if full_text_reference_samples > 0
                else None
            ),
        },
        "samples": sample_reports,
    }


def _json_pointer(document: Any, pointer: str) -> Any:
    if not isinstance(pointer, str) or not pointer.startswith("/"):
        raise EvaluationError(f"quality profile contains an invalid JSON Pointer: {pointer!r}")
    value = document
    for encoded_part in pointer[1:].split("/"):
        part = encoded_part.replace("~1", "/").replace("~0", "~")
        if isinstance(value, dict) and part in value:
            value = value[part]
        elif isinstance(value, list) and part.isdigit() and int(part) < len(value):
            value = value[int(part)]
        else:
            raise EvaluationError(f"quality profile JSON Pointer does not resolve: {pointer}")
    return value


def quality_profile_threshold_failures(
    profile: dict[str, Any],
    input_name: str,
    report: dict[str, Any],
) -> list[str]:
    if profile.get("profile_version") != 1:
        raise EvaluationError("quality profile version must be 1")
    inputs = profile.get("inputs")
    if not isinstance(inputs, dict) or not isinstance(inputs.get(input_name), dict):
        raise EvaluationError(f"quality profile does not define input {input_name!r}")
    expected_input = inputs[input_name]
    for field in ("version", "task", "dataset"):
        expected = expected_input.get(field)
        if expected is not None and report.get(field) != expected:
            raise EvaluationError(
                f"quality profile input {input_name!r} expects {field}={expected!r}, "
                f"got {report.get(field)!r}"
            )

    layers = profile.get("layers")
    if not isinstance(layers, dict):
        raise EvaluationError("quality profile layers must be an object")
    evaluated = 0
    failures = []
    seen_metrics = set()
    for layer_name, metric_set in layers.items():
        metrics = metric_set.get("metrics") if isinstance(metric_set, dict) else None
        if not isinstance(metrics, list):
            raise EvaluationError(f"quality profile layer {layer_name!r} must contain a metrics array")
        for metric in metrics:
            if not isinstance(metric, dict) or metric.get("input") != input_name:
                continue
            metric_name = metric.get("name")
            if not isinstance(metric_name, str) or not metric_name:
                raise EvaluationError(f"quality profile layer {layer_name!r} contains an invalid metric name")
            metric_id = f"{layer_name}.{metric_name}"
            if metric_id in seen_metrics:
                raise EvaluationError(f"quality profile contains duplicate metric {metric_id!r}")
            seen_metrics.add(metric_id)
            threshold = metric.get("threshold")
            if threshold is None:
                continue
            if not isinstance(threshold, dict):
                raise EvaluationError(f"quality profile threshold for {metric_id} must be an object")
            operator = threshold.get("operator")
            expected = threshold.get("value")
            if operator not in {"gte", "lte", "eq"}:
                raise EvaluationError(f"quality profile threshold for {metric_id} has an invalid operator")
            if not isinstance(expected, (int, float)) or isinstance(expected, bool):
                raise EvaluationError(f"quality profile threshold for {metric_id} must be numeric")
            actual = _json_pointer(report, metric.get("value"))
            if not isinstance(actual, (int, float)) or isinstance(actual, bool):
                raise EvaluationError(f"quality profile value for {metric_id} must resolve to a number")
            evaluated += 1
            passed = (
                (operator == "gte" and actual >= expected)
                or (operator == "lte" and actual <= expected)
                or (operator == "eq" and actual == expected)
            )
            if not passed:
                failures.append(metric_id)
    if evaluated == 0:
        raise EvaluationError(f"quality profile defines no thresholds for input {input_name!r}")
    return failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--predictions", required=True, type=Path)
    parser.add_argument("--ground-truth", type=Path, default=DEFAULT_GROUND_TRUTH)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--anchor-match-threshold", type=float, default=0.8)
    parser.add_argument("--minimum-text-completeness", type=float)
    parser.add_argument("--minimum-reading-order-anchor-recall", type=float)
    parser.add_argument("--minimum-reading-order-score", type=float)
    parser.add_argument("--maximum-text-duplication-rate", type=float)
    parser.add_argument("--quality-profile", type=Path)
    parser.add_argument("--quality-profile-input", default="pipeline_text")
    parser.add_argument("--quiet", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        explicit_thresholds = (
            ("--minimum-text-completeness", args.minimum_text_completeness),
            ("--minimum-reading-order-anchor-recall", args.minimum_reading_order_anchor_recall),
            ("--minimum-reading-order-score", args.minimum_reading_order_score),
            ("--maximum-text-duplication-rate", args.maximum_text_duplication_rate),
        )
        if args.quality_profile is not None and any(value is not None for _, value in explicit_thresholds):
            raise EvaluationError("quality profile cannot be combined with explicit threshold arguments")
        for name, value in explicit_thresholds:
            if value is not None and not 0.0 <= value <= 1.0:
                raise EvaluationError(f"{name} must be in [0, 1]")
        report = evaluate_pipeline(
            load_json(args.ground_truth),
            load_json(args.predictions),
            args.anchor_match_threshold,
            args.ground_truth.parent,
        )
        write_report(report, args.output, args.quiet)
        if args.quality_profile is not None:
            failures = quality_profile_threshold_failures(
                load_json(args.quality_profile),
                args.quality_profile_input,
                report,
            )
            for metric_id in failures:
                print(f"error: {metric_id} failed its quality profile threshold", file=sys.stderr)
            return 1 if failures else 0
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
        if args.maximum_text_duplication_rate is not None:
            duplication_rate = report["summary"]["text_duplication_rate"]
            if duplication_rate is None:
                raise EvaluationError("text duplication ceiling requires at least one full-text reference")
            if duplication_rate > args.maximum_text_duplication_rate:
                print("error: text duplication rate is above the regression ceiling", file=sys.stderr)
                return 1
    except EvaluationError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
