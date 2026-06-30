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
cmake -S . -B build -DDOC_PARSER_AUTO_SETUP_PDFIUM=OFF
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

To inspect the resolved settings without downloading:

```bash
bash scripts/setup_pdfium.sh --print-config
```

To refresh the local copy:

```bash
bash scripts/setup_pdfium.sh --force
```

`third_party/pdfium/` is intentionally not committed because PDFium binaries are platform-specific and should be reproducible from CMake setup or the pinned setup script.

### Threading policy

PDFium is treated as a process-wide native dependency. The project keeps PDFium initialization in `PdfLibrary` and document ownership in `PdfReader`.

Current policy:

- Create one `PdfLibrary` near application startup.
- Keep `PdfLibrary` alive longer than all `PdfReader` objects.
- Do not share one `PdfReader` instance across threads.
- Direct PDFium calls are serialized inside the PDF module.

This keeps the public API simple while leaving room for future batch processing or worker-process parallelism.

## stb_image_write

The first PDF rendering milestone uses [`stb_image_write.h`](https://github.com/nothings/stb) to write PNG files without adding a large image-processing dependency too early.

The vendored header lives at:

```text
third_party/stb/stb_image_write.h
```

OpenCV will be introduced later for preprocessing, but the initial PDF-to-PNG path intentionally stays small.

## nlohmann/json

Structured parser output is written with [`nlohmann/json`](https://github.com/nlohmann/json), pulled by CMake `FetchContent`.

The current pinned version is:

```text
v3.11.3
```

The first manifest is intentionally small:

```json
{
  "source": {
    "path": "input.pdf",
    "type": "pdf"
  },
  "render": {
    "dpi": 72
  },
  "pages": [
    {
      "page_index": 0,
      "page_number": 1,
      "image": "pages/page_1.png"
    }
  ]
}
```

OCR, layout, and table fields will be added by later pipeline stages.
