# Quality Baseline Corpus

The benchmark has two complementary local corpora. Large or externally licensed binaries are downloaded into
`data/raw/quality_baseline/`, which is ignored by git.

- `inputs/`: 15 real-world pages for end-to-end smoke and JSON snapshot checks.
- `annotated/`: 15 already-annotated open-dataset samples for numerical OCR, layout, and table metrics.

## Prepare

```bash
bash scripts/download_quality_baseline.sh
python3 -m pip install pypdf pillow
python3 scripts/prepare_quality_baseline.py
```

The preparation step creates selected PDFs, canonical 200 DPI PNG files, and
`data/raw/quality_baseline/inputs/prepared_manifest.json`. It uses the project CLI for PDFium rendering, so build
the project before running it.

## Prepare the annotated subset

No manual drawing or transcription is required. The script selects five fixed samples from each upstream
dataset and converts the existing annotations into a small common JSON representation.

```bash
python3 -m pip install pillow remotezip
python3 scripts/prepare_annotated_baseline.py
python3 tests/benchmark/validate_corpus.py
```

The resulting files are written under:

```text
tests/benchmark/corpus/
  ocr_tesseract/ground_truth.json
  layout_doclaynet/ground_truth.json
  layout_doclaynet/subset.coco.json
  table_pubtables/ground_truth.json
  manifest.json
```

## Evaluate predictions

The metric evaluators consume backend-independent prediction JSON. Missing sample IDs are scored as empty
predictions; duplicate or unknown sample IDs and unknown labels are rejected. This makes failed inference visible
without silently accepting misspelled IDs or categories.

OCR predictions contain page-level text:

```json
{
  "version": 1,
  "task": "ocr_text",
  "dataset": "tesseract-ocr/test",
  "metadata": {
    "engine_version": "0.1.0",
    "git_revision": "<git-sha>",
    "model": {"name": "example", "sha256": "<sha256>"}
  },
  "samples": [
    {"id": "eurotext", "text": "recognized text"}
  ]
}
```

The optional `metadata` object is copied into the metric report. Prediction producers should record the engine
version, git revision, model name and hash, and relevant inference configuration there. If `dataset` is present it
must match the ground-truth dataset.

Calculate corpus and per-sample CER/WER after NFKC and whitespace normalization:

```bash
python3 tests/benchmark/evaluate_ocr.py \
  --predictions output/ocr_predictions.json \
  --output output/ocr_metrics.json
```

With ONNX Runtime enabled, `paddle_ocr_benchmark` generates predictions for all five committed OCR pages with the
pinned PaddleOCR model and enforces a case-insensitive corpus CER ceiling of `0.70`. The current baseline is
`0.6827`; the two magazine pages expose unresolved multi-column ordering errors, and the rotated page exposes the
absence of orientation handling:

```bash
ctest --test-dir build-ort -R paddle_ocr_benchmark --output-on-failure
```

Layout predictions use the internal mapped labels (`title`, `text`, `list`, `table`, `figure`, `header`,
`footer`, or `unknown`):

```json
{
  "version": 1,
  "task": "layout",
  "samples": [
    {
      "id": 1736,
      "objects": [
        {"label": "title", "bbox": [90.4, 94.8, 190.1, 106.0], "confidence": 0.9}
      ]
    }
  ]
}
```

```bash
python3 tests/benchmark/evaluate_layout.py \
  --predictions output/layout_predictions.json \
  --iou-threshold 0.5 \
  --output output/layout_metrics.json
```

When ONNX Runtime is enabled, two CTests generate predictions for all five committed images. The pinned RF-DETR
model has a `0.70` micro-F1 regression floor, while Paddle PP-DocLayoutV3 has a separate `0.45` floor because its
25-class taxonomy must be mapped to DocLayNet and has no `List-item` equivalent:

```bash
ctest --test-dir build -R '^(doclaynet_layout_benchmark|paddle_layout_benchmark)$' --output-on-failure
```

Table predictions use the PubTables structure labels (`table`, `table row`, `table column`,
`table column header`, and `table spanning cell`):

```bash
python3 tests/benchmark/evaluate_table.py \
  --predictions output/table_predictions.json \
  --iou-threshold 0.5 \
  --output output/table_metrics.json
```

When the pinned Table Transformer models are installed, `pubtables_table_benchmark` runs real C++ region and
structure inference over all five images and enforces a `0.95` micro-F1 floor:

```bash
bash scripts/setup_table_transformer.sh
ctest --test-dir build -R pubtables_table_benchmark --output-on-failure
```

The pinned baseline has micro-F1 `1.000` and mean matched IoU `0.9746` over 130 objects. The small in-domain subset
is suitable for regression detection only; it is not a broad table-model leaderboard or a text/TEDS evaluation.

