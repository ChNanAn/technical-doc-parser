# Development Plan

This document turns the project goal into an execution plan. The main rule is to keep a working, reviewable milestone at the end of every phase instead of waiting for a large final integration.

## Long-Term Goal

Build a technical and table-heavy document intelligence engine:

```text
PDF
  -> page images
  -> preprocessing
  -> OCR
  -> layout detection
  -> table extraction
  -> document tree
  -> JSON / Markdown
```

Target command:

```bash
document_intelligence_engine input.pdf --out output/ --dpi 200 --debug
```

Target output:

```text
output/
  document.json
  document.md
  pages/
    page_1.png
    page_2.png
  debug/
```

## Milestone Plan

### Month 1: PDF Pipeline Foundation

Goal: build the C++ foundation and render PDF pages into images.

Core work:

- Keep the CMake, CLI11, PDFium, and CI setup reproducible.
- Open PDF documents with PDFium.
- Count pages.
- Render pages to PNG.
- Export a minimal `document.json` and `document.md`.

Month-end demo:

```bash
./build/cpp/app/document_intelligence_engine sample.pdf --out output --dpi 200 --debug
```

Expected output:

```text
output/
  document.json
  document.md
  pages/
    page_1.png
    page_2.png
  debug/
```

### Month 2: Image Preprocessing

Goal: improve page images before OCR and layout analysis.

Core work:

- Add OpenCV.
- Convert pages to grayscale.
- Add binarization.
- Add denoising.
- Add deskewing.
- Save debug images for each preprocessing stage.

Expected debug output:

```text
output/debug/
  page_1_gray.png
  page_1_binary.png
  page_1_denoised.png
  page_1_deskewed.png
```

### Month 3: OCR Integration and Text Reconstruction

Goal: connect OCR output to the document pipeline.

Core work:

- Start with PaddleOCR or Tesseract as a baseline.
- Normalize OCR output into a stable internal schema.
- Merge words into lines and text blocks.
- Implement reading-order reconstruction.
- Export OCR results into JSON.

Example output:

```json
{
  "words": [
    {
      "text": "Pressure",
      "bbox": [100, 120, 180, 145],
      "score": 0.98
    }
  ]
}
```

### Month 4: Layout Analysis

Goal: identify document structure such as titles, text blocks, tables, figures, headers, and footers.

Core work:

- Use DocLayNet as the primary layout dataset.
- Train or evaluate a layout baseline.
- Normalize layout results into block objects.
- Merge OCR text into layout blocks.

Example block:

```json
{
  "type": "title",
  "bbox": [80, 40, 700, 90],
  "text": "Technical Specification"
}
```

### Month 5: Table Structure Recovery

Goal: convert detected table regions into structured data.

Core work:

- Use PubTables-1M or Table Transformer as the table baseline.
- Implement an OpenCV-based table line detector.
- Implement a rule-based `TableStructureBuilder`.
- Recover rows, columns, cells, and cell text.
- Export tables as JSON and Markdown.

Example table:

```json
{
  "type": "table",
  "data": [
    ["Parameter", "Value"],
    ["Pressure", "1.6 MPa"]
  ]
}
```

### Month 6: Deployment and Performance

Goal: make the system deployable and measurable.

Core work:

- Export models to ONNX.
- Add C++ ONNX Runtime inference wrappers.
- Add benchmark scripts.
- Compare Python and C++ pipeline speed.
- Add Docker packaging.
- Optionally add an HTTP or gRPC service.

Example benchmark:

```text
Python pipeline: 100 pages / xx s
C++ ONNX:       100 pages / xx s
```

## Month 1 Daily Plan

### Day 1: Repository Baseline

Tasks:

- Confirm `main` is clean.
- Confirm GitHub Actions runs.
- Confirm local git identity is correct.
- Commit and push the PDFium dependency setup.

Checks:

```bash
git status
git log --oneline -5
```

### Day 2: PDFium Dependency Verification

Tasks:

- Run the PDFium setup script.
- Confirm `third_party/pdfium/` contains headers, library, and `PDFiumConfig.cmake`.
- Confirm CMake can find PDFium.

