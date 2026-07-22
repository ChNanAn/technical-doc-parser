# Dependencies

This project keeps large native dependencies out of git. They are downloaded into `third_party/` by setup scripts and ignored by `.gitignore`.

## PDFium

PDF rendering uses prebuilt PDFium binaries from [`bblanchon/pdfium-binaries`](https://github.com/bblanchon/pdfium-binaries).

CMake downloads the pinned Linux x64 build automatically when PDFium is enabled and the package is missing:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The downloaded package is installed into:

```text
third_party/pdfium
```

Automatic setup can be disabled:

```bash
cmake -S . -B build -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PDFIUM=OFF
```

The setup script can still be run manually when refreshing the local copy or debugging dependency setup:

```bash
bash scripts/setup_pdfium.sh
```

To use a custom PDFium location:

```bash
cmake -S . -B build -DPDFium_DIR=/path/to/pdfium
cmake --build build --config Release --parallel
```

The setup script currently pins:

```text
PDFIUM_VERSION=151.0.7906.0
PDFIUM_PACKAGE=pdfium-linux-x64.tgz
PDFIUM_DIR=third_party/pdfium
```

The release archive is verified against the SHA256 embedded in `scripts/setup_pdfium.sh` before extraction.
Custom platforms or download URLs must also provide the matching `PDFIUM_SHA256` value.

To inspect the resolved settings without downloading:

```bash
bash scripts/setup_pdfium.sh --print-config
```

To refresh the local copy:

```bash
bash scripts/setup_pdfium.sh --force
```

`third_party/pdfium/` is intentionally not committed because PDFium binaries are platform-specific and should be reproducible from CMake setup or the pinned setup script.

## ONNX Runtime and PaddleOCR baseline

PaddleOCR ONNX is the default OCR baseline. When ONNX Runtime support is enabled, CMake downloads the pinned ONNX Runtime package and the pinned PaddleOCR ONNX baseline models if they are missing.

The default build path is:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The downloaded runtime and models live outside git:

```text
third_party/onnxruntime-linux-x64-1.18.1/
models/paddleocr/baseline/
  det.onnx
  rec.onnx
  ppocrv5_dict.txt
```

The pinned setup scripts can also be run manually:

```bash
bash scripts/setup_onnxruntime.sh
bash scripts/setup_paddleocr_baseline.sh
```

ONNX Runtime is downloaded from an immutable release and verified with SHA256 before extraction. PaddleOCR
model and dictionary URLs contain immutable repository revisions, and every downloaded file is verified with
SHA256 before it replaces an existing model. When overriding a URL, override its corresponding `*_SHA256`
environment variable as well.

Automatic downloads can be disabled:

```bash
cmake -S . -B build \
  -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_ONNXRUNTIME=OFF \
  -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PADDLEOCR_BASELINE=OFF
```

To use custom ONNX Runtime or PaddleOCR model locations:

```bash
cmake -S . -B build \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime-linux-x64-1.18.1 \
  -DDOC_PARSER_PADDLEOCR_BASELINE_DIR=/path/to/paddleocr-baseline

# Runtime override, useful for quick experiments:
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_MODEL_DIR=/path/to/paddleocr-baseline
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DET_MODEL=/path/to/det.onnx
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_REC_MODEL=/path/to/rec.onnx
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DICT=/path/to/ppocrv5_dict.txt
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_PROFILE=ppocrv5_mobile

# Optional inference tuning:
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_REC_BATCH_SIZE=8
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_REC_MAX_WIDTH=2048
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DET_LIMIT_SIDE=960

# Optional end-to-end baseline image:
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_TEST_IMAGE=/path/to/text-image.png
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_EXPECT_TEXT=expected-substring
```

With ONNX Runtime enabled, CTest includes `paddle_ocr_onnx_baseline`. It loads the default models and runs
end-to-end detection, batched recognition, detection-only, and supplied-region recognition against a committed
test image. If the models are unavailable, the test is skipped with CTest skip code `77`.

The built-in `ppocrv4_mobile` and `ppocrv5_mobile` profiles are separate model contracts. The current official
mobile exports share the same BGR channel order and normalization values, while the public profile keeps those
fields model-scoped for future or custom exports. DB post-processing uses the official area/perimeter unclip
distance, recognition batches crops with similar aspect ratios, and dynamic-width ONNX models can grow from 320 to
the configured maximum width.

Angle classification is deliberately not advertised by this backend. Earlier configuration accepted a classifier
path but only loaded the session without executing it; that misleading option has been removed. Rotation support
should return as a complete preprocessing stage with its own model profile and evaluation corpus.

### Threading policy

PDFium is treated as a process-wide native dependency. The project keeps PDFium initialization in `PdfLibrary` and document ownership in `PdfReader`.

Current policy:

- Create one `PdfLibrary` near application startup.
- Keep `PdfLibrary` alive longer than all `PdfReader` objects.
- Do not share one `PdfReader` instance across threads.
- Direct PDFium calls are serialized inside the PDFium backend module.

This keeps the public API simple while leaving room for future batch processing or worker-process parallelism.

## stb_image_write

The first PDF rendering milestone uses [`stb_image_write.h`](https://github.com/nothings/stb) to write PNG files without adding a large image-processing dependency too early.

The vendored header lives at:

```text
third_party/stb/stb_image_write.h
```

The initial PDF-to-PNG path intentionally stays small and does not depend on OpenCV.

## OpenCV

Image preprocessing uses OpenCV 4.x with the `core`, `imgproc`, and `imgcodecs` components.

The project supports OpenCV 4.2 and newer. CI uses Ubuntu 24.04 with the distribution-provided
`libopencv-dev` package as the reference environment.

Install OpenCV through your system package manager:

```bash
# Ubuntu / Debian
bash scripts/setup_ubuntu_dependencies.sh

# Equivalent direct command:
sudo apt-get update && sudo apt-get install -y libopencv-dev

# macOS
brew install opencv
```

CMake detects OpenCV from the `cpp/image` module:

```cmake
find_package(OpenCV 4.2 REQUIRED COMPONENTS core imgproc imgcodecs)
```

If OpenCV is not installed, disable image preprocessing while configuring:

```bash
cmake -S . -B build -DDOCUMENT_INTELLIGENCE_ENGINE_ENABLE_OPENCV=OFF
```

OpenCV is treated as a system dependency rather than a vendored dependency. It is large, platform-specific,
and usually pulls additional image codec libraries from the host package manager. Keeping it external avoids
long source builds and large committed binaries while still letting CI pin a repeatable reference OS.

CMake deliberately does not run `sudo` or mutate the host package manager during configuration. On a fresh
Ubuntu/Debian machine, run the dependency setup script once before configuring the default build. Disabling
only image preprocessing is not enough when the PaddleOCR ONNX backend is enabled, because that backend also
uses OpenCV; disable both `DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_OPENCV` and
`DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_ONNXRUNTIME` for an OpenCV-free build.

Legacy `DOC_PARSER_AUTO_SETUP_PDFIUM`, `DOC_PARSER_ENABLE_PDFIUM`, and `DOC_PARSER_ENABLE_OPENCV` CMake options
are still accepted for compatibility.

## Tesseract OCR

OCR uses an optional Tesseract CLI adapter as the first baseline. The C++ pipeline checks both the executable and
the requested language packs at runtime. If PaddleOCR and Tesseract are both unavailable, `auto` mode keeps
native-text-only documents usable but returns an explicit error as soon as a page requires OCR. A non-zero
Tesseract process exit is also treated as OCR failure. Silent empty output is available only when the caller selects
`--ocr-backend noop` deliberately.

Install Tesseract through your system package manager or a user environment:

```bash
# Ubuntu / Debian
sudo apt-get install -y tesseract-ocr tesseract-ocr-eng tesseract-ocr-chi-sim

# Conda
conda install -c conda-forge tesseract
```

The default OCR language is `eng`. Override the executable or language with environment variables:

```bash
DOCUMENT_INTELLIGENCE_ENGINE_TESSERACT_CMD=/path/to/tesseract DOCUMENT_INTELLIGENCE_ENGINE_TESSERACT_LANG=eng+chi_sim ./build/cpp/app/document_intelligence_engine input.pdf --debug
```

The legacy `DOC_PARSER_TESSERACT_CMD` and `DOC_PARSER_TESSERACT_LANG` variables are still accepted for now.

The text extraction stage measures native text validity and vertical coverage. Empty or suspicious native text is
replaced by OCR. Sparse but usable native text triggers a full-page OCR pass whose non-overlapping lines are merged
by coordinates; overlapping OCR lines are discarded and `preferred_source` becomes `mixed`. If enhancement OCR
fails, usable native text is retained. Debug logs record the decision and quality measurements for every page.

Select the OCR backend explicitly with:

```bash
./build/cpp/app/document_intelligence_engine input.pdf --ocr-backend auto
./build/cpp/app/document_intelligence_engine input.pdf --ocr-backend tesseract
./build/cpp/app/document_intelligence_engine input.pdf --ocr-backend noop
```

## Layout Analysis

The default `auto` layout mode can use two pinned ONNX models. RF-DETR is trained on the DocLayNet 11-label
taxonomy, while the official Paddle PP-DocLayoutV3 backend provides an alternative 25-label taxonomy. Both downloads
use immutable revisions and SHA256 verification:

```bash
bash scripts/setup_doclaynet_layout.sh
bash scripts/setup_paddle_layout.sh
```

Automatic setup can be disabled independently with
`-DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_DOCLAYNET_LAYOUT=OFF` and
`-DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PADDLE_LAYOUT=OFF`. Custom model paths or runtime thresholds can be
selected with:

```bash
export DOCUMENT_INTELLIGENCE_ENGINE_DOCLAYNET_MODEL=/path/to/model.onnx
export DOCUMENT_INTELLIGENCE_ENGINE_DOCLAYNET_CONFIDENCE=0.5
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLE_LAYOUT_MODEL=/path/to/pp-doclayout-v3.onnx
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLE_LAYOUT_CONFIDENCE=0.5
```

The model uses RGB ImageNet normalization at `576x576` and emits `Caption`, `Footnote`, `Formula`, `List-item`,
`Page-footer`, `Page-header`, `Picture`, `Section-header`, `Table`, `Text`, and `Title`. These labels map into the
stable internal types as follows:

| DocLayNet labels | Internal type |
| --- | --- |
| `Title`, `Section-header` | `title` |
| `Text`, `Caption` | `text` |
| `List-item` | `list` |
| `Table` | `table` |
| `Picture` | `figure` |
| `Page-header` | `header` |
| `Page-footer`, `Footnote` | `footer` |
| `Formula` | `unknown` |

The original label remains available as `source_label`. Captions also carry `related_block_id` when a nearby
figure or table is found. Detected blocks are linked to normalized text lines by coordinate coverage.

`--layout-backend auto` uses the available chain `doclaynet -> paddle-layout -> text`; a model that is missing at
startup is omitted, and a per-page inference failure advances to the next backend. Use `--layout-backend doclaynet`
or `--layout-backend paddle-layout` for strict model-only execution, or `--layout-backend text` for the deterministic
rule backend.

Debug output records layout results under:

```text
pages[].debug.layout.blocks
```

Set `DOCUMENT_INTELLIGENCE_ENGINE_LAYOUT_DEBUG=1` to log input shape, threshold, block count, and inference errors.
The RF-DETR model repository declares its weights as MIT licensed, and Paddle PP-DocLayoutV3 is Apache-2.0.
DocLayNet corpus samples retain their CDLA-Permissive-1.0 terms.

## Table Recognition

The real table backend uses the Microsoft Table Transformer detection and structure-recognition models converted to
ONNX. The two model files use immutable Hugging Face revisions and SHA256 verification:

```bash
bash scripts/setup_table_transformer.sh
```

The models total approximately 221 MiB, so automatic configure-time setup is disabled by default. Enable it with
`-DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_TABLE_TRANSFORMER=ON`, or configure custom files and thresholds with:

```bash
export DOCUMENT_INTELLIGENCE_ENGINE_TABLE_DETECTION_MODEL=/path/to/detection.onnx
export DOCUMENT_INTELLIGENCE_ENGINE_TABLE_STRUCTURE_MODEL=/path/to/structure.onnx
export DOCUMENT_INTELLIGENCE_ENGINE_TABLE_DETECTION_CONFIDENCE=0.9
export DOCUMENT_INTELLIGENCE_ENGINE_TABLE_STRUCTURE_CONFIDENCE=0.5
export DOCUMENT_INTELLIGENCE_ENGINE_TABLE_CROP_PADDING=20
```

`--table-backend auto` selects Table Transformer when both models are present and falls back to the text-geometry
backend on startup or inference failure. `--table-backend table-transformer` is strict model-only mode and
`--table-backend text` selects the dependency-free fallback.

The backend detects table regions before reading order, expands each crop by a configurable margin, and predicts
rows, columns, column headers, projected row headers, and spanning cells. Row/column intersections form the cell
grid; spanning predictions become `row_span`/`column_span`, and normalized PDF/OCR tokens are assigned by geometry.
This supports borderless tables because grid recovery does not depend on visible ruling lines. Tables touching the
bottom and top of consecutive pages are linked with a continuation group when their horizontal bounds and column
counts agree.

Debug output records table results under:

```text
pages[].debug.tables.tables
```

Set `DOCUMENT_INTELLIGENCE_ENGINE_TABLE_DEBUG=1` to log thresholds, crop padding, detected regions, rows, columns,
cells, merged cells, and runtime errors. The default exporter writes `document.json`, `document.md`, and
`document.html`. HTML preserves `rowspan` and `colspan`; Markdown uses pipe tables for simple grids and embedded HTML
for merged cells. The Table Transformer weights are MIT licensed and the benchmark samples are
CDLA-Permissive-2.0.

## Pipeline Trace

Debug runs write a stage trace manifest to:

```text
debug/pipeline_trace.json
```

The trace records backend selection plus stage success/failure events for open, render, text extraction, layout,
table recognition, document assembly, and export.

## nlohmann/json

Structured parser output is written with [`nlohmann/json`](https://github.com/nlohmann/json), pulled by CMake `FetchContent`.

The current pinned version is:

```text
v3.11.3
```

The default manifest contains assembled document blocks:

```json
{
  "source": {
    "path": "input.pdf",
    "type": "pdf"
  },
  "render": {
    "dpi": 72
  },
  "blocks": [
    {
      "id": "doc_page_1_block_1",
      "type": "paragraph",
      "page_number": 1,
      "text": "Example"
    }
  ]
}
```

With `--debug`, the manifest also includes raw page text, layout blocks, table intermediate data, debug images,
and `debug/pipeline_trace.json`.
