#!/usr/bin/env python3

"""Shared validation and metric helpers for the benchmark evaluators."""

from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any, Iterable, Sequence


class EvaluationError(ValueError):
    """Raised when a benchmark or prediction file violates the protocol."""


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise EvaluationError(f"failed to read JSON from {path}: {error}") from error
    if not isinstance(value, dict):
        raise EvaluationError(f"expected a JSON object in {path}")
    return value


def write_report(report: dict[str, Any], output: Path | None, quiet: bool) -> None:
    serialized = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(serialized, encoding="utf-8")
    if not quiet:
        print(serialized, end="")


def validate_document(document: dict[str, Any], expected_task: str, description: str) -> list[dict[str, Any]]:
    task = document.get("task")
    if task is not None and task != expected_task:
        raise EvaluationError(f"{description} task must be {expected_task!r}, found {task!r}")
    samples = document.get("samples")
    if not isinstance(samples, list):
        raise EvaluationError(f"{description} must contain a samples array")
    for index, sample in enumerate(samples):
        if not isinstance(sample, dict) or "id" not in sample:
            raise EvaluationError(f"{description} sample {index} must be an object with an id")
    return samples


def validate_prediction_metadata(
    ground_truth: dict[str, Any], predictions: dict[str, Any]
) -> dict[str, Any]:
    prediction_dataset = predictions.get("dataset")
    if prediction_dataset is not None and prediction_dataset != ground_truth.get("dataset"):
        raise EvaluationError(
            f"prediction dataset must be {ground_truth.get('dataset')!r}, found {prediction_dataset!r}"
        )
    metadata = predictions.get("metadata", {})
    if not isinstance(metadata, dict):
        raise EvaluationError("prediction metadata must be an object")
    return metadata


