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

# Optional angle classifier model:
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_CLS_MODEL=/path/to/cls.onnx

# Optional end-to-end baseline image:
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_TEST_IMAGE=/path/to/text-image.png
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_EXPECT_TEXT=expected-substring
```

With ONNX Runtime enabled, CTest includes `paddle_ocr_onnx_baseline`. The test creates ONNX Runtime sessions for the default PaddleOCR models. If `DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_TEST_IMAGE` is set, it also runs one end-to-end OCR pass and optionally checks `DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_EXPECT_TEXT`. If the models are unavailable, the test is skipped with CTest skip code `77`.

The current PaddleOCR ONNX backend provides a baseline inference path: image loading, DB-style detection preprocessing, contour-based text box extraction, recognition preprocessing, greedy CTC decoding, and OCR `PageText` assembly. It is intentionally conservative and leaves angle-classifier execution, more faithful DB unclip logic, batching, and model-specific tuning as follow-up work.

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

OCR uses an optional Tesseract CLI adapter as the first baseline. The C++ pipeline detects `tesseract` at
runtime. If it is available, image-only pages can be normalized into `PageText`; if it is missing, the OCR
service falls back to the no-op backend.

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

OCR is only used by the text extraction stage when the PDF text layer for a page is empty. Debug output records
OCR text under `pages[].debug.text` with `preferred_source` set to `ocr`.

Select the OCR backend explicitly with:

```bash
./build/cpp/app/document_intelligence_engine input.pdf --ocr-backend auto
./build/cpp/app/document_intelligence_engine input.pdf --ocr-backend tesseract
./build/cpp/app/document_intelligence_engine input.pdf --ocr-backend noop
```

## Layout Analysis

The first layout stage uses a built-in baseline model that consumes normalized `PageText` lines and page image
metadata. It groups text lines into `PageLayout` blocks and labels them as `title`, `text`, `list`, `table`,
`figure`, `header`, or `footer`.

This baseline has no external runtime dependency. It is intentionally shaped like a model adapter so a DocLayNet,
ONNX, or external detector backend can replace it without changing the rest of the pipeline.

Debug output records layout results under:

```text
pages[].debug.layout.blocks
```

## Table Recognition

The first table stage uses a built-in baseline recognizer. It consumes `PageText` plus table layout blocks from
`PageLayout`, then recovers simple rows and cells by grouping text spans and splitting large horizontal gaps.

This baseline has no external runtime dependency. It is intentionally shaped like a backend adapter so a
Table Transformer, PubTables-style model, ONNX backend, or external table service can replace it while preserving
the same `PageTables` output model.

Debug output records table results under:

```text
pages[].debug.tables.tables
```

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
