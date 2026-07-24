# Document Intelligence Engine

[中文](README.zh-CN.md)

A C++ native, backend-agnostic document intelligence engine for structured document parsing, starting with technical and table-heavy PDFs.

The project combines native text extraction, OCR, layout analysis, reading order, table structure recognition, and document assembly behind typed, replaceable backend interfaces. It produces JSON, Markdown, HTML, page images, and optional debug artifacts for downstream applications.

> This is an early, runnable, and measurable engine, not a finished enterprise document product. Committed datasets
> are small regression sets, and external benchmark scores should not be interpreted as production accuracy claims.

## Why This Project

- **C++ native**: designed for offline, private, and embedded deployment.
- **Backend-agnostic**: PDF, OCR, layout, and table providers can evolve without rewriting the pipeline.
- **Structured output**: text, blocks, tables, reading order, page numbers, bounding boxes, and confidence are represented as typed data.
- **Inspectable and measurable**: intermediate artifacts, public fixtures, metrics, smoke tests, and CI are part of the engine.
- **Embeddable SDK**: `DocumentEngine` reuses model sessions across parses and returns typed results independently of export format.

## Pipeline

```text
Document
  -> Render
  -> Native Text / OCR
  -> Layout
  -> Table Structure
  -> Reading Order
  -> Document Assembly
  -> JSON / Markdown / HTML
```

Current backends include:

- PDFium for PDF access, rendering, and native text extraction.
- PaddleOCR ONNX and Tesseract for OCR.
- RF-DETR DocLayNet, Paddle PP-DocLayoutV3, and a deterministic text-layout fallback.
- Table Transformer and a deterministic text-table fallback.
- A Docling-like reading-order baseline.

Models and providers are adapters. The long-lived project boundary is the normalized document model and staged pipeline.

## Quick Start

The reference environment is Ubuntu 24.04. Install system dependencies, then configure the release preset:

```bash
bash scripts/setup_ubuntu_dependencies.sh
cmake --preset core-release
cmake --build --preset core-release --parallel
ctest --preset core-release
```

PDFium, ONNX Runtime, and pinned baseline models are downloaded and verified during configuration when missing. See [Dependency Setup](docs/dependencies.md) for custom paths, optional downloads, and lightweight build options.

Parse a document:

```bash
./build/core-release/cpp/app/document_intelligence_engine input.pdf --out output/
```

Select backends explicitly or use the versioned registry configuration:

```bash
./build/core-release/cpp/app/document_intelligence_engine input.pdf --out output/ \
  --ocr-backend auto \
  --layout-backend auto \
  --table-backend auto \
  --backend-config config/backends.json
```

### C++ Embedding

Install the library and versioned CMake package:

```bash
cmake --install build/core-release --prefix build/sdk
```

Downstream projects can call `find_package(DocumentIntelligenceEngine CONFIG REQUIRED)` and link
`DocumentIntelligenceEngine::engine`. `DocumentEngine::parse()` returns the document and pipeline artifacts in memory;
JSON, Markdown, and HTML export is an explicit caller choice. See the runnable [embedding example](examples/embed_document.cpp)
and its standalone [CMake project](examples/CMakeLists.txt).

PDFium and, when enabled, ONNX Runtime remain external package dependencies. Set `PDFium_DIR` and
`ONNXRuntime_ROOT` when configuring a downstream project if they are not installed in standard locations. Model
files are runtime data; SDK applications should provide their paths through `EngineConfig`.

## Output

```text
output/
  document.json
  document.md
  document.html
  pages/
    page_1.png
    page_2.png
  debug/                 # with --debug
```

The normal JSON output contains assembled document blocks and page artifacts. `--debug` additionally includes normalized text, layout blocks, reading order, table structures, and preprocessing artifacts.

```json
{
  "source": {"path": "input.pdf", "type": "pdf"},
  "render": {"dpi": 200},
  "blocks": [
    {
      "id": "doc_page_1_block_1",
      "type": "paragraph",
      "page_number": 1,
      "bbox": {"x0": 84.0, "y0": 132.0, "x1": 742.0, "y1": 168.0},
      "confidence": 0.92,
      "text": "Technical specification"
    }
  ]
}
```

The first public document contract is available as a release candidate: [Document Contract v1](docs/document-contract-v1.md). It freezes an extensible result envelope and coordinate semantics; actual output quality is governed separately by three-layer evaluation. The existing exporter has not migrated to it yet.

## Evaluation

The repository contains redistributable OCR, layout, and table fixtures with backend-independent evaluators. The full model build includes real PaddleOCR, DocLayNet, Paddle Layout, and Table Transformer inference regressions.

The committed model datasets are intentionally small. They protect preprocessing, inference, label mapping, and postprocessing from regressions; broader technical-document validation is still required.

The first full external run scores `44.2% +/- 0.9%` on 1,403-page
[olmOCR-Bench](docs/benchmarks/olmocr-bench.md), with all strengths and failures published by category.

See [Evaluation](docs/evaluation.md) and the [Benchmark Guide](tests/benchmark/README.md) for datasets, metrics, commands, and current limitations.

## Optional Inspection Platform

The standalone C++ engine is the default deliverable. An optional platform under [`platform/`](platform/README.md) adds:

- FastAPI upload and run APIs.
- Redis Streams job delivery.
- A persistent C++ worker with stage events.
- PostgreSQL run metadata.
- A React interface for backend selection and artifact inspection.

```bash
cmake --preset platform-release
cmake --build --preset platform-release --target document_intelligence_worker --parallel
docker compose -f platform/deploy/docker-compose.yml up --build
```

The platform is also early-stage. Pending-job recovery, strict timeout enforcement, cancellation, and atomic retry publication remain roadmap work.

## Project Status

The end-to-end pipeline is running. The current focus is:

1. A stable, versioned, traceable document contract.
2. Recoverable and idempotent worker execution.
3. End-to-end evaluation on representative technical documents.
4. OCR, reading order, document assembly, and source-grounded RAG output.
5. C++ SDK API stabilization, packaging portability, and broader embedding examples.

Additional input formats, more interchangeable models, multi-tenant SaaS features, and large-scale orchestration are not current priorities.

## Get Started and Resources

- [Roadmap](docs/roadmap.md)
- [Dependency Setup](docs/dependencies.md)
- [Evaluation](docs/evaluation.md)
- [Document Contract v1](docs/document-contract-v1.md)
- [Quality Report v1](schemas/quality-report.v1.schema.json)
- [Text Model](docs/text-model.md)
- [Optional Platform](platform/README.md)
- [Contribution Guide](CONTRIBUTING.md)
- [GitHub Issues](https://github.com/ChNanAn/technical-doc-parser/issues)

Contributions are welcome. Small, focused changes with reproducible fixtures, tests, and measurable evidence are preferred over broad rewrites or unmeasured backend additions.

## License

Licensed under the [MIT License](LICENSE).