def sample_key(sample_id: Any) -> str:
    try:
        return json.dumps(sample_id, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    except TypeError as error:
        raise EvaluationError(f"sample id is not JSON-serializable: {sample_id!r}") from error


def index_samples(samples: Iterable[dict[str, Any]], description: str) -> dict[str, dict[str, Any]]:
    indexed: dict[str, dict[str, Any]] = {}
    for sample in samples:
        key = sample_key(sample["id"])
        if key in indexed:
            raise EvaluationError(f"duplicate sample id in {description}: {sample['id']!r}")
        indexed[key] = sample
    return indexed


def align_predictions(
    references: list[dict[str, Any]], predictions: list[dict[str, Any]]
) -> list[tuple[dict[str, Any], dict[str, Any] | None]]:
    reference_index = index_samples(references, "ground truth")
    prediction_index = index_samples(predictions, "predictions")
    unknown = [sample["id"] for key, sample in prediction_index.items() if key not in reference_index]
    if unknown:
        raise EvaluationError(f"predictions contain unknown sample ids: {unknown}")
    return [(sample, prediction_index.get(sample_key(sample["id"]))) for sample in references]


def levenshtein_distance(reference: Sequence[Any], prediction: Sequence[Any]) -> int:
    """Compute Levenshtein distance with O(min(n, m)) memory."""

    if reference == prediction:
        return 0
    if not reference:
        return len(prediction)
    if not prediction:
        return len(reference)
    if len(reference) < len(prediction):
        reference, prediction = prediction, reference
    previous = list(range(len(prediction) + 1))
    for reference_index, reference_value in enumerate(reference, start=1):
        current = [reference_index]
        for prediction_index, prediction_value in enumerate(prediction, start=1):
            current.append(
                min(
                    previous[prediction_index] + 1,
                    current[prediction_index - 1] + 1,
                    previous[prediction_index - 1] + (reference_value != prediction_value),
                )
            )
        previous = current
    return previous[-1]


def error_rate(distance: int, reference_size: int) -> float:
    if reference_size == 0:
        return 0.0 if distance == 0 else 1.0
    return distance / reference_size


def safe_ratio(numerator: int | float, denominator: int | float) -> float:
    return 0.0 if denominator == 0 else numerator / denominator


def f1_score(precision: float, recall: float) -> float:
    return 0.0 if precision + recall == 0.0 else 2.0 * precision * recall / (precision + recall)


def validate_bbox(value: Any, description: str) -> tuple[float, float, float, float]:
    if not isinstance(value, list) or len(value) != 4:
        raise EvaluationError(f"{description} bbox must contain four coordinates")
    if any(isinstance(coordinate, bool) or not isinstance(coordinate, (int, float)) for coordinate in value):
        raise EvaluationError(f"{description} bbox coordinates must be numbers")
    x0, y0, x1, y1 = (float(coordinate) for coordinate in value)
    if not all(math.isfinite(coordinate) for coordinate in (x0, y0, x1, y1)):
        raise EvaluationError(f"{description} bbox coordinates must be finite")
    if x1 < x0 or y1 < y0:
        raise EvaluationError(f"{description} bbox has reversed coordinates: {value}")
    return x0, y0, x1, y1


def intersection_over_union(
    lhs: tuple[float, float, float, float], rhs: tuple[float, float, float, float]
) -> float:
    intersection_width = max(0.0, min(lhs[2], rhs[2]) - max(lhs[0], rhs[0]))
    intersection_height = max(0.0, min(lhs[3], rhs[3]) - max(lhs[1], rhs[1]))
    intersection = intersection_width * intersection_height
    lhs_area = max(0.0, lhs[2] - lhs[0]) * max(0.0, lhs[3] - lhs[1])
    rhs_area = max(0.0, rhs[2] - rhs[0]) * max(0.0, rhs[3] - rhs[1])
    union = lhs_area + rhs_area - intersection
    return 0.0 if union <= 0.0 else intersection / union


def maximum_iou_matching(
    references: list[tuple[float, float, float, float]],
    predictions: list[tuple[float, float, float, float]],
    threshold: float,
) -> list[tuple[int, int, float]]:
    """Return a maximum-cardinality one-to-one matching above an IoU threshold."""

    adjacency: list[list[tuple[int, float]]] = []
    for prediction in predictions:
        candidates = [
            (reference_index, intersection_over_union(reference, prediction))
            for reference_index, reference in enumerate(references)
        ]
        adjacency.append(
            sorted(
                (candidate for candidate in candidates if candidate[1] >= threshold),
                key=lambda candidate: (-candidate[1], candidate[0]),
            )
        )

    matched_prediction_for_reference = [-1] * len(references)

    def augment(prediction_index: int, seen_references: set[int]) -> bool:
        for reference_index, _ in adjacency[prediction_index]:
            if reference_index in seen_references:
                continue
            seen_references.add(reference_index)
            previous_prediction = matched_prediction_for_reference[reference_index]
            if previous_prediction == -1 or augment(previous_prediction, seen_references):
                matched_prediction_for_reference[reference_index] = prediction_index
                return True
        return False

    for prediction_index in range(len(predictions)):
        augment(prediction_index, set())

    matches = []
    for reference_index, prediction_index in enumerate(matched_prediction_for_reference):
        if prediction_index != -1:
            matches.append(
                (
                    reference_index,
                    prediction_index,
                    intersection_over_union(references[reference_index], predictions[prediction_index]),
                )
            )
    return matches


def _objects_by_label(
    sample: dict[str, Any], label_fields: tuple[str, ...], description: str
) -> dict[str, list[tuple[float, float, float, float]]]:
    objects = sample.get("objects", [])
    if not isinstance(objects, list):
        raise EvaluationError(f"{description} objects must be an array")
    result: dict[str, list[tuple[float, float, float, float]]] = {}
    for object_index, item in enumerate(objects):
        if not isinstance(item, dict):
            raise EvaluationError(f"{description} object {object_index} must be an object")
        label = next((item.get(field) for field in label_fields if isinstance(item.get(field), str)), None)
        if label is None or not label:
            raise EvaluationError(f"{description} object {object_index} is missing a label")
        bbox = validate_bbox(item.get("bbox"), f"{description} object {object_index}")
        result.setdefault(label, []).append(bbox)
    return result


def evaluate_object_detection(
    reference_document: dict[str, Any],
    prediction_document: dict[str, Any],
    task: str,
    reference_label_fields: tuple[str, ...],
    prediction_label_fields: tuple[str, ...],
    iou_threshold: float,
) -> dict[str, Any]:
    if not 0.0 < iou_threshold <= 1.0:
        raise EvaluationError("IoU threshold must be in the interval (0, 1]")

    references = validate_document(reference_document, task, "ground truth")
    predictions = validate_document(prediction_document, task, "predictions")
    prediction_metadata = validate_prediction_metadata(reference_document, prediction_document)
    aligned = align_predictions(references, predictions)

    reference_labels: set[str] = set()
    parsed_references: dict[str, dict[str, list[tuple[float, float, float, float]]]] = {}
    for sample in references:
        key = sample_key(sample["id"])
        parsed = _objects_by_label(sample, reference_label_fields, f"ground-truth sample {sample['id']!r}")
        parsed_references[key] = parsed
        reference_labels.update(parsed)

    aggregate = {
        label: {"reference": 0, "predicted": 0, "true_positive": 0, "matched_iou_sum": 0.0}
        for label in sorted(reference_labels)
    }
    sample_reports = []
    missing_predictions = 0

    for reference_sample, prediction_sample in aligned:
        key = sample_key(reference_sample["id"])
        reference_objects = parsed_references[key]
        if prediction_sample is None:
            prediction_objects: dict[str, list[tuple[float, float, float, float]]] = {}
            missing_predictions += 1
        else:
            prediction_objects = _objects_by_label(
                prediction_sample,
                prediction_label_fields,
                f"prediction sample {prediction_sample['id']!r}",
            )
        unknown_labels = sorted(set(prediction_objects) - reference_labels)
        if unknown_labels:
            raise EvaluationError(
                f"prediction sample {reference_sample['id']!r} contains unknown labels: {unknown_labels}"
            )

        sample_reference = 0
        sample_predicted = 0
        sample_true_positive = 0
        sample_iou_sum = 0.0
        for label in sorted(reference_labels):
            reference_boxes = reference_objects.get(label, [])
            prediction_boxes = prediction_objects.get(label, [])
            matches = maximum_iou_matching(reference_boxes, prediction_boxes, iou_threshold)
            aggregate[label]["reference"] += len(reference_boxes)
            aggregate[label]["predicted"] += len(prediction_boxes)
            aggregate[label]["true_positive"] += len(matches)
            aggregate[label]["matched_iou_sum"] += sum(match[2] for match in matches)
            sample_reference += len(reference_boxes)
            sample_predicted += len(prediction_boxes)
            sample_true_positive += len(matches)
            sample_iou_sum += sum(match[2] for match in matches)

        sample_false_positive = sample_predicted - sample_true_positive
        sample_false_negative = sample_reference - sample_true_positive
        sample_precision = safe_ratio(sample_true_positive, sample_predicted)
        sample_recall = safe_ratio(sample_true_positive, sample_reference)
        sample_reports.append(
            {
                "id": reference_sample["id"],
                "missing_prediction": prediction_sample is None,
                "reference_objects": sample_reference,
                "predicted_objects": sample_predicted,
                "true_positive": sample_true_positive,
                "false_positive": sample_false_positive,
                "false_negative": sample_false_negative,
                "precision": sample_precision,
                "recall": sample_recall,
                "f1": f1_score(sample_precision, sample_recall),
                "mean_matched_iou": safe_ratio(sample_iou_sum, sample_true_positive),
                "exact_match": sample_false_positive == 0 and sample_false_negative == 0,
            }
        )

    per_class = {}
    total_reference = 0
    total_predicted = 0
    total_true_positive = 0
    total_iou_sum = 0.0
    for label, counts in aggregate.items():
        false_positive = counts["predicted"] - counts["true_positive"]
        false_negative = counts["reference"] - counts["true_positive"]
        precision = safe_ratio(counts["true_positive"], counts["predicted"])
        recall = safe_ratio(counts["true_positive"], counts["reference"])
        per_class[label] = {
            "reference": counts["reference"],
            "predicted": counts["predicted"],
            "true_positive": counts["true_positive"],
            "false_positive": false_positive,
            "false_negative": false_negative,
            "precision": precision,
            "recall": recall,
            "f1": f1_score(precision, recall),
            "mean_matched_iou": safe_ratio(counts["matched_iou_sum"], counts["true_positive"]),
        }
        total_reference += counts["reference"]
        total_predicted += counts["predicted"]
        total_true_positive += counts["true_positive"]
        total_iou_sum += counts["matched_iou_sum"]

    micro_precision = safe_ratio(total_true_positive, total_predicted)
    micro_recall = safe_ratio(total_true_positive, total_reference)
    return {
        "version": 1,
        "task": task,
        "dataset": reference_document.get("dataset"),
        "prediction_metadata": prediction_metadata,
        "config": {"iou_threshold": iou_threshold, "matching": "maximum_cardinality_per_class"},
        "summary": {
            "samples": len(references),
            "missing_predictions": missing_predictions,
            "reference_objects": total_reference,
            "predicted_objects": total_predicted,
            "true_positive": total_true_positive,
            "false_positive": total_predicted - total_true_positive,
            "false_negative": total_reference - total_true_positive,
            "micro_precision": micro_precision,
            "micro_recall": micro_recall,
            "micro_f1": f1_score(micro_precision, micro_recall),
            "macro_f1": safe_ratio(sum(item["f1"] for item in per_class.values()), len(per_class)),
            "mean_matched_iou": safe_ratio(total_iou_sum, total_true_positive),
            "exact_match_rate": safe_ratio(sum(item["exact_match"] for item in sample_reports), len(sample_reports)),
        },
        "per_class": per_class,
        "samples": sample_reports,
    }
