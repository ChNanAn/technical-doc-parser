# Release Process

Releases use one version across CMake, Git tags, source archives, CLI bundles,
and container labels.

## Supported Program Artifacts

- Deterministic source archive.
- Ubuntu 24.04 x86-64 CLI bundle.
- `ghcr.io/chnanan/technical-doc-parser:<tag>` CLI container.

Program artifacts do not contain model weights. Models are versioned and
distributed separately so applications can upgrade engine code and model packs
independently.

## Local Packaging

Build without downloading models, then create source and CLI archives:

```bash
cmake -S . -B build/release \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PADDLEOCR_BASELINE=OFF \
  -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_DOCLAYNET_LAYOUT=OFF \
  -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PADDLE_LAYOUT=OFF
cmake --build build/release --target document_intelligence_engine --parallel
bash scripts/package_release.sh \
  --kind all \
  --build-dir build/release \
  --output-dir dist
(cd dist && sha256sum --check SHA256SUMS)
```

The packaging script fails when the executable has unresolved libraries, when
the configured build version differs from the project version, or when PDFium
and ONNX Runtime are absent from the runtime dependency graph.

## Publishing

1. Update `project(... VERSION ...)`, release notes, and compatibility notes.
2. Run the normal CI and release packaging smoke test.
3. Create and push an annotated `v<version>` tag.
4. Verify the GitHub Release archives, `SHA256SUMS`, and GHCR image.

The tag-triggered release workflow rejects tags that do not exactly match the
CMake project version.
