# Linux x86-64 CLI Bundle

This bundle contains the Document Intelligence Engine command-line program for
Linux x86-64 systems with glibc 2.31 or newer and a C++ runtime providing
`GLIBCXX_3.4.28`, including Ubuntu 20.04 and newer. It does not support
musl-based distributions such as Alpine Linux.

OpenCV and its image codecs are statically linked. Do not install or symlink a
different OpenCV ABI for this bundle. PDFium and ONNX Runtime are included
under `lib/`.

Model weights are distributed separately. From the extracted CLI bundle root,
extract the compatible model pack into `models/`. Engine v0.1.1 uses the
unchanged model pack v0.1.0:

```bash
mkdir -p models
tar -xzf ../technical-doc-parser-models-0.1.0.tar.gz \
  -C models --strip-components=1
```

Run the CLI from the bundle root so its relocatable defaults resolve `models/`:

```bash
bin/document_intelligence_engine input.pdf --out output/
```

To run it from another working directory or keep models elsewhere, configure
all model locations explicitly:

```bash
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_MODEL_DIR=/opt/technical-doc-parser/models/paddleocr/baseline
export DOCUMENT_INTELLIGENCE_ENGINE_DOCLAYNET_MODEL=/opt/technical-doc-parser/models/layout/doclaynet/model.onnx
export DOCUMENT_INTELLIGENCE_ENGINE_PADDLE_LAYOUT_MODEL=/opt/technical-doc-parser/models/layout/paddle/pp-doclayout-v3.onnx
export DOCUMENT_INTELLIGENCE_ENGINE_TABLE_DETECTION_MODEL=/opt/technical-doc-parser/models/table/table-transformer/detection.onnx
export DOCUMENT_INTELLIGENCE_ENGINE_TABLE_STRUCTURE_MODEL=/opt/technical-doc-parser/models/table/table-transformer/structure.onnx

/opt/technical-doc-parser/bin/document_intelligence_engine input.pdf --out output/
```

`PROGRAM-MANIFEST.json` records the Linux ABI baseline, static OpenCV linkage,
engine, PDFium, and ONNX Runtime versions, SPDX licenses, and SHA256 values.
The exact vcpkg dependency resolution is stored in `share/vcpkg/status`.
Upstream licenses and third-party notices are included under `share/licenses/`.
