# Dependencies

This project keeps large native dependencies out of git. They are downloaded into `third_party/` by setup scripts and ignored by `.gitignore`.

## PDFium

PDF rendering uses prebuilt PDFium binaries from [`bblanchon/pdfium-binaries`](https://github.com/bblanchon/pdfium-binaries).

Install the pinned Linux x64 build:

```bash
bash scripts/setup_pdfium.sh
```

Then configure CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPDFium_DIR=third_party/pdfium
cmake --build build --config Release --parallel
```

The setup script currently pins:

```text
PDFIUM_VERSION=151.0.7906.0
PDFIUM_PACKAGE=pdfium-linux-x64.tgz
PDFIUM_DIR=third_party/pdfium
```

To inspect the resolved settings without downloading:

```bash
bash scripts/setup_pdfium.sh --print-config
```

To refresh the local copy:

```bash
bash scripts/setup_pdfium.sh --force
```

`third_party/pdfium/` is intentionally not committed because PDFium binaries are platform-specific and should be reproducible from the pinned setup script.
