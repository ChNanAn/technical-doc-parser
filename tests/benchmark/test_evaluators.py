#!/usr/bin/env python3

"""Unit tests for the benchmark metric implementations."""

from __future__ import annotations

import json
import unittest
from pathlib import Path

from benchmark_common import EvaluationError, levenshtein_distance, maximum_iou_matching
from evaluate_layout import evaluate_layout
from evaluate_ocr import evaluate_ocr
from evaluate_pipeline import evaluate_pipeline
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


class PipelineEvaluatorTest(unittest.TestCase):
    def test_reports_completeness_anchor_recall_and_pairwise_order(self) -> None:
        ground_truth = {
            "task": "pipeline_text_order",
            "samples": [
                {
                    "id": "page",
                    "anchors": [{"id": "a", "text": "alpha"}, {"id": "b", "text": "bravo"}],
                    "reading_order": ["a", "b"],
                }
            ],
        }
        predictions = {
            "task": "pipeline_text_order",
            "samples": [{"id": "page", "blocks": [{"text": "bravo"}, {"text": "alpha"}]}],
        }
        report = evaluate_pipeline(ground_truth, predictions)
        self.assertEqual(1.0, report["summary"]["text_completeness"])
        self.assertEqual(1.0, report["summary"]["reading_order_anchor_recall"])
        self.assertEqual(0.0, report["summary"]["reading_order_score"])
        self.assertIsNone(report["summary"]["text_duplication_rate"])
        self.assertIsNone(report["summary"]["full_text_cer"])

    def test_reports_full_text_duplication_with_companion_cer(self) -> None:
        ground_truth = {
            "task": "pipeline_text_order",
            "samples": [
                {
                    "id": "page",
                    "anchors": [{"id": "a", "text": "alpha"}],
                    "reading_order": ["a"],
                    "reference_text": "alpha bravo",
                }
            ],
        }
        predictions = {
            "task": "pipeline_text_order",
            "samples": [{"id": "page", "blocks": [{"text": "alpha bravo bravo"}]}],
        }
        report = evaluate_pipeline(ground_truth, predictions)
        self.assertEqual(1, report["summary"]["full_text_reference_samples"])
        self.assertEqual(6, report["summary"]["extra_characters"])
        self.assertAlmostEqual(6 / 17, report["summary"]["text_duplication_rate"])
        self.assertAlmostEqual(6 / 11, report["summary"]["full_text_cer"])

        reordered = {
            "task": "pipeline_text_order",
            "samples": [{"id": "page", "blocks": [{"text": "bravo alpha"}]}],
        }
        report = evaluate_pipeline(ground_truth, reordered)
        self.assertEqual(0.0, report["summary"]["text_duplication_rate"])

    def test_missing_prediction_does_not_look_like_duplication(self) -> None:
        ground_truth = {
            "task": "pipeline_text_order",
            "samples": [
                {
                    "id": "page",
                    "anchors": [{"id": "a", "text": "alpha"}],
                    "reading_order": ["a"],
                    "reference_text": "alpha",
                }
            ],
        }
        report = evaluate_pipeline(ground_truth, {"task": "pipeline_text_order", "samples": []})
        self.assertEqual(0.0, report["summary"]["text_duplication_rate"])
        self.assertEqual(1.0, report["summary"]["full_text_cer"])

    def test_validates_all_committed_reviewed_spans(self) -> None:
        path = Path(__file__).resolve().parent / "corpus" / "pipeline_quality" / "ground_truth.json"
        ground_truth = json.loads(path.read_text(encoding="utf-8"))
        predictions = {
            "task": "pipeline_text_order",
            "dataset": ground_truth["dataset"],
            "samples": [
                {
                    "id": sample["id"],
                    "blocks": [{"text": anchor["text"]} for anchor in sample["anchors"]],
                }
                for sample in ground_truth["samples"]
            ],
        }
        report = evaluate_pipeline(ground_truth, predictions, reference_root=path.parent)
        self.assertEqual(15, report["summary"]["samples"])
        self.assertEqual(1.0, report["summary"]["text_completeness"])
        self.assertEqual(1.0, report["summary"]["reading_order_anchor_recall"])
        self.assertEqual(1.0, report["summary"]["reading_order_score"])
        self.assertEqual(11, report["summary"]["full_text_reference_samples"])


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

    def test_matches_cell_text_by_bbox(self) -> None:
        cells = [
            {"bbox": [0, 0, 50, 20], "text": "Alpha"},
            {"bbox": [50, 0, 100, 20], "text": "Bravo"},
        ]
        ground_truth = {
            "task": "table_structure",
            "samples": [
                {
                    "id": "table",
                    "objects": [{"label": "table", "bbox": [0, 0, 100, 20]}],
                    "cells": cells,
                }
            ],
        }
        predictions = {
            "task": "table_structure",
            "samples": [
                {
                    "id": "table",
                    "objects": ground_truth["samples"][0]["objects"],
                    "cells": cells,
                }
            ],
        }
        report = evaluate_table(ground_truth, predictions)
        self.assertEqual(2, report["summary"]["matched_cells"])
        self.assertEqual(1.0, report["summary"]["cell_coverage"])
        self.assertEqual(0.0, report["summary"]["table_text_cer"])

    def test_reports_incorrect_matched_cell_text(self) -> None:
        ground_truth = {
            "task": "table_structure",
            "samples": [
                {
                    "id": "table",
                    "objects": [{"label": "table", "bbox": [0, 0, 20, 20]}],
                    "cells": [{"bbox": [0, 0, 20, 20], "text": "cat"}],
                }
            ],
        }
        predictions = {
            "task": "table_structure",
            "samples": [
                {
                    "id": "table",
                    "objects": ground_truth["samples"][0]["objects"],
                    "cells": [{"bbox": [0, 0, 20, 20], "text": "cot"}],
                }
            ],
        }
        report = evaluate_table(ground_truth, predictions)
        self.assertEqual(1, report["summary"]["character_edit_distance"])
        self.assertAlmostEqual(1 / 3, report["summary"]["table_text_cer"])

    def test_scores_missing_cell_as_empty_text(self) -> None:
        ground_truth = {
            "task": "table_structure",
            "samples": [
                {
                    "id": "table",
                    "objects": [{"label": "table", "bbox": [0, 0, 60, 20]}],
                    "cells": [
                        {"bbox": [0, 0, 20, 20], "text": "one"},
                        {"bbox": [20, 0, 40, 20], "text": "two"},
                        {"bbox": [40, 0, 60, 20], "text": ""},
                    ],
                }
            ],
        }
        predictions = {
            "task": "table_structure",
            "samples": [
                {
                    "id": "table",
                    "objects": ground_truth["samples"][0]["objects"],
                    "cells": [{"bbox": [0, 0, 20, 20], "text": "one"}],
                }
            ],
        }
        report = evaluate_table(ground_truth, predictions)
        self.assertEqual(2, report["summary"]["unmatched_reference_cells"])
        self.assertEqual(1, report["summary"]["exact_text_cells"])
        self.assertEqual(3, report["summary"]["character_edit_distance"])
        self.assertEqual(0.5, report["summary"]["table_text_cer"])


if __name__ == "__main__":
    unittest.main()
