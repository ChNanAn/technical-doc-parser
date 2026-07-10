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

The first baseline reports:

- `ok_rate`: ratio of pages where the OCR backend returned successfully.
- `corpus_cer`: character error rate over all evaluated pages after simple text normalization.
- per-page `cer`, `gt_chars`, `pred_chars`, and `edit_distance`.

This is intentionally a text-level baseline. FUNSD annotations contain word boxes, while the current PaddleOCR adapter returns normalized lines/spans. Detection precision/recall and end-to-end box matching should be added after the project defines a stable line/word matching policy.

If `ok_rate` is high but `text_found_rate` is zero, enable PaddleOCR backend diagnostics on a single page:

```bash
DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DEBUG=1 \
./build-ort/tests/funsd_ocr_eval \
  --funsd-root data/raw/funsd/dataset \
  --split testing_data \
  --limit 1
```

The diagnostic output includes detection output shape, probability range, contour counts, accepted boxes, crop counts, recognition output shape, and decoded text counts. This separates "the detector found no text boxes" from "the recognizer decoded no text".

## Next Metrics

The next useful evaluation steps are:

- Add word/line box matching with IoU thresholding.
- Report detection precision, recall, and hmean.
- Report matched-line CER.
- Keep a pinned small subset, such as `testing_data --limit 20`, for fast regression checks.
