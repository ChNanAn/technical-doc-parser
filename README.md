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

This repository targets technical and table-heavy documents such as:

- technical manuals
- patents
- tenders
- reports
- specifications
- scientific PDFs
- table-heavy enterprise documents

The first objective is to build a reliable end-to-end parsing system. Model fine-tuning is part of the roadmap, but the project prioritizes measurable engineering progress: reproducible pipelines, structured outputs, evaluation scripts, and deployable C++ components.

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

For demos, the project can also use a small collection of public technical manuals or specifications.

## Roadmap

### Month 1: PDF and Image Pipeline

- Create a C++17/CMake project.
- Render PDF pages with PDFium.
- Save each page as an image.
- Add OpenCV preprocessing:
  - grayscale conversion
  - binarization
  - denoising
  - deskewing
- Produce debug images for each processing stage.

### Month 2: Layout Baseline

- Add a DocLayNet data adapter.
- Train or evaluate a layout detection baseline.
- Normalize layout output into document blocks:
  - title
  - text
  - table
  - figure
  - list
  - header/footer

### Month 3: OCR Integration

- Integrate an OCR baseline.
- Normalize OCR output into words, lines, and blocks.
- Implement reading-order reconstruction.
- Merge OCR text with detected layout regions.

### Month 4: Table Extraction

- Add table detection and structure recovery.
- Evaluate PubTables-1M or Table Transformer baselines.
- Implement a rule-based `TableStructureBuilder` for line-based and aligned-text tables.
- Export tables as JSON and Markdown.

### Month 5: Document Tree and Export

- Combine layout, OCR, and table results into a single document tree.
- Export structured JSON.
- Export readable Markdown.
- Add evaluation scripts and sample reports.

### Month 6: C++ Inference and Deployment

- Export trained models to ONNX.
- Add C++ ONNX Runtime wrappers.
- Benchmark Python vs C++ inference.
- Add Docker packaging and optional HTTP/gRPC service.

## Current Status

Early project scaffold. The first milestone is a minimal C++ CLI that can render PDF pages and write them to an output directory.

## Build

Build instructions will be added after the initial CMake target and dependencies are wired.

Planned local build:

```bash
cmake -S . -B build
cmake --build build
./build/doc_parser input.pdf --out output/
```

## License

License is not selected yet.
