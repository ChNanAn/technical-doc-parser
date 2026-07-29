# Changelog

Notable user-facing changes are recorded here. This project follows Semantic
Versioning for the engine release, C ABI compatibility, and the independently
versioned document contract.

## [Unreleased]

## [0.1.1] - 2026-07-29

### Added

- Added a Compose `model-init` health gate that downloads missing baseline
  models, replaces files that fail SHA256 verification, and prevents the
  Worker from starting until the complete model pack is verified.
- Reworked the platform Web UI into a document inspection workspace with page
  images, thumbnails, zoom controls, and overlays for Text, Layout, Table,
  Reading Order, and final document blocks. Raw stage JSON remains available
  in a copyable drawer.

### Fixed

- Replaced the Ubuntu 24.04-specific CLI artifact with a Linux x86-64 bundle
  built against the Ubuntu 20.04 ABI baseline.
- Statically linked OpenCV and its image codecs so the CLI no longer requires
  a distribution-specific `libopencv_*.so` ABI.
- Added release gates for unresolved libraries, dynamic OpenCV, `GLIBC_2.31`,
  and `GLIBCXX_3.4.28`, with a second verification before upload.
- Included resolved vcpkg versions and license notices for statically linked
  dependencies in the CLI bundle.
- Artifact manifests are now written to temporary files and atomically
  published before `artifact_ready` events, preventing API readers from
  observing empty or partially written JSON during frequent refreshes.

### Compatibility

- The v0.1.1+ CLI supports x86-64 glibc Linux with glibc 2.31 or newer and
  `GLIBCXX_3.4.28`. musl distributions are not supported.
- The v0.1.0 CLI remains Ubuntu 24.04-specific and should not be repaired by
  symlinking a different OpenCV ABI.
- Model weights are unchanged; engine v0.1.1 continues to use model pack
  v0.1.0.

## [0.1.0] - 2026-07-28

### Added

- Document Contract v1 release candidate with JSON Schema, fixtures, snapshot
  tests, source metadata, bounded warning aggregation, and real complete and
  partial engine-output validation.
- Reusable C++ `DocumentEngine` facade with explicit configuration, model
  session reuse, lifecycle state, structured status, run provenance, and
  export-independent parse results.
- C ABI v1 with opaque handles, explicit ownership, exception containment,
  runtime ABI/version queries, symbol visibility checks, and SONAME 1.
- Three-layer backend, pipeline, and product evaluation with a versioned
  Quality Report profile. The first public olmOCR-Bench baseline is
  `44.2% +/- 0.9%` over 8,413 tests.
- Deterministic source and Linux x86-64 CLI archives, a separately versioned
  model pack, GHCR container publishing, SHA256 manifests, and vcpkg consumer
  builds.

### Pre-v0.1 Migration Notes

- `DocumentEngine()` no longer creates an implicit default configuration.
  Construct it with `DocumentEngine(defaultEngineConfig())` or, preferably, an
  explicitly populated `EngineConfig`.
- The public `DocumentEngine(EngineConfig, BackendRegistry)` constructor was
  removed when backend registry injection became an internal pipeline concern.
  Embedders should use the public facade; repository tests use the private
  internal-access boundary.
- `DocumentEngine::parse(PipelineRunOptions)` was replaced by
  `parse(DocumentParseOptions)`. Backend selection belongs to `EngineConfig`
  and is fixed for the reusable engine's lifetime.
- Build-tree model defaults are relocatable `models/...` paths. Installed CMake
  packages inject their installed model prefix, while the C ABI requires model
  paths explicitly.

### Known Limitations

- The first input backend is PDF; additional formats can be added without
  changing Document Contract v1.
- Input and rendered pages still use filesystem paths, `output_directory` is
  required by the C ABI, and active inference cannot yet be cancelled.
- The published external score is an early baseline, not a production accuracy
  claim. Formula-heavy and degraded scanned documents remain weak categories.

[Unreleased]: https://github.com/ChNanAn/technical-doc-parser/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/ChNanAn/technical-doc-parser/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/ChNanAn/technical-doc-parser/releases/tag/v0.1.0
