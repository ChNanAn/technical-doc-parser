# Evaluation

[中文说明](evaluation.zh-CN.md)

The project separates interoperability from quality:

- [Document Contract v1](document-contract-v1.md) defines whether a result can be interpreted consistently.
- Three-layer evaluation determines whether the engine is actually accurate, useful, and efficient.
- [Quality Report v1](../schemas/quality-report.v1.schema.json) records comparable results with engine and corpus
  identity. See the [example report](../schemas/examples/quality-report.v1.example.json).

A valid JSON document is not necessarily a good parse. Conversely, an honest partial parse should remain valid and
receive lower quality scores instead of inventing data just to satisfy the Schema.

## Three Evaluation Layers

| Layer | Question | Core metrics |
| --- | --- | --- |
| Backend | Does one model or provider perform its isolated task? | OCR CER/WER, Layout F1, Table Structure F1 |
| Pipeline | Did stage composition preserve and organize the document? | Text completeness and duplication, Block Type F1, reading order, table text |
| Product | Can a downstream system rely on the delivered result? | JSON compatibility, success rate, RAG citation completeness, latency, memory |

Backend metrics diagnose components. Pipeline metrics catch integration failures that isolated models cannot see, such
as lost native text, duplicate OCR, wrong block assembly, or broken reading order. Product metrics measure the API and
operational result that users actually receive. A release should not substitute a strong Backend score for a missing
Pipeline or Product measurement.

## Metric Definitions

### Backend

- `ocr_cer` and `ocr_wer`: corpus-level Levenshtein edit distance divided by reference characters or words after a
  versioned normalization policy. Missing predictions are scored as empty, not dropped.
- `layout_micro_f1`: class-aware, one-to-one bbox matching at a recorded IoU threshold. Reports must retain precision,
  recall, macro F1, per-class counts, and the label mapping used by the Backend.
- `table_structure_micro_f1`: class-aware, one-to-one matching of table, row, column, header, and spanning-cell
  objects. Text quality is intentionally excluded.

### Pipeline

- `text_completeness`: reference characters aligned to the final normalized document text divided by total reference
  characters. It exposes content lost between extraction/OCR and assembly.
- `text_duplication_rate`: normalized output characters whose multiplicity exceeds the full-text reference divided by
  normalized output characters. Character-multiset matching keeps this metric independent of reading order. Report
  reference coverage and CER beside it so unavailable references or substituted text are not mistaken for quality.
- `block_type_micro_f1`: class-aware one-to-one matching between final blocks and annotated blocks, using the recorded
  IoU threshold and taxonomy mapping.
- `reading_order_score`: correctly ordered pairs divided by all comparable pairs among matched content blocks. This is
  a normalized Kendall-style score in `[0, 1]`; pages with fewer than two matched blocks are reported separately.
- `table_text_cer`: corpus CER over text in structure-matched table cells. It complements, rather than replaces, Table
  Structure F1.
- Header/footer precision and recall should be recorded when the final consumer output removes those regions.

The normalization policy, bbox matching threshold, taxonomy mapping, and treatment of empty samples are part of the
metric version. Changing one requires a new corpus or metric version; silently changing the formula invalidates trend
comparisons.

### Product

- `document_success_rate`: attempted documents that finish with a usable `complete` or explained `partial` result,
  divided by all attempted documents. Timeouts, crashes, unrecoverable jobs, and invalid output are failures.
- `json_contract_valid_rate`: usable outputs that validate against the selected public contract. Contract fixture
  snapshots additionally catch accidental field removal or semantic changes; snapshots are reviewed, not blindly
  regenerated.
- `rag_citation_completeness`: emitted chunks whose source references resolve to existing pages and valid page-local
  regions, divided by all emitted chunks. A later RAG Chunk contract may add quote agreement and retrieval metrics.
- `latency_ms_per_page_p50/p95`: wall-clock latency normalized by page count, with hardware, concurrency, warm/cold
  state, and Backend configuration recorded.
- `peak_rss_mib_p50/p95`: peak resident memory under the same controlled run. GPU memory is reported separately.

## Quality Report v1