Checks:

```bash
bash scripts/setup_pdfium.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPDFium_DIR=third_party/pdfium
cmake --build build --config Release --parallel
```

### Day 3: PdfReader Interface

Tasks:

- Add `cpp/backend/pdf/pdfium/pdf_reader.h`.
- Add `cpp/backend/pdf/pdfium/pdf_reader.cpp`.
- Define the first `PdfReader` API.

Initial API:

```cpp
class PdfReader {
public:
    PdfReader();
    ~PdfReader();

    bool open(const std::string& path);
    int pageCount() const;
};
```

### Day 4: PDFium Library Lifetime

Tasks:

- Wrap `FPDF_InitLibrary()`.
- Wrap `FPDF_DestroyLibrary()`.
- Prefer RAII so initialization and cleanup are deterministic.

Suggested files:

```text
cpp/backend/pdf/pdfium/
  pdf_library.h
  pdf_library.cpp
  pdf_reader.h
  pdf_reader.cpp
```

### Day 5: Open PDF Documents

Tasks:

- Implement `PdfReader::open(path)`.
- Use `FPDF_LoadDocument`.
- Store `FPDF_DOCUMENT`.
- Close it in the destructor with `FPDF_CloseDocument`.

Expected behavior:

```text
error: failed to open PDF: sample.pdf
```

### Day 6: Page Count

Tasks:

- Implement `PdfReader::pageCount()`.
- Print page count from the CLI path.
- Keep error handling clear.

Expected output:

```text
input: sample.pdf
pages: 3
```

### Day 7: Week 1 Review

Tasks:

- Ensure CI is green.
- Commit the PDF open/page count work.
- Update notes if the implementation differs from the plan.

Week 1 target:

```text
The CLI can open a PDF and print its page count.
```

### Day 8: Render API Design

Tasks:

- Add a render method to `PdfReader`.
- Use zero-based page indexes internally.
- Use one-based filenames in output.

Proposed API:

```cpp
bool renderPageToPng(int page_index, int dpi, const std::string& output_path);
```

### Day 9: Render PDFium Bitmap

Tasks:

- Use `FPDF_LoadPage`.
- Compute pixel width and height from DPI.
- Use `FPDFBitmap_Create`.
- Use `FPDF_RenderPageBitmap`.
- Confirm bitmap dimensions are valid.

### Day 10: PNG Writer Choice

Tasks:

- Use a small PNG writer before introducing OpenCV.
- Prefer `stb_image_write.h` for the first renderer.
- Record dependency source and license.

Decision:

```text
Use PDFium + stb_image_write for the first PDF-to-PNG demo.
Add OpenCV in Month 2.
```

### Day 11: Write Page PNG

Tasks:

- Convert PDFium bitmap data to a PNG-compatible format.
- Handle BGRA/RGBA channel order.
- Write `page_1.png`.

Check:

```bash
./build/cpp/app/document_intelligence_engine sample.pdf --out output
ls output/pages/
```

### Day 12: Render All Pages

Tasks:

- Create `output/pages/`.
- Loop through all pages.
- Write `page_1.png`, `page_2.png`, etc.

Expected output:

```text
output/pages/page_1.png
output/pages/page_2.png
```

### Day 13: Connect CLI Options

Tasks:

- Make `--dpi` affect render scale.
- Make `--out` affect output directory.
- Print useful progress logs.

Expected output:

```text
input: sample.pdf
pages: 2
dpi: 200
wrote output/pages/page_1.png
```

### Day 14: Week 2 Demo

Tasks:

- Run the renderer on a small public PDF.
- Do not commit large PDF samples.
- Add sample instructions instead of large binary files.

Week 2 target:

```text
The CLI can render every page of a PDF to PNG.
```

### Day 15: Error Handling

Tasks:

- Handle PDF page load failures.
- Handle output directory failures.
- Handle PNG write failures.
- Keep CLI errors readable.

### Day 16: Path Utilities

Tasks:

- Centralize output path creation.
- Generate page image filenames consistently.
- Avoid path string duplication in `main.cpp`.

