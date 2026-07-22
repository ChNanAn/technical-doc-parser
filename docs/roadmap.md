# Roadmap

This roadmap describes the project as a Document Intelligence Engine, not as a finished OCR/Layout/Table product.

The current implementation has an end-to-end skeleton:

```text
PDF
  -> page rendering
  -> native text extraction / OCR fallback
  -> layout analysis
  -> reading order
  -> table recognition
  -> document assembly
  -> JSON manifest
```

That is an important milestone because the core data flow is now runnable and testable. It does not mean the intelligent stages are production-ready. OCR, layout analysis, table recognition, reading order, document assembly, and RAG output still need to become real, measurable modules.

## Current Status

Working today:

- C++17/CMake CLI.
- PDF backend using PDFium through a backend adapter.
- Page rendering to PNG artifacts.
- PDF text-layer extraction.
- Optional Tesseract CLI OCR baseline.
- OpenCV preprocessing baseline for debug images.
- DocLayNet RF-DETR ONNX layout baseline with a rule-based fallback.
- Docling-like reading-order baseline.
- Rule-based table baseline.
- Document assembly into `DocumentBlock`.
- JSON exporter with debug/raw artifacts.
- Unit and smoke tests for the main pipeline.
- Backend boundaries split from pipeline orchestration.

Important caveats:

- OCR is only a baseline adapter, not a robust OCR subsystem.
- Layout analysis is measured on a five-page DocLayNet regression subset, but broader domain validation is pending.
- Reading order is a heuristic baseline, not a measured reading-order model.
- Table recognition is a minimal text-gap baseline, not full table understanding.
- Markdown/HTML/RAG outputs are not yet mature.
- A small distributed OCR/layout/table metric corpus now exists; broader domain coverage and release thresholds are
  still missing.

## Design Principles

- Keep the pipeline backend-agnostic.
- Treat OCR, layout, table, VLM, and external parsers as replaceable capabilities.
- Normalize all provider outputs into stable typed document models.
- Preserve page numbers, bounding boxes, confidence, source backend, and debug artifacts.
- Prefer small stage contracts over large god interfaces.
- Make every stage independently testable and measurable.
- Keep the C++ core embeddable as a future SDK, not just a CLI demo.

## Major Workstreams

### Core Engine

Goal: keep the foundation clean as more providers are added.

- Pipeline orchestration.
- Backend registry and factory boundaries.
- Backend capability negotiation.
- Stage configuration from config files.
- `Status` / `Result<T>` error propagation.
- Trace and debug manifest.
- Stable artifact schemas.
- Batch processing primitives.

### Input Backends

Goal: support more document sources without changing the pipeline.

- PDF backend.
- Image backend for scanned pages.
- Office/Word backend.
- HTML or structured document backend.
- External parser output backend.
- VLM structured-output backend.

### Image Preprocessing

Goal: make OCR and layout inputs more reliable.

- Grayscale, binarization, denoising baseline.
- Deskew.
- Orientation detection.
- Border and noise cleanup.
- Page crop and content-region detection.
- Debug image artifacts for every preprocessing step.

### OCR

Goal: turn the measurable PaddleOCR baseline into a production OCR subsystem.

- Improve Tesseract baseline.
- Maintain PaddleOCR model profiles; add RapidOCR / docTR adapters where they add measurable value.
- Support language configuration.
- Track confidence and source per token/span/line.
- Normalize OCR word boxes into lines and spans.
- Calibrate native-text quality and coordinate-based OCR merge policies by document domain.
- Add orientation and domain-specific fixtures, then define release thresholds for the layered OCR metrics.

### Layout Analysis

Goal: identify document regions robustly.

- Calibrate the DocLayNet ONNX backend on broader domain-specific corpora.
- Keep the rule-based backend as a deterministic fallback.
- Support DocLayNet / PubLayNet-style labels.
- Detect titles, paragraphs, lists, tables, figures, captions, headers, footers, sidebars, footnotes.
- Extend multi-column handling to nested sidebars and floating figures.
- Improve text-line and caption-to-visual-region linking.
- Add per-domain layout release thresholds beyond the five-page regression set.

### Reading Order

Goal: model reading sequence as a first-class stage.

