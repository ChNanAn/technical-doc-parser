# C ABI v1

The C ABI is the native boundary for future Java/JNI support. It exposes
opaque handles and Document v1 JSON without C++ STL types, exceptions, or
temporary JSON files.

## Compatibility

- `DIE_ABI_VERSION` and `die_abi_version()` return `1`.
- The shared library has `SOVERSION 1`.
- Functions and fixed-width result/state constants in ABI v1 are append-only.
- Document output follows the independently versioned Document Contract v1.
- `die_engine_version()` reports the engine release version, not the ABI
  version or model-pack version.

## Ownership

| Handle | Created by | Released by |
| --- | --- | --- |
| `die_engine_t` | `die_engine_create` | `die_engine_destroy` |
| `die_document_t` | `die_engine_parse` | `die_document_destroy` |
| `die_error_t` | Any failed operation with `out_error` | `die_error_destroy` |

Destroy functions accept `NULL`. Strings returned by document and error
accessors are borrowed UTF-8 views valid until their owning handle is
destroyed. Bindings should copy them into managed strings before destruction.

The caller must keep an engine alive until all calls return. One engine accepts
one active parse; a concurrent call returns `engine.busy` with
`retryable=true`. Create multiple engines for parallel parsing.

## Engine Configuration

`die_engine_create` requires a UTF-8 JSON object with `schema_version: 1`.
Unknown fields, wrong types, invalid ranges, and unsupported schema versions
are rejected.

```json
{
  "schema_version": 1,
  "backends": {
    "document": "pdf",
    "ocr": "paddle",
    "layout": "doclaynet",
    "table": "table-transformer",
    "registry_config": "/opt/app/backends.json"
  },
  "tesseract": {
    "executable": "tesseract",
    "language": "eng"
  },
  "models": {
    "paddle_ocr": {
      "detection_model": "/models/paddleocr/baseline/det.onnx",
      "recognition_model": "/models/paddleocr/baseline/rec.onnx",
      "character_dict": "/models/paddleocr/baseline/ppocrv5_dict.txt",
      "profile": "ppocrv5_mobile"
    },
    "doclaynet": {
      "model_path": "/models/layout/doclaynet/model.onnx",
      "confidence_threshold": 0.5
    },
    "paddle_layout": {
      "model_path": "/models/layout/paddle/pp-doclayout-v3.onnx",
      "confidence_threshold": 0.5
    },
    "table_transformer": {
      "detection_model": "/models/table/table-transformer/detection.onnx",
      "structure_model": "/models/table/table-transformer/structure.onnx",
      "detection_confidence_threshold": 0.9,
      "structure_confidence_threshold": 0.5,
      "crop_padding": 20
    }
  }
}
```

Unspecified recognized fields use `defaultEngineConfig()`. No process
environment is read by the C ABI.

## Runtime Dependencies

The CMake target propagates the external shared libraries used by the selected
build, such as PDFium, OpenCV, and ONNX Runtime. They are dependencies of the
program package, not part of the C ABI or model pack. Install them in the
platform loader path or place distributable copies beside
`libdocument_intelligence_engine_c.so`; the installed library uses
`$ORIGIN` as its relative runtime search path on Linux. Every distributed
native bundle must record the dependency versions, licenses, and SHA256 values.
Linux x86-64 is the first supported binary distribution target; other
platforms currently build from source.

## Parse Options

`input_path` and `output_directory` are required. Page images and optional
debug artifacts are written under the output directory; the owned
`die_document_t` contains the normalized Document v1 JSON.

```json
{
  "schema_version": 1,
  "input_path": "/work/input.pdf",
  "output_directory": "/work/output",
  "dpi": 200,
  "debug": false,
  "timeout_seconds": 0,
  "maximum_pages": 0,
  "run_id": "job-123"
}
```

## Errors

Every operation returns a coarse `die_result_t`. On failure, `die_error_t`
provides the stable machine-readable code, message, stage, and retryable flag
from the C++ Status contract. No C++ exception crosses the ABI.

```c
die_engine_t* engine = NULL;
die_error_t* error = NULL;
die_result_t result = die_engine_create(config_json, &engine, &error);
if (result != DIE_RESULT_OK) {
    fprintf(stderr, "%s: %s\n",
            die_error_code(error),
            die_error_message(error));
    die_error_destroy(error);
}
```

## Java Direction

The first Java binding should map each opaque handle to a `long` owned by an
`AutoCloseable` class, copy native strings immediately, and serialize all calls
per engine. JNI code should depend only on this header and shared library,
never on C++ document or backend headers.
