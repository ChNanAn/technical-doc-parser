# Linux x86-64 CLI Bundle

This bundle contains the Document Intelligence Engine command-line program for
Ubuntu 24.04 x86-64. It is not a universal Linux binary.

Install the OpenCV runtime libraries before running the CLI:

```bash
sudo apt-get update
sudo apt-get install -y \
  libopencv-core406t64 \
  libopencv-imgcodecs406t64 \
  libopencv-imgproc406t64
```

Model weights are distributed separately. From the extracted CLI bundle root,
extract the matching model pack into `models/`:

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

`PROGRAM-MANIFEST.json` records the engine, PDFium, and ONNX Runtime versions,
SPDX licenses, and SHA256 values for bundled executables and shared libraries.
Their upstream licenses and third-party notices are included under
`share/licenses/`.
