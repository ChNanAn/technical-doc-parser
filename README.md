# Technical Doc Parser

[中文文档](README.zh-CN.md)

Technical Doc Parser is a C++ native, backend-agnostic document parsing engine.

It is not positioned as another model-centric PDF-to-Markdown application. Its long-term value is the infrastructure around document parsing: a typed document model, stable pipeline boundaries, pluggable backends, traceable intermediate artifacts, reproducible C++ builds, and an SDK-friendly core that can be embedded into production systems.

AI models such as OCR engines, layout detectors, table recognizers, ONNX models, and vision-language models are treated as replaceable capabilities. The engine normalizes their outputs into one internal document model and keeps the rest of the pipeline stable.

Technical and table-heavy PDFs are the first proving ground. They keep the project grounded in real extraction requirements: source coordinates, section hierarchy, table structure, parameter/value/unit relationships, debug artifacts, and RAG-ready structured output.

## Goal

Build a production-oriented C++ document parsing infrastructure that can:

- Open and render documents through backend adapters.
- Normalize PDF text layers, OCR results, layout blocks, tables, and model outputs into typed internal models.
- Compose those models through a staged, inspectable pipeline.
- Export stable JSON/Markdown outputs for downstream applications.
- Preserve source traceability through page numbers, bounding boxes, confidence scores, and debug artifacts.
- Support CLI usage today and a C++ SDK/library boundary over time.

The current CLI shape is:

Input:

```bash
doc_parser input.pdf --out output/
```

Output:

```text
output/
  document.json
  document.md
  pages/
    page_1.png
    page_2.png
  debug/
```

Example JSON:

```json
{
  "pages": [
    {
      "page": 1,
      "blocks": [
        {
          "type": "title",
          "text": "Technical Specification",
          "bbox": [80, 40, 700, 90]
        },
        {
          "type": "table",
          "bbox": [90, 180, 760, 520],
          "data": [
            ["Parameter", "Value", "Unit"],
            ["Pressure", "1.6", "MPa"]
          ]
        }
      ]
    }
  ]
}
```

## Scope

The project scope is infrastructure-first:

- **Core engine**: C++17/CMake, RAII wrappers for native dependencies, stable typed models, pipeline orchestration, export contracts, tests, and CI.
- **Backend adapters**: PDFium, OCR engines, layout/table models, ONNX Runtime, vision-language models, and external parsers can be integrated behind narrow interfaces.
- **First domain focus**: technical and table-heavy PDFs, especially specifications, parameter tables, reports, standards, manuals, and structured tabular documents.
- **Reproducible evaluation**: public datasets and small curated technical fixtures should drive development before any domain-specific fine-tuning.

Model fine-tuning is a downstream option, not the center of the project. The first objective is to make the parsing pipeline reliable, extensible, testable, and deployable.

## Project Value

Many open-source document parsers focus on end-user conversion quality: turning documents into Markdown, HTML, JSON, or model-ready chunks. This project focuses on the lower-level parsing engine that makes those outputs reliable and extensible in C++ applications.

The project should optimize for:

- **Unified document model**: one typed representation for PDF text, OCR text, layout blocks, tables, reading order, source references, and final document structure.
- **Backend neutrality**: PDFium, OCR, layout detectors, table recognizers, external parsers, and VLMs should be replaceable without rewriting the pipeline.
- **C++ native deployment**: the core should work as a CLI, library, and future SDK for private/offline deployment.
- **Traceability**: extracted content must retain page number, bounding box, source backend, confidence, and debug artifacts.
- **Engineering quality**: reproducible builds, small module boundaries, RAII ownership, unit tests, smoke tests, and stable schemas matter as much as model accuracy.
- **Technical document depth**: technical/table-heavy PDFs are the first benchmark because they stress the engine with dense tables, coordinates, hierarchy, units, and structured extraction needs.

## Non-Goals

The project should avoid competing head-on as a broad, model-centric document parsing platform.

It does not aim to:

- Replace mature end-user document parsing applications.
- Own every OCR, layout, table, or VLM model.
- Become a Python-first training framework.
- Treat Markdown conversion as the main product.
- Hide intermediate decisions inside an opaque model-only pipeline.

Instead, it should be able to consume model/application outputs through adapters, normalize them, and provide a stable C++ engine for downstream products.

## Pipeline Boundaries

The parser is organized as a staged pipeline. Each stage should have a narrow responsibility and pass typed intermediate data to the next stage. Implementation details can evolve, but stage boundaries should stay stable.

The pipeline is intentionally backend-agnostic. PDFium, OCR engines, layout models, table recognizers, external parsers, and VLM services should enter the system through adapters and normalize their outputs into the same document models.

```text
document input
  -> source ingestion
  -> page rendering / page artifacts
  -> text extraction
  -> layout analysis
  -> table structure recovery
  -> document assembly
  -> export / SDK result
```

### Stage Responsibilities

**Source ingestion**

Owns backend setup and document access. The current implementation opens PDF files through PDFium, manages PDFium lifetime, reads page count and page metadata, and hides PDFium-specific resource management from the rest of the pipeline. Future source adapters can target other document backends or external parser outputs.

**Page rendering**

Converts PDF pages into page images with stable output paths and render settings. It does not perform OCR, layout detection, or table reconstruction.

Current output:

```text
pages/page_1.png
pages/page_2.png
```

**Text extraction**

Provides a unified text extraction interface. The pipeline should call one text extraction stage, not branch directly between PDFium, OCR, or future model-based extractors.

Current strategy:

