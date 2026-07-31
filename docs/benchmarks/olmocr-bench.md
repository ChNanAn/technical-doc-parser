# olmOCR-Bench Baseline

[olmOCR-Bench](https://github.com/allenai/olmocr/tree/main/olmocr/bench) evaluates Markdown or plain-text output
with machine-checkable facts about text presence and absence, natural reading order, tables, math, and basic output
health. Unlike the repository's small regression fixtures, this is an independent public benchmark over 1,403
single-page PDFs.

## Result

The first full CPU run scored **44.2% +/- 0.9%** over 8,413 tests. olmOCR-Bench computes the overall result as an
unweighted average of the seven suite scores and its generated baseline score, not as one micro-average over tests.

| Official suite | Score | Passed / tests |
| --- | ---: | ---: |
| Baseline output health | 92.4% | 1,288 / 1,394 |
| Headers and footers | 94.5% | 718 / 760 |
| Multi-column order | 52.3% | 462 / 884 |
| Tables | 50.7% | 518 / 1,022 |
| Long tiny text | 47.7% | 211 / 442 |
| Old scans | 16.3% | 86 / 526 |
| arXiv math | 0.0% | 0 / 2,927 |
| Old scans math | 0.0% | 0 / 458 |
| **Overall** | **44.2% +/- 0.9%** | **8,413 tests** |

The two baseline facts committed inside other suite files explain why the table suite contains 1,022 entries while
the benchmark's `table` test-type summary contains 1,020. The official cross-suite test-type summary was: baseline
`92.4%`, absence `93.8%`, table `50.6%`, order `44.0%`, presence `32.0%`, and math `0.0%`.

### Unreleased reading-order checkpoint

The post-v0.1.1 reading-order work was rerun on the complete multi-column suite on 2026-07-31. The unmodified official
scorer passed **575 / 884 order tests (65.0%)**, up from **462 / 884 (52.3%)** in the published baseline. Candidate
health remained **228 / 231 PDFs (98.7%)**, and the runner successfully parsed all 231 PDFs without reusing old
candidates.

This is a category-scoped development checkpoint, not a replacement full-benchmark score. The released, comparable
cross-suite baseline remains **44.2% +/- 0.9%** until all seven suites are regenerated at one revision. The checkpoint
uses the same pinned olmOCR source and data, 200 DPI render, CPU inference, and 1,000 bootstrap samples as the
published run.

## Interpretation

- Output health and header/footer suppression are already credible. All 1,403 PDFs produced a candidate file with
  no parse/export failure; 42 candidates were empty and remained in the candidate set for the benchmark to score.
- Table and multi-column results establish useful but early capability. They are high enough to prove the structured
  pipeline is doing real work and low enough to provide a large, measurable improvement range.
- Old scans expose OCR and assembly weakness that the five-page OCR regression cannot represent.
- Math is a known capability gap, not a mysterious OCR miss. The current exporter emits visible characters but no
  renderable LaTeX, so all structure-aware math facts fail.
- The 44.2 score is not competitive with mature document parsers reported by olmOCR-Bench. Its value is as an honest,
  reproducible starting point and a map from product-level failures back to OCR, reading order, table, and exporter
  work.

The run also exposed invalid UTF-8 from PDFium supplementary-plane surrogate pairs. That general input-boundary bug
was fixed before the published run; all final candidates pass strict UTF-8 decoding.

## Run Identity

| Item | Value |
| --- | --- |
| Engine revision | `27e2676` |
| olmOCR source | `f7cfe4c22098b154c76b6ec950d1c0a464eecf8d` (`0.4.27`) |
| olmOCR-Bench data | `54a96a6fb6a2bd3b297e59869491db4d3625b711` |
| Dataset license | ODC-BY-1.0 |
| Scorer | Unmodified `olmocr.bench.benchmark`, 1,000 bootstrap samples |
| Hardware | Intel Core Ultra 7 265, 20 CPU cores, 19 GiB RAM, CPU-only inference |
| Render | 200 DPI |
| OCR | PaddleOCR `ppocrv5_mobile` |
| Layout | `doclaynet -> paddle-layout -> text` |
| Table | `table-transformer -> text` |
| Runtime | ONNX Runtime 1.18.1 |

This run was interrupted to fix the UTF-8 defect, then resumed and category-sharded. Its elapsed time is therefore
not a comparable latency result. The scores remain comparable because every candidate used the same engine policy.
The machine-readable result is [olmocr-bench-2026-07.json](olmocr-bench-2026-07.json).

## Reproduce

Prepare the pinned upstream source and dataset using their documented Python 3.11+ environment:

```bash
git clone https://github.com/allenai/olmocr.git data/raw/olmocr-source
git -C data/raw/olmocr-source checkout f7cfe4c22098b154c76b6ec950d1c0a464eecf8d
python3 -m venv data/raw/olmocr-venv
data/raw/olmocr-venv/bin/pip install -e 'data/raw/olmocr-source[bench]'
data/raw/olmocr-venv/bin/playwright install chromium
hf download allenai/olmOCR-bench --repo-type dataset \
  --revision 54a96a6fb6a2bd3b297e59869491db4d3625b711 \
  --include 'bench_data/**' --local-dir data/raw/olmocr-bench
```

Build and generate the candidate Markdown with one reusable engine:

```bash
cmake -S . -B build-ort -DCMAKE_BUILD_TYPE=Release \
  -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_TABLE_TRANSFORMER=ON
cmake --build build-ort --target olmocr_bench_runner --parallel
./build-ort/tests/olmocr_bench_runner \
  --pdf-root data/raw/olmocr-bench/bench_data/pdfs \
  --output-root data/raw/olmocr-bench/bench_data/document_intelligence_engine \
  --work-dir build-ort/olmocr-bench-work \
  --report build-ort/olmocr-bench-runner-report.json \
  --dpi 200 --resume
```

Run the unmodified official scorer:

```bash
PYTHONPATH=data/raw/olmocr-source \
data/raw/olmocr-venv/bin/python -m olmocr.bench.benchmark \
  --dir data/raw/olmocr-bench/bench_data \
  --candidate document_intelligence_engine \
  --bootstrap_samples 1000
```

`--resume` treats an existing candidate, including an intentional empty failure candidate, as completed. Omit it to
regenerate every output after changing engine behavior.