Quality Report v1 uses open metric names inside three stable layer names: `backend`, `pipeline`, and `product`. Every
metric records a numeric value, unit, direction, and optional threshold, counts, and details. A report also identifies:

- Engine version, Git revision, and configuration.
- Corpus name, version, split, sample count, and optional manifest SHA256.
- Overall status: `passed`, `failed`, or `incomplete`.
- Baseline report and per-document artifacts when available.

Open metric names allow new measurements without changing the Schema. Comparability comes from documented metric
semantics and versioned corpora, not from a closed enum.

## Corpus Strategy

Use three corpus sizes for different feedback loops:

1. **Contract fixtures**: native text, scanned OCR, complex tables, and multi-column order. Small enough for every CI
   run; used for Schema, semantic-reference, and reviewed snapshot tests.
2. **Pinned regression corpus**: representative technical manuals, specifications, drawings, and table-heavy reports.
   Runs on scheduled CI and produces all three quality layers.
3. **External validation corpus**: larger or private domain sets. Used before releases to test generalization; aggregate
   reports may be published without committing source documents.

Every corpus needs a manifest with stable document IDs, content hashes, licensing/source notes, annotation version,
and train/test isolation. Tiny committed model sets are regression alarms, not production-accuracy evidence.

## Current Backend Baselines

This project uses public datasets for repeatable OCR, layout, and table evaluation. The first OCR target is FUNSD
because it is small, public, and contains scanned form images with text annotations.

## Committed PaddleOCR Regression

The required ONNX build runs real PaddleOCR detection and recognition over five redistributable pages from the
Tesseract test corpus. A C++ producer writes page-level predictions, then the backend-independent evaluator applies
NFKC normalization, whitespace collapse, case folding, and corpus-level CER/WER:

```bash
ctest --test-dir build-ort -R paddle_ocr_benchmark --output-on-failure
```

The pinned `ppocrv5_mobile` baseline has corpus CER `0.6827`, character-count ratio `0.9714`, and WER `0.8487`.
CI enforces `CER <= 0.70` and publishes `paddle_ocr_report.json` as an artifact. The high page-level CER is visible
evidence of two known limitations: text from multi-column magazine pages is emitted in row-wise order, and the
baseline has no 180-degree orientation handling. On the two simple upright pages, CER is `0.0170` and `0.0000`.
This five-page set is a deterministic regression gate, not a broad OCR accuracy claim.

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

The evaluator reports several complementary metrics:

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

## OCR Next Steps

The next useful evaluation steps are:

- Define a word-to-line grouping policy, then report matched-line precision, recall, hmean, and CER.
- Add orientation-specific recognition fixtures before introducing an angle classifier.
- Keep a pinned small subset, such as `testing_data --limit 20`, for fast regression checks.

## DocLayNet Layout F1

The repository distributes five DocLayNet images and their annotations. With ONNX Runtime enabled, CTest runs real
RF-DETR and Paddle PP-DocLayoutV3 inference on all five and then applies class-aware, maximum-cardinality IoU
matching at IoU `0.5`:

```bash
ctest --test-dir build -R '^(doclaynet_layout_benchmark|paddle_layout_benchmark)$' --output-on-failure
```

The prediction and metric reports are written under the build tree with `doclaynet_layout_` and `paddle_layout_`
prefixes. CI uses model-specific micro-F1 regression floors: `0.70` for RF-DETR and `0.45` for Paddle.

| Backend | Precision | Recall | Micro F1 | Macro F1 | Mean matched IoU |
| --- | ---: | ---: | ---: | ---: | ---: |
| RF-DETR DocLayNet | 0.880342 | 0.682119 | 0.768657 | 0.839304 | 0.873327 |
| Paddle PP-DocLayoutV3 | 0.478788 | 0.523179 | 0.500000 | 0.590087 | 0.826115 |

Paddle's 25 labels are mapped into the internal DocLayNet-shaped evaluation labels. It has no `List-item`
equivalent, so this comparison measures taxonomy compatibility as well as detector quality. RF-DETR's native
DocLayNet taxonomy therefore has an inherent advantage on this corpus.

