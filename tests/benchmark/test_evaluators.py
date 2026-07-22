#!/usr/bin/env python3

"""Unit tests for the benchmark metric implementations."""

from __future__ import annotations

import unittest

from benchmark_common import EvaluationError, levenshtein_distance, maximum_iou_matching
from evaluate_layout import evaluate_layout
from evaluate_ocr import evaluate_ocr
from evaluate_table import evaluate_table


class BenchmarkCommonTest(unittest.TestCase):
    def test_levenshtein_distance(self) -> None:
        self.assertEqual(3, levenshtein_distance("kitten", "sitting"))

    def test_iou_matching_is_one_to_one(self) -> None:
        matches = maximum_iou_matching([(0.0, 0.0, 10.0, 10.0)], [(0.0, 0.0, 10.0, 10.0)] * 2, 0.5)
        self.assertEqual(1, len(matches))

    def test_iou_matching_finds_maximum_cardinality(self) -> None:
        references = [(0.0, 0.0, 10.0, 10.0), (6.0, 0.0, 16.0, 10.0)]
        predictions = [(3.0, 0.0, 13.0, 10.0), (0.0, 0.0, 10.0, 10.0)]
        self.assertEqual(2, len(maximum_iou_matching(references, predictions, 0.5)))


class OcrEvaluatorTest(unittest.TestCase):
    def test_reports_corpus_cer_and_wer(self) -> None:
        ground_truth = {
            "task": "ocr_text",
            "dataset": "test",
            "samples": [{"id": "page", "text": "One two"}],
        }
        predictions = {
            "task": "ocr_text",
            "samples": [{"id": "page", "text": "One too"}],
        }
        report = evaluate_ocr(ground_truth, predictions)
        self.assertEqual(1, report["summary"]["char_edit_distance"])
        self.assertAlmostEqual(1 / 7, report["summary"]["cer"])
        self.assertEqual(1, report["summary"]["word_edit_distance"])
        self.assertAlmostEqual(0.5, report["summary"]["wer"])

    def test_missing_prediction_is_scored_as_empty(self) -> None:
        ground_truth = {"task": "ocr_text", "samples": [{"id": "page", "text": "abc"}]}
        report = evaluate_ocr(ground_truth, {"task": "ocr_text", "samples": []})
        self.assertEqual(1, report["summary"]["missing_predictions"])
        self.assertEqual(1.0, report["summary"]["cer"])

    def test_mismatched_dataset_is_rejected(self) -> None:
        ground_truth = {
            "task": "ocr_text",
            "dataset": "expected",
            "samples": [{"id": "page", "text": "abc"}],
        }
        predictions = {"task": "ocr_text", "dataset": "other", "samples": []}
        with self.assertRaises(EvaluationError):
            evaluate_ocr(ground_truth, predictions)


class LayoutEvaluatorTest(unittest.TestCase):
    def test_class_aware_one_to_one_metrics(self) -> None:
        ground_truth = {
            "task": "layout",
            "samples": [
                {
                    "id": 1,
                    "objects": [
                        {"mapped_label": "text", "bbox": [0, 0, 10, 10]},
                        {"mapped_label": "text", "bbox": [20, 0, 30, 10]},
                    ],
                }
            ],
        }
        predictions = {
            "task": "layout",
            "samples": [
                {
                    "id": 1,
                    "objects": [
                        {"label": "text", "bbox": [0, 0, 10, 10]},
                        {"label": "text", "bbox": [40, 0, 50, 10]},
                    ],
                }
            ],
        }
        report = evaluate_layout(ground_truth, predictions)
        self.assertEqual(1, report["summary"]["true_positive"])
        self.assertEqual(1, report["summary"]["false_positive"])
        self.assertEqual(1, report["summary"]["false_negative"])
        self.assertAlmostEqual(0.5, report["summary"]["micro_f1"])

    def test_unknown_prediction_label_is_rejected(self) -> None:
        ground_truth = {
            "task": "layout",
            "samples": [{"id": 1, "objects": [{"mapped_label": "text", "bbox": [0, 0, 1, 1]}]}],
        }
        predictions = {
            "task": "layout",
            "samples": [{"id": 1, "objects": [{"label": "typo", "bbox": [0, 0, 1, 1]}]}],
        }
        with self.assertRaises(EvaluationError):
            evaluate_layout(ground_truth, predictions)


class TableEvaluatorTest(unittest.TestCase):
    def test_reports_exact_structure_match_rate(self) -> None:
        ground_truth = {
            "task": "table_structure",
            "samples": [
                {
                    "id": "table",
                    "objects": [
                        {"label": "table", "bbox": [0, 0, 100, 100]},
                        {"label": "table row", "bbox": [0, 0, 100, 50]},
                    ],
                }
            ],
        }
        perfect = {
            "task": "table_structure",
            "samples": [{"id": "table", "objects": ground_truth["samples"][0]["objects"]}],
        }
        report = evaluate_table(ground_truth, perfect)
        self.assertEqual(1.0, report["summary"]["exact_match_rate"])
        self.assertEqual(1.0, report["summary"]["micro_f1"])

        missing_row = {
            "task": "table_structure",
            "samples": [{"id": "table", "objects": [{"label": "table", "bbox": [0, 0, 100, 100]}]}],
        }
        report = evaluate_table(ground_truth, missing_row)
        self.assertEqual(0.0, report["summary"]["exact_match_rate"])
        self.assertEqual(1, report["summary"]["false_negative"])


if __name__ == "__main__":
    unittest.main()
