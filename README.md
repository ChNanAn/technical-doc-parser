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

## Architecture

```text
PDF
  -> page rendering
  -> image preprocessing
  -> OCR
  -> layout detection
  -> table extraction
  -> document tree
  -> JSON / Markdown
```

Planned module layout:

```text
cpp/
  app/          CLI entrypoint
  pdf/          PDFium-based PDF rendering
  image/        OpenCV preprocessing
  ocr/          OCR result normalization
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

Early implementation. The project currently has a C++17/CMake CLI, pinned PDFium setup, PDFium lifetime management, PDF open/page-count support, and CI smoke tests. The next milestone is page rendering to image files.

## Build

Build instructions will be added after the initial CMake target and dependencies are wired.

Planned local build:

```bash
cmake -S . -B build
cmake --build build
./build/doc_parser input.pdf --out output/
```

## Development

- Dependency setup notes: [docs/dependencies.md](docs/dependencies.md)
- Commit message convention: [docs/commit-convention.md](docs/commit-convention.md)
- Development plan: [docs/roadmap.md](docs/roadmap.md)

## License

This project is licensed under the [MIT License](LICENSE).