Layout and table reports use class-aware, one-to-one maximum-cardinality matching. They contain per-class and
per-sample precision, recall, F1, mean matched IoU, micro/macro F1, and exact object-structure match rate. The table
metric evaluates the available PubTables row/column/header/spanning-cell boxes; it is not a text-content or TEDS
metric.

The corpus integrity check, evaluator unit tests, and perfect-prediction CLI smoke tests are registered with CTest
under the `benchmark` label. The normal GitHub Actions `ctest` step therefore runs them automatically:

```bash
ctest --test-dir build -L benchmark --output-on-failure
```

The fixed annotated subset contains:

| Dataset | Samples | Existing ground truth used |
| --- | ---: | --- |
| Tesseract test corpus | 5 | Page-level reference transcripts |
| DocLayNet | 5 | 11 layout classes and bounding boxes |
| PubTables-1M | 5 | Table, row, column, header, and spanning-cell boxes |

These 15 images and annotations are intentionally committed with the repository. Tesseract samples are
Apache-2.0, DocLayNet is CDLA-Permissive-1.0, and PubTables-1M is CDLA-Permissive-2.0.

## Pipeline reference spans

`corpus/pipeline_quality/ground_truth.json` covers all 15 real-world pages in the prepared input baseline. Each page
contains visually reviewed text spans and an explicit order for those spans. The spans come from independent PDF
text layers where available and visual transcription for image-only samples; they are not copied from engine output.

`text_completeness` is therefore a sampled reference-span metric, not a claim that every character on each page has
been transcribed. `reading_order_score` compares all pairs among anchors matched at or above the configured threshold,
and `reading_order_anchor_recall` prevents a high order score from hiding missing content.

The scheduled Pipeline Evaluation workflow downloads the source PDFs, prepares the fixed 15-page selection, and runs
one reusable `DocumentEngine` over all eight PDFs. Enable the local CTest entry explicitly after preparing the corpus:

```bash
cmake -S . -B build-ort -DDOCUMENT_INTELLIGENCE_ENGINE_ENABLE_PIPELINE_BENCHMARK=ON
cmake --build build-ort --target document_intelligence_engine pipeline_quality_eval --parallel
bash scripts/download_quality_baseline.sh
python3 scripts/prepare_quality_baseline.py --engine build-ort/cpp/app/document_intelligence_engine
ctest --test-dir build-ort -R '^pipeline_quality_benchmark$' --output-on-failure
```

The pinned model policy resolves to PaddleOCR, `doclaynet -> paddle-layout -> text` for layout, and
`table-transformer -> text` for tables. Predictions record the requested backends, configured fallback order, and
model paths and thresholds. The current regression baseline is:

| Metric | Baseline | Regression floor |
| --- | ---: | ---: |
| Sampled text completeness | 0.8934 | 0.88 |
| Reading-order anchor recall | 0.8571 | 0.84 |
| Pairwise reading-order score | 0.9385 | 0.92 |

The aggregate covers 2,280 reviewed characters and 77 anchors; 2,037 characters and 66 anchors are matched, and 122
of 130 comparable anchor pairs are ordered correctly. `irs_fw4_2024_selected:p02` currently emits only one empty
header block and scores zero on completeness and anchor recall. This failure remains in the corpus and report so an
aggregate score cannot hide it.

DocLayNet is read with HTTP Range requests, so only its test annotation JSON and five PNG files are transferred
from the 30 GB archive. PubTables images are streamed only until the five fixed entries are found; the multi-GB
image archive is not downloaded in full. Source versions, IDs, checksums, and license notes are recorded in
`annotated_sources.json`. Large intermediate annotation archives remain under the ignored `data/raw/` cache.

## Coverage

| Source | Selected pages | Purpose |
| --- | --- | --- |
| IRS Form 1040 (2024) | 1, 2 | Dense native-PDF form and ruled fields |
| IRS Form W-4 (2024) | 1, 3 | Form layout and worksheet table |
| NIST SP 800-53 Rev. 5 | 1, 15, 35, 36 | Cover, contents, control-family table, diagram |
| NASA Space Shuttle News Reference | 5, 14, 27 | Legacy technical contents, engineering diagram, dense specifications |
| PaddleOCR book photo | 1 | Perspective, background noise, formulas, English OCR |
| PaddleOCR formula document | 1 | Two-column reading order and mathematical formulas |
| PaddleOCR medal table | 1 | Chinese and English table structure |
| PaddleOCR small table | 1 | Low-resolution mixed-language table |

The upstream sources, immutable revisions, checksums, and licensing notes are recorded in `sources.json`.

## Licensing

The IRS, NIST, and NASA documents are official United States Government publications. PaddleOCR repository
samples are fetched from a fixed Apache-2.0-licensed repository commit. Keep source attribution with any copied
sample, and perform a separate legal review before redistributing the downloaded corpus as a standalone package.

The distributed annotated corpus uses only upstream sources with explicit redistribution terms. Retain the
source metadata, copyright notices, and license references in this directory when copying the corpus elsewhere.