- Add `PageReadingOrder` / ordered element models.
- Detect columns and full-width blocks.
- Order blocks inside and across columns.
- Handle cross-page section continuity.
- Merge title, paragraph, list, figure caption, and table caption relationships.
- Identify repeated headers and footers.
- Add reading-order fixtures and metrics.

### Table Understanding

Goal: replace the current table demo with real table structure recovery.

- Table region detection.
- Table structure recognition.
- Borderless table support.
- Row, column, and cell recovery.
- Rowspan and colspan support.
- Table caption linking.
- Table-to-Markdown and table-to-HTML export.
- Table structure metrics.

### Document Assembly

Goal: produce a clean product-facing document model.

- Use reading order instead of raw layout order.
- Build section hierarchy.
- Represent paragraphs, lists, tables, figures, captions, headers, footers, and footnotes.
- Split public `ParsedDocument` from debug `PipelineArtifacts`.
- Preserve source references for every block.
- Prepare a Markdown AST / HTML AST layer.

### RAG Output

Goal: produce retrieval-friendly output, not just text dumps.

- Heading-aware chunks.
- Layout-aware chunks.
- Table-aware chunks.
- Figure/caption-aware chunks.
- Source citation metadata.
- Page and bbox references.
- Embedding-ready JSONL.
- Chunk quality evaluation.

### Model Backends

Goal: make model integration pluggable.

- ONNX Runtime backend.
- OpenVINO / TensorRT adapters where useful.
- Remote model service backend.
- VLM backend for structured extraction.
- Backend capability declaration.
- Model version and provider metadata in debug output.

### Evaluation

Goal: make progress measurable.

- Golden PDF/image fixtures.
- Snapshot tests for JSON/Markdown outputs.
- OCR accuracy metrics.
- Layout block matching metrics.
- Table structure metrics.
- Reading order metrics.
- End-to-end regression suite.
- Benchmark scripts and reports.

### Performance and Deployment

Goal: make the engine usable in batch and production-like environments.

- Page-level parallelism.
- Batch processing CLI mode.
- Memory profiling.
- CPU throughput benchmarks.
- Optional process isolation for native dependencies.
- Docker image.
- C++ SDK boundary.
- Python bindings as a downstream integration option.

## Near-Term Milestones

### Milestone 1: Harden the Current Skeleton

- Keep the current end-to-end flow passing.
- Make pipeline errors more structured.
- Keep provider-specific code outside the pipeline module.
- Improve debug manifest and trace output.
- Add small fixtures for scanned/image-only documents.

### Milestone 2: Real OCR and Layout Baselines

- Improve image preprocessing.
- Add one stronger OCR adapter beyond the current baseline.
- Add layout fixtures.
- Improve layout classes and block linking.
- Add basic OCR/layout metrics.

### Milestone 3: Reading Order and Assembly

- Improve the Docling-like heuristic reading-order backend.
- Replace the linear spatial index with an R-tree implementation if profiling shows it matters.
- Add more multi-column and mixed figure/table fixtures.
- Add reading-order metrics.
- Use ordered elements to improve section hierarchy and Markdown AST output.

### Milestone 4: Table Understanding

- Improve table detection and structure recovery.
- Add borderless table cases.
- Preserve row/column/cell source references.
- Add table snapshot and structure tests.

### Milestone 5: Markdown, HTML, and RAG Outputs

- Add Markdown AST.
- Add Markdown exporter.
- Add RAG chunking output.
- Preserve source citations in chunks.
- Add snapshot tests for generated outputs.

### Milestone 6: Model Backends and Evaluation

- Add ONNX Runtime wrapper.
- Add a layout/table model backend.
- Add optional VLM backend.
- Build a small public benchmark suite.
- Track regression metrics in CI or release notes.

## Good First Contribution Areas

- Add fixtures and expected JSON snapshots.
- Improve Tesseract OCR normalization.
- Add image preprocessing steps.
- Improve rule-based layout heuristics.
- Implement reading-order data models.
- Add Markdown exporter scaffolding.
- Add table fixtures and structure tests.
- Improve docs for backend boundaries.
- Add benchmark scripts.

## Project Positioning

The project should be described honestly:

```text
The end-to-end engine skeleton is running.
The intelligent stages are still baseline implementations.
The goal is to make OCR, layout, table, reading order, assembly, and RAG output pluggable, debuggable, and measurable.
```

This keeps expectations clear while making the project attractive to contributors who want to build the real modules.
