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

The fixed annotated subset contains:

| Dataset | Samples | Existing ground truth used |
| --- | ---: | --- |
| Tesseract test corpus | 5 | Page-level reference transcripts |
| DocLayNet | 5 | 11 layout classes and bounding boxes |
| PubTables-1M | 5 | Table, row, column, header, and spanning-cell boxes |

These 15 images and annotations are intentionally committed with the repository. Tesseract samples are
Apache-2.0, DocLayNet is CDLA-Permissive-1.0, and PubTables-1M is CDLA-Permissive-2.0.

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
