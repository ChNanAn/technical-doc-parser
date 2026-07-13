#!/usr/bin/env python3

import hashlib
import json
from pathlib import Path


CORPUS_ROOT = Path(__file__).resolve().parent / "corpus"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    manifest = json.loads((CORPUS_ROOT / "manifest.json").read_text(encoding="utf-8"))
    if manifest["sample_count"] != 15 or len(manifest["datasets"]) != 3:
        raise RuntimeError("the distributed corpus must contain 3 datasets and 15 samples")

    image_count = 0
    for dataset in manifest["datasets"]:
        ground_truth_path = CORPUS_ROOT / dataset["ground_truth"]
        if sha256(ground_truth_path) != dataset["ground_truth_sha256"]:
            raise RuntimeError(f"ground-truth SHA256 mismatch: {ground_truth_path}")
        ground_truth = json.loads(ground_truth_path.read_text(encoding="utf-8"))
        if len(ground_truth["samples"]) != dataset["samples"]:
            raise RuntimeError(f"sample count mismatch: {ground_truth_path}")

        dataset_root = ground_truth_path.parent
        for sample in ground_truth["samples"]:
            image_path = dataset_root / sample["image"]
            if sha256(image_path) != sample["image_sha256"]:
                raise RuntimeError(f"image SHA256 mismatch: {image_path}")
            image_count += 1

            if "transcript" in sample:
                transcript_path = dataset_root / sample["transcript"]
                if sha256(transcript_path) != sample["transcript_sha256"]:
                    raise RuntimeError(f"transcript SHA256 mismatch: {transcript_path}")
            if "annotation" in sample:
                annotation_path = dataset_root / sample["annotation"]
                if sha256(annotation_path) != sample["annotation_sha256"]:
                    raise RuntimeError(f"annotation SHA256 mismatch: {annotation_path}")

            for item in sample.get("objects", []):
                x0, y0, x1, y1 = item["bbox"]
                if not (0 <= x0 <= x1 <= sample["width"] + 1 and 0 <= y0 <= y1 <= sample["height"] + 1):
                    raise RuntimeError(f"bbox outside image for {sample['id']}: {item['bbox']}")

    if image_count != 15:
        raise RuntimeError(f"expected 15 images, found {image_count}")
    print(f"Validated {image_count} distributed benchmark images across 3 datasets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
