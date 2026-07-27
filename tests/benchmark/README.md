# Quality Baseline Corpus

The benchmark has two complementary local corpora. Large or externally licensed binaries are downloaded into
`data/raw/quality_baseline/`, which is ignored by git.

- `inputs/`: 15 real-world pages for end-to-end smoke and JSON snapshot checks.
- `annotated/`: 15 already-annotated open-dataset samples for numerical OCR, layout, and table metrics.

## Prepare

```bash
bash scripts/download_quality_baseline.sh
python3 -m pip install pypdf==5.9.0 pillow==11.3.0
python3 scripts/prepare_quality_baseline.py
```

The preparation step creates selected PDFs, canonical 200 DPI PNG files, and
`data/raw/quality_baseline/inputs/prepared_manifest.json`. It uses the project CLI for PDFium rendering, so build
the project before running it.

The committed full-text references for the 11 native-PDF pages are reproducible with a pinned independent
extractor. The four image-only pages intentionally have no full-text reference:

```bash
python3 -m pip install pypdf==5.9.0
python3 scripts/prepare_pipeline_text_references.py
```

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

Table predictions contain PubTables structure objects and final cells with bbox, row/column indices, spans, and text:

```bash
python3 tests/benchmark/evaluate_table.py \
  --predictions output/table_predictions.json \
  --iou-threshold 0.5 \
  --output output/table_metrics.json
```

When the pinned PaddleOCR and Table Transformer models are installed, `pubtables_table_benchmark` runs real C++ OCR,
region/structure inference, and final cell-text assignment over all five images. It enforces a `0.95` structure
micro-F1 floor and a `0.08` structure-matched Table Text CER ceiling:

```bash
bash scripts/setup_table_transformer.sh
ctest --test-dir build -R pubtables_table_benchmark --output-on-failure
```

The pinned baseline has micro-F1 `1.000` and mean matched IoU `0.9746` over 130 objects. All 384 final cells match a
reference cell, and case-insensitive Table Text CER is `0.0577` (210 edits over 3,642 NFKC-normalized reference
characters). Exact cell text is `271/384` (`0.7057`).

Layout and table reports use class-aware, one-to-one maximum-cardinality matching. They contain per-class and
per-sample precision, recall, F1, mean matched IoU, micro/macro F1, and exact object-structure match rate. Table cells
are matched one-to-one by bbox IoU independently of text; missing reference cells are scored as empty predictions.
The small in-domain subset is suitable for regression detection only. It is not a broad table-model leaderboard and
does not measure TEDS or cross-page tables.

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
| PubTables-1M + Europe PMC JATS | 5 | Table structure boxes and cell text |

These 15 images and annotations are intentionally committed with the repository. Tesseract samples are
Apache-2.0, DocLayNet is CDLA-Permissive-1.0, and PubTables-1M is CDLA-Permissive-2.0.

## Pipeline reference spans

`corpus/pipeline_quality/ground_truth.json` covers all 15 real-world pages in the prepared input baseline. Each page
contains visually reviewed text spans and an explicit order for those spans. The spans come from independent PDF
text layers where available and visual transcription for image-only samples; they are not copied from engine output.

`text_completeness` is therefore a sampled reference-span metric, not a claim that every character on each page has
been transcribed. `reading_order_score` compares all pairs among anchors matched at or above the configured threshold,
and `reading_order_anchor_recall` prevents a high order score from hiding missing content.

Eleven native-PDF pages additionally have pinned, SHA256-verified full-text references. `text_duplication_rate`
compares normalized character multiplicities, so reading-order changes do not create false duplicates. It is reported
with full-text CER because OCR substitutions can also appear as extra characters. The four image-only pages are
excluded from both full-text metrics and remain explicit in the `full_text_reference_coverage` count.

The scheduled Pipeline Evaluation workflow downloads the source PDFs, prepares the fixed 15-page selection, and runs
one reusable `DocumentEngine` over all eight PDFs. Enable the local CTest entry explicitly after preparing the corpus:

```bash
cmake -S . -B build-ort -DDOCUMENT_INTELLIGENCE_ENGINE_ENABLE_PIPELINE_BENCHMARK=ON
cmake --build build-ort --target document_intelligence_engine pipeline_quality_eval --parallel
bash scripts/download_quality_baseline.sh
python3 scripts/prepare_quality_baseline.py --engine build-ort/cpp/app/document_intelligence_engine
ctest --test-dir build-ort \
  -R '^(pipeline_(quality|block_type)_benchmark|pipeline_quality_report_v1)$' \
  --output-on-failure --no-tests=error
```

The pinned model policy resolves to PaddleOCR, `doclaynet -> paddle-layout -> text` for layout, and
`table-transformer -> text` for tables. Predictions record the requested backends, configured fallback order, and
model paths and thresholds. The current regression baseline is:

`pipeline_quality_report_v1` converts the evaluator output through the versioned
[`pipeline-quality-v1` profile](profiles/pipeline-quality-v1.json), validates the result against the public Schema,
and writes `build-ort/tests/pipeline_quality_report.v1.json`. It records source-report and corpus hashes and remains
`incomplete` until Backend and Product evidence from the same versioned quality suite is available.

| Metric | Baseline | Regression guard |
| --- | ---: | ---: |
| Sampled text completeness | 0.8934 | 0.88 |
| Full-text duplication rate (11/15 pages) | 0.1421 | <= 0.16 |
| Reading-order anchor recall | 0.8571 | 0.84 |
| Pairwise reading-order score | 0.9385 | 0.92 |

The aggregate covers 2,280 reviewed characters and 77 anchors; 2,037 characters and 66 anchors are matched, and 122
of 130 comparable anchor pairs are ordered correctly. Across the 11 full-text references, 4,856 of 34,170 output
characters exceed the 35,742-character reference multiset. Companion full-text CER is `0.4608` and has no regression
threshold yet. `irs_fw4_2024_selected:p02` currently emits only one empty header block and scores zero on completeness
and anchor recall. This failure remains in the corpus and report so an aggregate score cannot hide it.

The Pipeline gate also wraps the five committed DocLayNet images in deterministic 200 DPI PDFs and evaluates final
assembled `DocumentBlock` objects, rather than calling the layout Backend directly. At IoU `0.5`, the baseline has
104 true positives over 151 references and 118 predictions: precision `0.8814`, recall `0.6887`, micro-F1 `0.7732`,
and mean matched IoU `0.8740`. CI enforces micro-F1 `>= 0.75`; the report retains per-class and per-page failures.

## External olmOCR-Bench

`olmocr_bench_runner` converts the independent 1,403-PDF olmOCR-Bench corpus to its required candidate layout while
reusing model sessions across documents. It supports `--category`, `--limit`, and `--resume`; parse failures are
represented by empty Markdown so the official scorer penalizes them instead of silently dropping samples.

The first full run scored `44.2% +/- 0.9%` over 8,413 official tests. The complete result, exact upstream revisions,
hardware, commands, and interpretation are published in the
[olmOCR-Bench baseline report](../../docs/benchmarks/olmocr-bench.md). The external corpus and generated candidates
remain under ignored `data/raw/`; they are not CI dependencies or repository fixtures.

DocLayNet is read with HTTP Range requests, so only its test annotation JSON and five PNG files are transferred
from the 30 GB archive. PubTables images are streamed only until the five fixed entries are found; its 4.17 GB words
archive is not a dependency. Cell text comes from four small SHA256-pinned Europe PMC JATS XML responses. Source
versions, IDs, checksums, and license notes are recorded in `annotated_sources.json` and each table sample. Large
intermediate annotation archives remain under the ignored `data/raw/` cache.

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