```text
PDF text layer -> PageText
```

Future strategy:

```text
if PDF text layer is usable:
  use PDF text layer
else:
  use OCR
```

Both PDF text layer and OCR must normalize into the same internal text model:

```text
TextSpan -> TextLine -> PageText
```

This stage only provides text, coordinates, confidence, and source information. It does not decide whether a region is a title, paragraph, table, or figure.

**Layout analysis**

Classifies page regions into semantic block types such as:

```text
title
text
table
figure
header
footer
```

Layout consumes page images and normalized text. It should output layout blocks with bounding boxes and confidence scores. It should not reconstruct table cells or export Markdown.

**Table structure recovery**

Consumes table layout blocks and the text tokens that fall inside those blocks. This stage is responsible for rows, columns, cells, merged cells, and table reading order.

It should not care whether text came from the PDF text layer or OCR. It only consumes normalized text tokens and coordinates.

**Document assembly**

Combines layout blocks, text tokens, table structures, page metadata, and reading order into a structured document tree.

This is the first stage that should create user-facing document structure:

```text
pages
  blocks
    title/text/table/figure
```

**Export / SDK result**

Writes the final consumer-facing outputs:

```text
document.json
document.md
```

Export should not expose raw intermediate data by default. Intermediate values such as `PageText`, OCR boxes, layout proposals, and table debug cells should be preserved only in debug mode or explicit diagnostic outputs.

### Intermediate Data Policy

Intermediate models are for communication between stages, not the public output contract.

Examples:

```text
PageText      text extraction -> layout/table
LayoutBlock   layout -> document assembly/table
TableCell     table recovery -> document assembly/export
```

Normal output should be the assembled document result. Debug output can include intermediate data to support inspection and regression tests.

### Module Layout

Planned module layout:

```text
cpp/
  app/          CLI entrypoint
  pipeline/     pipeline orchestration and stage interfaces
  document/     shared internal document models
  pdf/          PDFium-based PDF access, rendering, and PDF text extraction
  image/        OpenCV preprocessing
  ocr/          OCR adapters and text normalization
  layout/       layout block detection
  table/        table structure recovery
  inference/    ONNX Runtime inference wrappers
  export/       JSON and Markdown writers

python/
  ocr_train/     OCR experiments and fine-tuning
  layout_train/  layout model training
  export_onnx/   model export scripts

data/          dataset adapters and small samples
models/        local model files, not committed
docs/          design notes and evaluation reports
docker/        deployment assets
tests/         unit and integration tests
```

## Public Datasets

The project is designed around public datasets so the work can be reproduced:

- [DocLayNet](https://github.com/DS4SD/DocLayNet): layout detection dataset with document categories such as manuals, patents, tenders, financial reports, laws, and scientific articles.
- [PubTables-1M](https://github.com/microsoft/table-transformer): large-scale table detection and table structure recognition dataset.
- [FUNSD](https://guillaumejaume.github.io/FUNSD/): small scanned form understanding dataset for OCR, entities, and relations.

For demos and targeted evaluation, the project can also use a small curated set of public technical PDFs.

## Implementation Milestones

### Phase 1: PDF Ingestion

- Maintain a reproducible C++17/CMake build.
- Integrate PDFium through a pinned setup script.
- Open PDF documents and inspect page metadata.
- Render pages to image files.
- Produce a minimal JSON/Markdown export.

### Phase 2: Image Preprocessing

- Add OpenCV preprocessing.
- Implement grayscale conversion, binarization, denoising, and deskewing.
- Preserve intermediate debug artifacts for inspection and regression tests.

### Phase 3: OCR and Text Reconstruction

- Integrate an OCR baseline.
- Normalize words, lines, confidence scores, and bounding boxes.
- Reconstruct reading order and merge OCR text into page-level structures.

### Phase 4: Layout Analysis

- Use DocLayNet for layout detection experiments.
- Normalize layout regions into title, text, table, figure, list, header, and footer blocks.
- Combine layout regions with OCR text.

### Phase 5: Table Structure Recovery

- Use PubTables-1M or Table Transformer as a table baseline.
- Implement a rule-based `TableStructureBuilder` for line-based and aligned-text tables.
- Export tables as JSON and Markdown.

### Phase 6: Deployment and Evaluation

- Export models to ONNX where applicable.
- Add C++ ONNX Runtime wrappers.
- Add benchmark scripts and performance reports.
- Add Docker packaging and optional HTTP/gRPC service.

## Current Status

Early implementation. The project currently has a C++17/CMake CLI, pinned PDFium setup, PDFium lifetime management, PDF open/page-count support, page rendering, an internal text model, PDF text layer extraction, an optional Tesseract OCR baseline, a baseline layout analysis model, and smoke tests for the main pipeline pieces.

The current pipeline is intentionally small:

```text
PDF -> rendered pages -> PageText -> PageLayout -> minimal manifest
```

Next implementation work should keep the same stage boundaries while improving layout detection quality, adding table structure recovery, and building a dedicated export layer.

## Build

Configure and build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target doc_parser --parallel
./build/cpp/app/doc_parser input.pdf --out output/
```

PDFium is downloaded automatically during CMake configure when it is missing. The pinned package is installed under `third_party/pdfium`, which is not committed to git.

## Development

- Dependency setup notes: [docs/dependencies.md](docs/dependencies.md)
- Commit message convention: [docs/commit-convention.md](docs/commit-convention.md)
- Development plan: [docs/roadmap.md](docs/roadmap.md)

## License

This project is licensed under the [MIT License](LICENSE).
