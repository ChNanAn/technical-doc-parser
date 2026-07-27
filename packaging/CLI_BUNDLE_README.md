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

Run:

```bash
bin/document_intelligence_engine input.pdf --out output/
```

Model weights are intentionally not included in the program bundle. Install the
matching model pack separately and configure model paths as documented in
`docs/dependencies.md` in the source release.

The bundled PDFium and ONNX Runtime shared libraries retain their upstream
licenses. See the release notes and source release for dependency versions and
license links.
