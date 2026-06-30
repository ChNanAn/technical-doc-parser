# Technical Doc Parser

A C++/Python document intelligence engine for parsing technical and table-heavy PDFs into structured JSON and Markdown.

The project focuses on the engineering pipeline behind document AI: PDF rendering, image preprocessing, OCR integration, layout analysis, table structure recovery, structured export, and C++ inference deployment.

## Goal

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

This repository targets technical and table-heavy PDFs, especially documents that contain specifications, parameter tables, reports, standards, and other structured tabular data.

The core layout, table, and OCR stages are intended to be developed and evaluated with public datasets first. Domain-specific technical document behavior is expected to come from schema design, table/parameter post-processing, traceable output, and a small curated demo set rather than from a large proprietary industrial dataset.

The first objective is to build a reliable end-to-end parsing system. Model fine-tuning is part of the long-term plan, but the project prioritizes measurable engineering progress: reproducible pipelines, structured outputs, evaluation scripts, and deployable C++ components.

## Project Focus

This project is not intended to be a general-purpose document parsing platform. Its long-term value is in a narrower, engineering-focused direction:

- **Technical and table-heavy PDFs**: optimize for documents with specifications, parameter tables, reports, standards, and dense structured data instead of trying to cover every document category equally.
- **Structured technical data**: recover sections, parameter names, values, units, conditions, table headers, merged cells, and source page coordinates rather than only producing plain Markdown.
- **C++ native core**: keep the parsing core suitable for CLI use, library embedding, private deployment, and future ONNX Runtime inference.
- **Explainable pipeline**: preserve intermediate artifacts such as rendered pages, preprocessing outputs, OCR boxes, layout regions, table cells, reading order, and debug overlays.
- **RAG-ready output**: generate metadata-rich JSON and Markdown chunks that retain section hierarchy, table structure, page numbers, and bounding boxes for retrieval and question-answering workflows.

The goal is to build a focused technical PDF structure engine: smaller in scope than broad document parsing platforms, but deeper in table reconstruction, parameter-oriented post-processing, traceability, and native deployment.

## Pipeline Boundaries

The parser is organized as a staged pipeline. Each stage should have a narrow responsibility and pass typed intermediate data to the next stage. Implementation details can evolve, but stage boundaries should stay stable.

```text
PDF input
  -> PDF ingestion
  -> page rendering
  -> text extraction
  -> layout analysis
  -> table structure recovery
  -> document assembly
  -> export
```

### Stage Responsibilities

**PDF ingestion**

Owns PDF backend setup and document access. This stage opens the PDF, manages PDFium lifetime, reads page count and page metadata, and hides PDFium-specific resource management from the rest of the pipeline.

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

**Export**

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

Early implementation. The project currently has a C++17/CMake CLI, pinned PDFium setup, PDFium lifetime management, PDF open/page-count support, page rendering, an internal text model, PDF text layer extraction, and smoke tests for the main pipeline pieces.

The current pipeline is intentionally small:

```text
PDF -> rendered pages -> PageText -> minimal manifest
```

Next implementation work should keep the same stage boundaries while adding layout blocks, table structure recovery, and a dedicated export layer.

## Build

Configure and build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
./build/doc_parser input.pdf --out output/
```

PDFium is downloaded automatically during CMake configure when it is missing. The pinned package is installed under `third_party/pdfium`, which is not committed to git.

## Development

- Dependency setup notes: [docs/dependencies.md](docs/dependencies.md)
- Commit message convention: [docs/commit-convention.md](docs/commit-convention.md)
- Development plan: [docs/roadmap.md](docs/roadmap.md)

## License

This project is licensed under the [MIT License](LICENSE).
