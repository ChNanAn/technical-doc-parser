#!/usr/bin/env python3

"""Verify that the platform worker resolves the shared engine environment adapter."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from pathlib import Path
from typing import Any


def assert_equal(actual: Any, expected: Any, field: str) -> None:
    if actual != expected:
        raise AssertionError(f"{field}: expected {expected!r}, found {actual!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", required=True, type=Path)
    args = parser.parse_args()

    environment = os.environ.copy()
    environment.update(
        {
            "DOCUMENT_INTELLIGENCE_ENGINE_TESSERACT_CMD": "/worker/bin/tesseract",
            "DOCUMENT_INTELLIGENCE_ENGINE_TESSERACT_LANG": "eng+chi_sim",
            "DOCUMENT_INTELLIGENCE_ENGINE_BACKEND_CONFIG": "/worker/backends.json",
            "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_MODEL_DIR": "/worker/models/paddleocr",
            "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DET_MODEL": "/worker/overrides/det.onnx",
            "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_PROFILE": "ppocrv4_mobile",
            "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_REC_BATCH_SIZE": "17",
            "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_REC_MAX_WIDTH": "1536",
            "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DET_LIMIT_SIDE": "1280",
            "DOCUMENT_INTELLIGENCE_ENGINE_DOCLAYNET_MODEL": "/worker/models/doclaynet.onnx",
            "DOCUMENT_INTELLIGENCE_ENGINE_DOCLAYNET_CONFIDENCE": "0.71",
            "DOCUMENT_INTELLIGENCE_ENGINE_PADDLE_LAYOUT_MODEL": "/worker/models/paddle-layout.onnx",
            "DOCUMENT_INTELLIGENCE_ENGINE_PADDLE_LAYOUT_CONFIDENCE": "0.62",
            "DOCUMENT_INTELLIGENCE_ENGINE_TABLE_DETECTION_MODEL": "/worker/models/table-detection.onnx",
            "DOCUMENT_INTELLIGENCE_ENGINE_TABLE_STRUCTURE_MODEL": "/worker/models/table-structure.onnx",
            "DOCUMENT_INTELLIGENCE_ENGINE_TABLE_DETECTION_CONFIDENCE": "0.83",
            "DOCUMENT_INTELLIGENCE_ENGINE_TABLE_STRUCTURE_CONFIDENCE": "0.74",
            "DOCUMENT_INTELLIGENCE_ENGINE_TABLE_CROP_PADDING": "29",
        }
    )
    completed = subprocess.run(
        [args.worker, "--print-engine-config"],
        check=True,
        capture_output=True,
        env=environment,
        text=True,
    )
    config = json.loads(completed.stdout)

    assert_equal(config["backends"]["registry_config"], "/worker/backends.json", "backends.registry_config")
    assert_equal(config["tesseract"]["executable"], "/worker/bin/tesseract", "tesseract.executable")
    assert_equal(config["tesseract"]["language"], "eng+chi_sim", "tesseract.language")
    assert_equal(config["paddle_ocr"]["detection_model"], "/worker/overrides/det.onnx", "paddle_ocr.det")
    assert_equal(
        config["paddle_ocr"]["recognition_model"],
        "/worker/models/paddleocr/rec.onnx",
        "paddle_ocr.rec",
    )
    assert_equal(
        config["paddle_ocr"]["character_dict"],
        "/worker/models/paddleocr/ppocrv5_dict.txt",
        "paddle_ocr.dict",
    )
    assert_equal(config["paddle_ocr"]["profile"], "ppocrv4_mobile", "paddle_ocr.profile")
    assert_equal(config["paddle_ocr"]["recognition_batch_size"], 17, "paddle_ocr.batch_size")
    assert_equal(config["paddle_ocr"]["recognition_max_width"], 1536, "paddle_ocr.max_width")
    assert_equal(config["paddle_ocr"]["detection_limit_side"], 1280, "paddle_ocr.limit_side")
    assert_equal(config["doclaynet"]["model"], "/worker/models/doclaynet.onnx", "doclaynet.model")
    assert_equal(config["doclaynet"]["confidence_threshold"], 0.71, "doclaynet.confidence")
    assert_equal(config["paddle_layout"]["model"], "/worker/models/paddle-layout.onnx", "paddle_layout.model")
    assert_equal(config["paddle_layout"]["confidence_threshold"], 0.62, "paddle_layout.confidence")
    assert_equal(
        config["table_transformer"]["detection_model"],
        "/worker/models/table-detection.onnx",
        "table.detection_model",
    )
    assert_equal(
        config["table_transformer"]["structure_model"],
        "/worker/models/table-structure.onnx",
        "table.structure_model",
    )
    assert_equal(config["table_transformer"]["detection_confidence_threshold"], 0.83, "table.det_conf")
    assert_equal(config["table_transformer"]["structure_confidence_threshold"], 0.74, "table.struct_conf")
    assert_equal(config["table_transformer"]["crop_padding"], 29, "table.crop_padding")

    invalid_runtime_environment = os.environ.copy()
    invalid_runtime_environment["WORKER_ENGINE_CACHE_SIZE"] = "0"
    invalid_runtime = subprocess.run(
        [args.worker],
        capture_output=True,
        env=invalid_runtime_environment,
        text=True,
    )
    assert_equal(invalid_runtime.returncode, 2, "invalid runtime limit exit code")
    if "must be positive" not in invalid_runtime.stderr:
        raise AssertionError(f"missing runtime limit diagnostic: {invalid_runtime.stderr!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
