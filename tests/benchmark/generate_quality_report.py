#!/usr/bin/env python3

"""Generate and validate Quality Report v1 from evaluator reports."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import sys
from copy import deepcopy
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


QUALITY_SCHEMA_URI = (
    "https://github.com/ChNanAn/technical-doc-parser/schemas/quality-report.v1.schema.json"
)
QUALITY_LAYERS = ("backend", "pipeline", "product")


class QualityReportError(ValueError):
    """Raised when a profile, evaluator report, or quality report is invalid."""


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise QualityReportError(f"failed to read JSON from {path}: {error}") from error
    if not isinstance(value, dict):
        raise QualityReportError(f"expected a JSON object in {path}")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as input_file:
            for chunk in iter(lambda: input_file.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise QualityReportError(f"failed to hash {path}: {error}") from error
    return digest.hexdigest()


def canonical_sha256(value: Any) -> str:
    encoded = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def resolve_pointer(document: Any, pointer: str, description: str) -> Any:
    if pointer == "":
        return document
    if not isinstance(pointer, str) or not pointer.startswith("/"):
        raise QualityReportError(f"{description} must be an RFC 6901 JSON Pointer")
    current = document
    for encoded_part in pointer[1:].split("/"):
        part = encoded_part.replace("~1", "/").replace("~0", "~")
        if isinstance(current, dict):
            if part not in current:
                raise QualityReportError(f"{description} does not exist: {pointer}")
            current = current[part]
        elif isinstance(current, list):
            try:
                index = int(part)
            except ValueError as error:
                raise QualityReportError(f"{description} has a non-numeric array index: {pointer}") from error
            if index < 0 or index >= len(current):
                raise QualityReportError(f"{description} has an out-of-range array index: {pointer}")
            current = current[index]
        else:
            raise QualityReportError(f"{description} traverses a scalar value: {pointer}")
    return current


def finite_number(value: Any, description: str) -> int | float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise QualityReportError(f"{description} must be a finite number")
    return value


def non_negative_integer(value: Any, description: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise QualityReportError(f"{description} must be a non-negative integer")
    return value


def parse_inputs(specifications: list[str]) -> dict[str, Path]:
    inputs: dict[str, Path] = {}
    for specification in specifications:
        key, separator, raw_path = specification.partition("=")
        if not separator or not key or not raw_path:
            raise QualityReportError("--input must use KEY=PATH")
        if key in inputs:
            raise QualityReportError(f"duplicate input key: {key}")
        inputs[key] = Path(raw_path)
    return inputs


def validate_evaluator_inputs(profile: dict[str, Any], reports: dict[str, dict[str, Any]]) -> None:
    expected_inputs = profile.get("inputs")
    if not isinstance(expected_inputs, dict) or not expected_inputs:
        raise QualityReportError("profile.inputs must be a non-empty object")
    missing = sorted(set(expected_inputs) - set(reports))
    extra = sorted(set(reports) - set(expected_inputs))
    if missing:
        raise QualityReportError(f"missing evaluator inputs: {missing}")
    if extra:
        raise QualityReportError(f"unexpected evaluator inputs: {extra}")
    for key, expectation in expected_inputs.items():
        if not isinstance(expectation, dict):
            raise QualityReportError(f"profile input {key!r} must be an object")
        report = reports[key]
        for field in ("task", "version", "dataset"):
            expected = expectation.get(field)
            if expected is not None and report.get(field) != expected:
                raise QualityReportError(
                    f"input {key!r} {field} must be {expected!r}, found {report.get(field)!r}"
                )


def extract_metric(
    layer: str,
    metric_profile: dict[str, Any],
    reports: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    if not isinstance(metric_profile, dict):
        raise QualityReportError(f"{layer} metric profile must be an object")
    name = metric_profile.get("name")
    input_key = metric_profile.get("input")
    if not isinstance(name, str) or not name:
        raise QualityReportError(f"{layer} metric has no name")
    if not isinstance(input_key, str) or input_key not in reports:
        raise QualityReportError(f"{layer}.{name} references unknown input {input_key!r}")
    source = reports[input_key]
    value_pointer = metric_profile.get("value")
    value = finite_number(
        resolve_pointer(source, value_pointer, f"{layer}.{name}.value"),
        f"{layer}.{name}.value",
    )
    unit = metric_profile.get("unit")
    direction = metric_profile.get("direction")
    if not isinstance(unit, str) or not unit:
        raise QualityReportError(f"{layer}.{name}.unit must be a non-empty string")
    if direction not in ("higher_is_better", "lower_is_better", "target"):
        raise QualityReportError(f"{layer}.{name}.direction is invalid")
    metric: dict[str, Any] = {
        "name": name,
        "value": value,
        "unit": unit,
        "direction": direction,
    }
    for field in ("sample_count", "numerator", "denominator"):
        pointer = metric_profile.get(field)
        if pointer is None:
            continue
        extracted = resolve_pointer(source, pointer, f"{layer}.{name}.{field}")
        if field == "sample_count":
            metric[field] = non_negative_integer(extracted, f"{layer}.{name}.{field}")
        else:
            metric[field] = finite_number(extracted, f"{layer}.{name}.{field}")
    threshold = metric_profile.get("threshold")
    if threshold is not None:
        if not isinstance(threshold, dict):
            raise QualityReportError(f"{layer}.{name}.threshold must be an object")
        if threshold.get("operator") not in ("gte", "lte", "eq"):
            raise QualityReportError(f"{layer}.{name}.threshold.operator is invalid")
        finite_number(threshold.get("value"), f"{layer}.{name}.threshold.value")
        metric["threshold"] = deepcopy(threshold)
    if "description" in metric_profile:
        if not isinstance(metric_profile["description"], str):
            raise QualityReportError(f"{layer}.{name}.description must be a string")
        metric["description"] = metric_profile["description"]
    if "details" in metric_profile:
        if not isinstance(metric_profile["details"], dict):
            raise QualityReportError(f"{layer}.{name}.details must be an object")
        metric["details"] = deepcopy(metric_profile["details"])
    return metric


def threshold_passes(metric: dict[str, Any]) -> bool:
    threshold = metric["threshold"]
    value = metric["value"]
    expected = threshold["value"]
    if threshold["operator"] == "gte":
        return value >= expected
    if threshold["operator"] == "lte":
        return value <= expected
    return math.isclose(value, expected, rel_tol=1e-12, abs_tol=1e-12)


def metric_threshold_failures(report: dict[str, Any]) -> tuple[int, list[str]]:
    evaluated = 0
    failures: list[str] = []
    for layer, metric_set in report.get("layers", {}).items():
        for metric in metric_set.get("metrics", []):
            if "threshold" not in metric:
                continue
            evaluated += 1
            if not threshold_passes(metric):
                failures.append(f"{layer}.{metric['name']}")
    return evaluated, failures


def expected_status(report: dict[str, Any], required_layers: list[str]) -> tuple[str, list[str], int, list[str]]:
    missing_layers = [layer for layer in required_layers if layer not in report.get("layers", {})]
    evaluated, failures = metric_threshold_failures(report)
    if failures:
        return "failed", missing_layers, evaluated, failures
    if missing_layers or evaluated == 0:
        return "incomplete", missing_layers, evaluated, failures
    return "passed", missing_layers, evaluated, failures


def artifact_uri(path: Path, output_path: Path) -> str:
    try:
        return Path(os.path.relpath(path.resolve(), output_path.parent.resolve())).as_posix()
    except OSError:
        return path.as_posix()


def build_quality_report(
    profile: dict[str, Any],
    profile_sha256: str,
    input_paths: dict[str, Path],
    reports: dict[str, dict[str, Any]],
    subject_version: str,
    git_revision: str,
    generated_at: str,
    corpus_manifest: Path,
    output_path: Path,
    report_id: str | None = None,
) -> dict[str, Any]:
    if profile.get("profile_version") != 1:
        raise QualityReportError("profile.profile_version must be 1")
    validate_evaluator_inputs(profile, reports)
    try:
        parsed_time = datetime.fromisoformat(generated_at.replace("Z", "+00:00"))
    except ValueError as error:
        raise QualityReportError("--generated-at must be an RFC 3339 timestamp") from error
    if parsed_time.tzinfo is None:
        raise QualityReportError("--generated-at must include a timezone")
    if not subject_version:
        raise QualityReportError("--subject-version must not be empty")
    if len(git_revision) < 7:
        raise QualityReportError("--git-revision must contain at least 7 characters")

    subject_profile = profile.get("subject")
    corpus_profile = profile.get("corpus")
    layer_profiles = profile.get("layers")
    if not isinstance(subject_profile, dict) or not isinstance(subject_profile.get("name"), str):
        raise QualityReportError("profile.subject.name must be a string")
    if not isinstance(corpus_profile, dict):
        raise QualityReportError("profile.corpus must be an object")
    if not isinstance(layer_profiles, dict) or not layer_profiles:
        raise QualityReportError("profile.layers must be a non-empty object")

    sample_count_source = corpus_profile.get("sample_count")
    if not isinstance(sample_count_source, dict):
        raise QualityReportError("profile.corpus.sample_count must identify an input and JSON Pointer")
    sample_input = sample_count_source.get("input")
    sample_pointer = sample_count_source.get("pointer")
    if sample_input not in reports:
        raise QualityReportError("profile.corpus.sample_count references an unknown input")
    sample_count = non_negative_integer(
        resolve_pointer(reports[sample_input], sample_pointer, "corpus.sample_count"),
        "corpus.sample_count",
    )

    layers: dict[str, Any] = {}
    for layer, metric_set_profile in layer_profiles.items():
        if layer not in QUALITY_LAYERS:
            raise QualityReportError(f"unknown quality layer: {layer}")
        if not isinstance(metric_set_profile, dict):
            raise QualityReportError(f"profile layer {layer!r} must be an object")
        metric_profiles = metric_set_profile.get("metrics")
        if not isinstance(metric_profiles, list):
            raise QualityReportError(f"profile layer {layer!r} must contain a metrics array")
        metric_set: dict[str, Any] = {
            "metrics": [extract_metric(layer, metric, reports) for metric in metric_profiles]
        }
        if "notes" in metric_set_profile:
            metric_set["notes"] = deepcopy(metric_set_profile["notes"])
        layers[layer] = metric_set

    report_id_prefix = profile.get("report_id_prefix")
    if not isinstance(report_id_prefix, str) or not report_id_prefix:
        raise QualityReportError("profile.report_id_prefix must be a non-empty string")
    effective_report_id = report_id or f"{report_id_prefix}_{git_revision[:12]}"
    configuration_id = subject_profile.get("configuration_id")
    if not isinstance(configuration_id, str) or not configuration_id:
        raise QualityReportError("profile.subject.configuration_id must be a non-empty string")
    required_layers = profile.get("required_layers", list(QUALITY_LAYERS))
    if (
        not isinstance(required_layers, list)
        or not required_layers
        or any(layer not in QUALITY_LAYERS for layer in required_layers)
        or len(set(required_layers)) != len(required_layers)
    ):
        raise QualityReportError("profile.required_layers must contain unique quality layer names")

    report: dict[str, Any] = {
        "$schema": QUALITY_SCHEMA_URI,
        "schema_version": 1,
        "report_id": effective_report_id,
        "generated_at": generated_at,
        "status": "incomplete",
        "subject": {
            "name": subject_profile["name"],
            "version": subject_version,
            "git_revision": git_revision,
            "configuration_id": configuration_id,
        },
        "corpus": {
            "name": corpus_profile.get("name"),
            "version": corpus_profile.get("version"),
            "split": corpus_profile.get("split"),
            "sample_count": sample_count,
            "manifest_sha256": sha256_file(corpus_manifest),
        },
        "layers": layers,
        "artifacts": [
            {
                "name": f"evaluator-{key}",
                "uri": artifact_uri(path, output_path),
                "media_type": "application/json",
                "sha256": sha256_file(path),
            }
            for key, path in sorted(input_paths.items())
        ],
        "metadata": {
            "quality_profile": profile.get("profile_id"),
            "quality_profile_sha256": profile_sha256,
            "required_layers": required_layers,
        },
        "extensions": {
            "evaluator_sources": {
                key: {
                    "version": report_value.get("version"),
                    "task": report_value.get("task"),
                    "dataset": report_value.get("dataset"),
                    "sha256": sha256_file(input_paths[key]),
                }
                for key, report_value in sorted(reports.items())
            }
        },
    }
    status, missing_layers, evaluated, failures = expected_status(report, required_layers)
    report["status"] = status
    report["metadata"]["missing_layers"] = missing_layers
    report["metadata"]["thresholds"] = {
        "evaluated": evaluated,
        "failed": failures,
    }
    return report


def validate_quality_report_semantics(report: dict[str, Any]) -> None:
    layers = report.get("layers")
    if not isinstance(layers, dict) or not layers:
        raise QualityReportError("quality report must contain at least one layer")
    for layer, metric_set in layers.items():
        if layer not in QUALITY_LAYERS or not isinstance(metric_set, dict):
            raise QualityReportError(f"invalid quality layer: {layer}")
        metrics = metric_set.get("metrics")
        if not isinstance(metrics, list):
            raise QualityReportError(f"{layer}.metrics must be an array")
        names: set[str] = set()
        for metric in metrics:
            if not isinstance(metric, dict):
                raise QualityReportError(f"{layer}.metrics contains a non-object value")
            name = metric.get("name")
            if not isinstance(name, str) or not name:
                raise QualityReportError(f"{layer}.metrics contains an invalid metric name")
            if name in names:
                raise QualityReportError(f"duplicate metric name: {layer}.{name}")
            names.add(name)
            value = finite_number(metric.get("value"), f"{layer}.{name}.value")
            direction = metric.get("direction")
            threshold = metric.get("threshold")
            if threshold is not None:
                if not isinstance(threshold, dict):
                    raise QualityReportError(f"{layer}.{name}.threshold must be an object")
                operator = threshold.get("operator")
                expected_operator = {
                    "higher_is_better": "gte",
                    "lower_is_better": "lte",
                    "target": "eq",
                }.get(direction)
                if operator != expected_operator:
                    raise QualityReportError(
                        f"{layer}.{name} direction {direction!r} requires threshold operator {expected_operator!r}"
                    )
                finite_number(threshold.get("value"), f"{layer}.{name}.threshold.value")
            if "sample_count" in metric:
                non_negative_integer(metric["sample_count"], f"{layer}.{name}.sample_count")
            has_numerator = "numerator" in metric
            has_denominator = "denominator" in metric
            if has_numerator != has_denominator:
                raise QualityReportError(f"{layer}.{name} must provide numerator and denominator together")
            if has_numerator:
                numerator = finite_number(metric["numerator"], f"{layer}.{name}.numerator")
                denominator = finite_number(metric["denominator"], f"{layer}.{name}.denominator")
                if denominator < 0:
                    raise QualityReportError(f"{layer}.{name}.denominator must be non-negative")
                expected_value = numerator / denominator if denominator else 0.0
                if denominator == 0 and numerator != 0:
                    raise QualityReportError(f"{layer}.{name} has a non-zero numerator with a zero denominator")
                if not math.isclose(value, expected_value, rel_tol=1e-12, abs_tol=1e-12):
                    raise QualityReportError(
                        f"{layer}.{name}.value does not equal numerator / denominator"
                    )

    required_layers = report.get("metadata", {}).get("required_layers", list(QUALITY_LAYERS))
    if not isinstance(required_layers, list):
        raise QualityReportError("metadata.required_layers must be an array")
    expected, missing_layers, evaluated, failures = expected_status(report, required_layers)
    if report.get("status") != expected:
        raise QualityReportError(f"report status must be {expected!r}, found {report.get('status')!r}")
    metadata = report.get("metadata", {})
    if metadata.get("missing_layers") != missing_layers:
        raise QualityReportError("metadata.missing_layers does not match the emitted layers")
    if metadata.get("thresholds") != {"evaluated": evaluated, "failed": failures}:
        raise QualityReportError("metadata.thresholds does not match metric threshold results")


def validate_quality_report_schema(report: dict[str, Any], schema: dict[str, Any]) -> None:
    try:
        import jsonschema
    except ModuleNotFoundError as error:
        raise QualityReportError(
            "jsonschema is required; install tests/contract/requirements.txt"
        ) from error
    try:
        jsonschema.Draft202012Validator.check_schema(schema)
        validator = jsonschema.Draft202012Validator(schema, format_checker=jsonschema.FormatChecker())
        errors = sorted(
            validator.iter_errors(report),
            key=lambda item: tuple(str(part) for part in item.absolute_path),
        )
    except jsonschema.SchemaError as error:
        raise QualityReportError(f"invalid Quality Report Schema: {error.message}") from error
    if errors:
        messages = []
        for error in errors:
            location = "$" + "".join(
                f"[{part}]" if isinstance(part, int) else f".{part}" for part in error.absolute_path
            )
            messages.append(f"{location}: {error.message}")
        raise QualityReportError("Quality Report Schema validation failed:\n" + "\n".join(messages))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--input", action="append", default=[], metavar="KEY=PATH")
    parser.add_argument("--subject-version", required=True)
    parser.add_argument("--git-revision", required=True)
    parser.add_argument("--corpus-manifest", required=True, type=Path)
    parser.add_argument("--schema", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--report-id")
    parser.add_argument("--generated-at")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        input_paths = parse_inputs(args.input)
        profile = load_json(args.profile)
        reports = {key: load_json(path) for key, path in input_paths.items()}
        generated_at = args.generated_at or (
            datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
        )
        report = build_quality_report(
            profile,
            sha256_file(args.profile),
            input_paths,
            reports,
            args.subject_version,
            args.git_revision,
            generated_at,
            args.corpus_manifest,
            args.output,
            args.report_id,
        )
        validate_quality_report_semantics(report)
        validate_quality_report_schema(report, load_json(args.schema))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(
            f"wrote {args.output}: status={report['status']} "
            f"metrics={sum(len(layer['metrics']) for layer in report['layers'].values())}"
        )
        return 0
    except QualityReportError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