### Day 17: Smoke Tests

Tasks:

- Add a CLI smoke test script.
- Keep `tests/check_pdfium_setup_script.sh`.
- Add render smoke tests once a minimal PDF fixture exists.

### Day 18: CI Render Test

Tasks:

- Add a tiny PDF fixture or generate one in a script.
- Run `document_intelligence_engine` in CI.
- Assert `output/pages/page_1.png` exists.

Example check:

```bash
./build/cpp/app/document_intelligence_engine tests/fixtures/minimal.pdf --out output
test -f output/pages/page_1.png
```

### Day 19: README Build Instructions

Tasks:

- Replace planned build instructions with real commands.
- Include PDFium setup.
- Include a minimal run example.

### Day 20: Commit Cleanup

Suggested commits:

```text
build(pdfium): add pinned dependency setup
feat(pdf): open PDF documents
feat(pdf): render pages to PNG
test(pdf): add render smoke test
docs: update build instructions
```

### Day 21: Week 3 Review

Tasks:

- Confirm GitHub Actions is green.
- Confirm the demo works from a clean checkout.
- Avoid adding new features before the render path is stable.

Week 3 target:

```text
PDF rendering is stable and covered by CI.
```

### Day 22: Main Cleanup

Tasks:

- Keep `main.cpp` small.
- Move CLI config into dedicated files if needed.

Suggested files:

```text
cpp/app/
  cli_options.h
  cli_options.cpp
  main.cpp
```

### Day 23: Pipeline Skeleton

Tasks:

- Add a first pipeline object.
- Move document processing out of `main.cpp`.

Suggested API:

```cpp
class DocumentPipeline {
public:
    bool run(const CliOptions& options);
};
```

### Day 24: Minimal JSON Output

Tasks:

- Write `output/document.json`.
- Include input path, page count, DPI, and page image paths.

First schema:

```json
{
  "input": "sample.pdf",
  "page_count": 2,
  "pages": [
    {
      "page": 1,
      "image": "pages/page_1.png"
    }
  ]
}
```

### Day 25: JSON Dependency

Tasks:

- Use `nlohmann/json` instead of hand-writing JSON.
- Add it through CMake `FetchContent`.
- Keep dependency version pinned.

### Day 26: Minimal Markdown Output

Tasks:

- Write `output/document.md`.
- Include page references.

First markdown:

```md
# Parsed Document

- Page 1: pages/page_1.png
- Page 2: pages/page_2.png
```

### Day 27: Debug Directory Contract

Tasks:

- If `--debug` is set, create `output/debug/`.
- Do not add preprocessing yet.
- Reserve the directory for Month 2 intermediate images.

### Day 28: Integration Test

Tasks:

- Run the full command.
- Verify PNG, JSON, and Markdown outputs.
- Make the check script repeatable.

### Day 29: Documentation Cleanup

Tasks:

- Update README current status.
- Update dependency docs if needed.
- Add a short Month 1 demo note.

### Day 30: Month 1 Demo

Final command:

```bash
bash scripts/setup_pdfium.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPDFium_DIR=third_party/pdfium
cmake --build build --config Release --parallel
./build/cpp/app/document_intelligence_engine sample.pdf --out output --dpi 200 --debug
```

Final output:

```text
output/
  document.json
  document.md
  pages/
    page_1.png
    page_2.png
  debug/
```

## Month 1 Completion Criteria

The month is complete when all of these are true:

- GitHub Actions is green.
- PDFium setup is reproducible from scripts.
- PDFium binaries are not committed.
- The CLI can open PDF files.
- The CLI can render every page to PNG.
- The CLI writes a minimal `document.json`.
- The CLI writes a minimal `document.md`.
- README has real build and run instructions.

## Month 1 Non-Goals

Do not work on these in Month 1:

- OpenCV preprocessing
- deskewing
- OCR
- LayoutLMv3
- table recognition
- ONNX Runtime
- Docker deployment

Those topics are intentionally deferred so the PDF foundation stays clean and demonstrable.
