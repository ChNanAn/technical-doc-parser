#!/usr/bin/env python3

"""Unit tests for Quality Report generation and semantic validation."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from generate_quality_report import (
    QualityReportError,
    build_quality_report,
    canonical_sha256,
    load_json,
    validate_quality_report_schema,
    validate_quality_report_semantics,
)


ROOT = Path(__file__).resolve().parents[2]
PROFILE_PATH = Path(__file__).resolve().parent / "profiles" / "pipeline-quality-v1.json"
SCHEMA_PATH = ROOT / "schemas" / "quality-report.v1.schema.json"
EXAMPLE_PATH = ROOT / "schemas" / "examples" / "quality-report.v1.example.json"


def evaluator_report(text_completeness: float = 0.9) -> dict:
    return {
        "version": 2,
        "task": "pipeline_text_order",
        "dataset": "technical-doc-parser/quality-baseline-15",
        "summary": {
            "samples": 15,
            "reference_characters": 100,
            "aligned_characters": int(text_completeness * 100),
            "text_completeness": text_completeness,
            "anchors": 10,
            "matched_anchors": 9,
            "reading_order_anchor_recall": 0.9,
            "comparable_pairs": 10,
            "correct_pairs": 10,
            "reading_order_score": 1.0,
            "full_text_reference_samples": 10,
            "full_text_reference_coverage": 10 / 15,
            "full_text_reference_characters": 100,
            "full_text_predicted_characters": 100,
            "extra_characters": 10,
            "text_duplication_rate": 0.1,
            "full_text_char_edit_distance": 20,
            "full_text_cer": 0.2,
        },
    }


class QualityReportGeneratorTest(unittest.TestCase):
    def build(self, root: Path, text_completeness: float = 0.9) -> dict:
        profile = load_json(PROFILE_PATH)
        source_path = root / "pipeline_quality_report.json"
        source_path.write_text(json.dumps(evaluator_report(text_completeness)), encoding="utf-8")
        manifest_path = root / "ground_truth.json"
        manifest_path.write_text('{"version":2}\n', encoding="utf-8")
        output_path = root / "quality-report.v1.json"
        return build_quality_report(
            profile,
            canonical_sha256(profile),
            {"pipeline_text": source_path},
            {"pipeline_text": evaluator_report(text_completeness)},
            "0.1.0",
            "0123456789abcdef",
            "2026-07-27T04:00:00Z",
            manifest_path,
            output_path,
        )

    def test_builds_traceable_incomplete_pipeline_report(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self.build(Path(directory))
        self.assertEqual("incomplete", report["status"])
        self.assertEqual(["backend", "product"], report["metadata"]["missing_layers"])
        self.assertEqual({"evaluated": 4, "failed": []}, report["metadata"]["thresholds"])
        self.assertEqual(6, len(report["layers"]["pipeline"]["metrics"]))
        self.assertEqual(15, report["corpus"]["sample_count"])
        self.assertEqual(64, len(report["corpus"]["manifest_sha256"]))
        self.assertEqual(64, len(report["artifacts"][0]["sha256"]))
        validate_quality_report_semantics(report)

    def test_failed_threshold_takes_precedence_over_missing_layers(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self.build(Path(directory), text_completeness=0.5)
        self.assertEqual("failed", report["status"])
        self.assertEqual(["pipeline.text_completeness"], report["metadata"]["thresholds"]["failed"])
        validate_quality_report_semantics(report)

    def test_rejects_duplicate_metric_names(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self.build(Path(directory))
        report["layers"]["pipeline"]["metrics"].append(
            dict(report["layers"]["pipeline"]["metrics"][0])
        )
        with self.assertRaisesRegex(QualityReportError, "duplicate metric name"):
            validate_quality_report_semantics(report)

    def test_rejects_ratio_that_disagrees_with_counts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self.build(Path(directory))
        report["layers"]["pipeline"]["metrics"][0]["numerator"] = 1
        with self.assertRaisesRegex(QualityReportError, "numerator / denominator"):
            validate_quality_report_semantics(report)

    def test_generated_report_matches_public_schema_when_dependency_is_available(self) -> None:
        try:
            import jsonschema  # noqa: F401
        except ModuleNotFoundError:
            self.skipTest("jsonschema is not installed for this interpreter")
        with tempfile.TemporaryDirectory() as directory:
            report = self.build(Path(directory))
        validate_quality_report_schema(report, load_json(SCHEMA_PATH))

    def test_public_example_has_consistent_status_semantics(self) -> None:
        report = load_json(EXAMPLE_PATH)
        validate_quality_report_semantics(report)


if __name__ == "__main__":
    unittest.main()