The threshold is a regression floor, not a production acceptance target. Five pages are enough to catch model,
preprocessing, label-map, and postprocessing regressions, but not enough to establish domain-wide quality.

## PubTables-1M Table Structure

With both Table Transformer models installed, CTest runs region detection, padded region cropping, and structure
recognition on the five committed PubTables-1M table images:

```bash
ctest --test-dir build -R pubtables_table_benchmark --output-on-failure
```

At IoU `0.5`, the pinned baseline predicts all 130 table, row, column, column-header, projected-row-header, and
spanning-cell objects:

| Precision | Recall | Micro F1 | Macro F1 | Mean matched IoU | Exact match |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1.000 | 1.000 | 1.000 | 1.000 | 0.9746 | 1.000 |

CI enforces a `0.95` micro-F1 regression floor. This is a deliberately tiny in-domain regression set for the
Table Transformer/PubTables label system, not evidence of perfect production accuracy. It catches changes to
region preprocessing, crop padding, tensor decoding, label mapping, and structural postprocessing. Text accuracy
and TEDS are not yet measured by this object-structure metric.

## 15-Page Pipeline Baseline

A scheduled and manually dispatchable workflow runs one reusable `DocumentEngine` over eight public PDFs containing
15 reviewed pages. The final ordered `DocumentBlock` output is evaluated against 2,280 independently reviewed
characters and 77 reading-order anchors.

| Metric | Baseline | Regression guard |
| --- | ---: | ---: |
| Sampled text completeness | 0.8934 | 0.88 |
| Full-text duplication rate (11/15 pages) | 0.1421 | <= 0.16 |
| Reading-order anchor recall | 0.8571 | 0.84 |
| Pairwise reading-order score | 0.9385 | 0.92 |

The baseline matches 2,037 reviewed characters and 66 anchors; 122 of 130 comparable pairs are correctly ordered.
The 11 native-PDF pages also provide 35,742 normalized reference characters. Character-multiset matching finds 4,856
extra characters among 34,170 output characters; because this order-independent count also reacts to substitutions,
the report includes companion full-text CER `0.4608`. The four image-only pages are excluded and reported through
reference coverage rather than silently entering either denominator.
One selected IRS W-4 worksheet page currently produces only an empty header block and scores zero for completeness
and anchor recall. It is intentionally retained as a visible failure and improvement target. These floors detect
regression under the pinned corpus and model policy; they are not production acceptance thresholds. Reproduction
commands, metric scope, and corpus sources are in the [Benchmark Guide](../tests/benchmark/README.md).

The Pipeline gate separately wraps the five committed DocLayNet images in deterministic 200 DPI PDFs and evaluates
the final assembled `DocumentBlock` objects at IoU `0.5`. Against 151 reference blocks, the current output has 118
predictions and 104 true positives: precision `0.8814`, recall `0.6887`, micro-F1 `0.7732`, and mean matched IoU
`0.8740`. CI enforces micro-F1 `>= 0.75`. This is distinct from the direct layout-Backend benchmark because OCR,
table recognition, and document assembly all run before scoring.

## External Validation

The first complete run on the independent 1,403-PDF olmOCR-Bench scored `44.2% +/- 0.9%` over 8,413 tests. Strong
header/footer suppression (`94.5%`) and baseline output health (`92.4%`) coexist with early table (`50.7%`) and
multi-column order (`52.3%`) performance, weak old-scan results (`16.3%`), and no structure-aware math output (`0%`).

This is intentionally published despite the low overall score. It is a reproducible external starting point, not a
production claim, and it gives future OCR, reading-order, table, and formula-export changes an independent trend line.
See the [full olmOCR-Bench report](benchmarks/olmocr-bench.md) for versions, counts, commands, and limitations.

## Implementation Priority

The Backend baselines and sampled text/order Pipeline gate should remain in CI. The next evaluation work is:

1. Add structure-matched table text CER.
2. Measure success rate, citation completeness, p50/p95 latency, and peak RSS in one repeatable end-to-end runner.
3. Emit `quality-report.v1` and compare it with a pinned baseline in CI.

Regression floors should be introduced only after the metric and corpus are stable. A floor prevents known regressions;
it is not automatically a production acceptance target.
