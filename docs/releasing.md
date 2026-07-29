# Release Process

Releases use one version across CMake, Git tags, source archives, CLI bundles,
and container labels.

## Supported Program Artifacts

- Deterministic source archive.
- Linux x86-64 CLI bundle built against the Ubuntu 20.04 ABI baseline.
- `ghcr.io/chnanan/technical-doc-parser:<tag>` CLI container.
- Separately versioned baseline model pack.

Program artifacts do not contain model weights. Models are versioned and
distributed separately so applications can upgrade engine code and model packs
independently.

The model pack is defined by
[`packaging/model-pack.v1.json`](../packaging/model-pack.v1.json). Every entry
records an immutable upstream revision and URL, SHA256, SPDX license, and
license source. Packaging fails unless every local file matches that manifest.

The v0.1.1+ CLI supports glibc 2.31 and newer with `GLIBCXX_3.4.28`, statically
links OpenCV, and does not require a distribution OpenCV package. It is not
compatible with musl-based systems. The v0.1.0 CLI remains an Ubuntu
24.04-only artifact.

## Local Packaging

A normal local build can create development smoke packages:

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

These host-built CLI archives inherit the host ABI and are not release
artifacts. Build the publishable CLI through the pinned Ubuntu 20.04 builder:

```bash
docker buildx build \
  --target artifacts \
  --output type=local,dest=dist/portable \
  --build-arg REVISION="$(git rev-parse HEAD)" \
  --build-arg SOURCE_DATE_EPOCH="$(git show -s --format=%ct HEAD)" \
  -f packaging/portable-cli.Dockerfile .
(cd dist/portable && sha256sum --check SHA256SUMS)
```

Create the model artifact only after the pinned setup scripts have completed:

```bash
bash scripts/setup_paddleocr_baseline.sh
bash scripts/setup_doclaynet_layout.sh
bash scripts/setup_paddle_layout.sh
bash scripts/setup_table_transformer.sh
python3 scripts/package_model_pack.py verify
python3 scripts/package_model_pack.py package --output-dir dist
(cd dist && sha256sum ./*.tar.gz > SHA256SUMS)
```

Install the model pack into an SDK prefix:

```bash
mkdir -p <prefix>/share/DocumentIntelligenceEngine/models
tar -xzf technical-doc-parser-models-0.1.0.tar.gz \
  -C <prefix>/share/DocumentIntelligenceEngine/models \
  --strip-components=1
```

For source builds that explicitly need CMake to install already downloaded
models, configure with `-DDOCUMENT_INTELLIGENCE_ENGINE_INSTALL_MODELS=ON` and
install the `Models` component. The option rejects incomplete model sets.

Release packaging uses `--require-portable-linux` and fails when any bundled
ELF requires newer than `GLIBC_2.31` or `GLIBCXX_3.4.28`, dynamically links
OpenCV, has unresolved libraries, lacks vcpkg version/license metadata, uses a
different configured project version, or omits PDFium or ONNX Runtime.

## Publishing

1. Update `project(... VERSION ...)`, release notes, and compatibility notes.
2. Run the normal CI and release packaging smoke test.
3. Create and push an annotated `v<version>` tag.
4. Verify the GitHub Release archives, `SHA256SUMS`, and GHCR image.

The tag-triggered release workflow rejects tags that do not exactly match the
CMake project version.
