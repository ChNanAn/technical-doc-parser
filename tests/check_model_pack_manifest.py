#!/usr/bin/env python3
"""Check that setup-script defaults remain synchronized with the model manifest."""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parent.parent
MANIFEST = json.loads(
    (ROOT_DIR / "packaging" / "model-pack.v1.json").read_text(encoding="utf-8")
)


def script_config(script: str) -> dict[str, str]:
    environment = {
        key: value
        for key, value in os.environ.items()
        if not key.startswith(
            ("PADDLEOCR_", "DOCLAYNET_", "PADDLE_LAYOUT_", "TABLE_")
        )
    }
    completed = subprocess.run(
        ["bash", str(ROOT_DIR / "scripts" / script), "--print-config"],
        cwd=ROOT_DIR,
        check=True,
        capture_output=True,
        text=True,
        env=environment,
    )
    values: dict[str, str] = {}
    for line in completed.stdout.splitlines():
        key, separator, value = line.partition("=")
        if separator:
            values[key] = value
    return values


def expect(entry_id: str, config: dict[str, str], prefix: str) -> None:
    entry = next(item for item in MANIFEST["files"] if item["id"] == entry_id)
    actual = {
        "path": config[f"{prefix}_PATH"],
        "revision": config[f"{prefix}_REVISION"],
        "url": config[f"{prefix}_URL"],
        "sha256": config[f"{prefix}_SHA256"],
    }
    expected = {
        "path": f"models/{entry['path']}",
        "revision": entry["source"]["revision"],
        "url": entry["source"]["url"],
        "sha256": entry["sha256"],
    }
    if actual != expected:
        raise AssertionError(f"{entry_id} setup defaults differ: {actual} != {expected}")


def main() -> int:
    paddle_ocr = script_config("setup_paddleocr_baseline.sh")
    expect(
        "paddleocr-v5-mobile-detection",
        {
            "PADDLEOCR_DET_PATH": paddle_ocr["PADDLEOCR_DET_MODEL"],
            "PADDLEOCR_DET_REVISION": paddle_ocr["PADDLEOCR_DET_REVISION"],
            "PADDLEOCR_DET_URL": paddle_ocr["PADDLEOCR_DET_URL"],
            "PADDLEOCR_DET_SHA256": paddle_ocr["PADDLEOCR_DET_SHA256"],
        },
        "PADDLEOCR_DET",
    )
    expect(
        "paddleocr-v5-mobile-recognition",
        {
            "PADDLEOCR_REC_PATH": paddle_ocr["PADDLEOCR_REC_MODEL"],
            "PADDLEOCR_REC_REVISION": paddle_ocr["PADDLEOCR_REC_REVISION"],
            "PADDLEOCR_REC_URL": paddle_ocr["PADDLEOCR_REC_URL"],
            "PADDLEOCR_REC_SHA256": paddle_ocr["PADDLEOCR_REC_SHA256"],
        },
        "PADDLEOCR_REC",
    )
    expect(
        "paddleocr-v5-dictionary",
        {
            "PADDLEOCR_DICT_PATH": paddle_ocr["PADDLEOCR_DICT"],
            "PADDLEOCR_DICT_REVISION": paddle_ocr["PADDLEOCR_DICT_REVISION"],
            "PADDLEOCR_DICT_URL": paddle_ocr["PADDLEOCR_DICT_URL"],
            "PADDLEOCR_DICT_SHA256": paddle_ocr["PADDLEOCR_DICT_SHA256"],
        },
        "PADDLEOCR_DICT",
    )

    doclaynet = script_config("setup_doclaynet_layout.sh")
    expect(
        "rfdetr-doclaynet-layout",
        {
            "DOCLAYNET_MODEL_PATH": doclaynet["DOCLAYNET_MODEL"],
            "DOCLAYNET_MODEL_REVISION": doclaynet["DOCLAYNET_REVISION"],
            "DOCLAYNET_MODEL_URL": doclaynet["DOCLAYNET_MODEL_URL"],
            "DOCLAYNET_MODEL_SHA256": doclaynet["DOCLAYNET_MODEL_SHA256"],
        },
        "DOCLAYNET_MODEL",
    )

    paddle_layout = script_config("setup_paddle_layout.sh")
    expect(
        "paddle-doclayout-v3-model",
        {
            "PADDLE_LAYOUT_MODEL_PATH": paddle_layout["PADDLE_LAYOUT_MODEL"],
            "PADDLE_LAYOUT_MODEL_REVISION": paddle_layout["PADDLE_LAYOUT_REVISION"],
            "PADDLE_LAYOUT_MODEL_URL": paddle_layout["PADDLE_LAYOUT_MODEL_URL"],
            "PADDLE_LAYOUT_MODEL_SHA256": paddle_layout["PADDLE_LAYOUT_MODEL_SHA256"],
        },
        "PADDLE_LAYOUT_MODEL",
    )
    expect(
        "paddle-doclayout-v3-config",
        {
            "PADDLE_LAYOUT_CONFIG_PATH": paddle_layout["PADDLE_LAYOUT_CONFIG"],
            "PADDLE_LAYOUT_CONFIG_REVISION": paddle_layout["PADDLE_LAYOUT_REVISION"],
            "PADDLE_LAYOUT_CONFIG_URL": paddle_layout["PADDLE_LAYOUT_CONFIG_URL"],
            "PADDLE_LAYOUT_CONFIG_SHA256": paddle_layout[
                "PADDLE_LAYOUT_CONFIG_SHA256"
            ],
        },
        "PADDLE_LAYOUT_CONFIG",
    )

    table = script_config("setup_table_transformer.sh")
    expect(
        "table-transformer-detection",
        {
            "TABLE_DETECTION_PATH": table["TABLE_DETECTION_MODEL"],
            "TABLE_DETECTION_REVISION": table["TABLE_DETECTION_REVISION"],
            "TABLE_DETECTION_URL": table["TABLE_DETECTION_URL"],
            "TABLE_DETECTION_SHA256": table["TABLE_DETECTION_SHA256"],
        },
        "TABLE_DETECTION",
    )
    expect(
        "table-transformer-structure",
        {
            "TABLE_STRUCTURE_PATH": table["TABLE_STRUCTURE_MODEL"],
            "TABLE_STRUCTURE_REVISION": table["TABLE_STRUCTURE_REVISION"],
            "TABLE_STRUCTURE_URL": table["TABLE_STRUCTURE_URL"],
            "TABLE_STRUCTURE_SHA256": table["TABLE_STRUCTURE_SHA256"],
        },
        "TABLE_STRUCTURE",
    )
    print(f"Verified setup defaults for {len(MANIFEST['files'])} model files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
