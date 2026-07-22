# Evaluation

This project uses public datasets for repeatable OCR, layout, table, and reading-order evaluation. The first OCR evaluation target is FUNSD because it is small, public, and contains scanned form images with text annotations.

## FUNSD OCR Baseline

FUNSD is not committed to this repository. Download it into `data/raw/`, which is ignored by git:

```bash
bash scripts/download_funsd.sh
```

The expected extracted layout is:

```text
data/raw/funsd/dataset/
  training_data/
    annotations/
    images/
  testing_data/
    annotations/
    images/
```

Build the ONNX Runtime/PaddleOCR evaluation target:

```bash
cmake -S . -B build-ort \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-ort --config Release --target funsd_ocr_eval --parallel
```

By default, CMake downloads pinned ONNX Runtime and PaddleOCR baseline models into:

```text
third_party/onnxruntime-linux-x64-1.18.1/
models/paddleocr/baseline/
```

Run a small smoke evaluation first:

```bash
./build-ort/tests/funsd_ocr_eval \
  --funsd-root data/raw/funsd/dataset \
  --split testing_data \
  --limit 5 \
  --report output/funsd_ocr_eval_5.json
```

Then run the full testing split:

```bash
./build-ort/tests/funsd_ocr_eval \
  --funsd-root data/raw/funsd/dataset \
  --split testing_data \
  --report output/funsd_ocr_eval_testing.json
```

The evaluator reports three independent layers:

- `ok_rate`: ratio of pages where the OCR backend returned successfully.
- `corpus_cer`: character error rate over all evaluated pages after simple text normalization.
- `detection_recall`: fraction of FUNSD ground-truth word boxes for which a detected line covers at least 50% of
  the word area. The threshold can be changed with `--detection-coverage-threshold`.
- `gt_crop_recognition_cer`: recognition CER when the recognizer receives ground-truth word crops, isolating
  recognition from detection and reading order.
- per-page end-to-end, detection, and ground-truth-crop counters.

PaddleOCR detects text lines while FUNSD annotates words, so the report intentionally does not call line count versus
word count a detection precision metric. Coverage recall is stable across that granularity mismatch. A future
line-grouping policy can add matched-line precision and hmean without changing the current metrics.

If `ok_rate` is high but `text_found_rate` is zero, enable PaddleOCR backend diagnostics on a single page:

```bash
DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DEBUG=1 \
./build-ort/tests/funsd_ocr_eval \
  --funsd-root data/raw/funsd/dataset \
  --split testing_data \
  --limit 1
```

The diagnostic output includes the model profile, detection shape and probability range, contour rejection counts,
dynamic recognition input/output shapes, crop counts, batch count, and decoded text counts. This separates "the
detector found no text boxes" from "the recognizer decoded no text" and verifies that batching is active.

## Next Metrics

The next useful evaluation steps are:

- Define a word-to-line grouping policy, then report matched-line precision, recall, hmean, and CER.
- Add orientation-specific recognition fixtures before introducing an angle classifier.
- Keep a pinned small subset, such as `testing_data --limit 20`, for fast regression checks.
