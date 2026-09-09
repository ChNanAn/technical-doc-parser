# C++ SDK Lifecycle and Error Contract

This document defines the lifecycle and failure semantics that language
bindings and embedded applications may rely on from v0.1.

## Ownership

- `DocumentEngine` requires an explicit `EngineConfig`.
- One engine owns one initialized set of backend and model sessions.
- Repeated sequential `parse()` calls reuse those sessions.
- Rendered/decoded page images use a separate cache owned by each parse. Configure
  `DocumentParseOptions::image_cache_bytes` (default 64 MiB; `0` disables retention).
  Pixels are released after each page's table recognition, including on early return or an
  exception. Returned artifacts carry only weak references to this runtime cache.
- PDFium hands rendered RGBA pixels to the cache within its byte budget. The first
  image consumer converts them to BGR and releases RGBA; unused pages need no conversion.
  Admission counts the RGBA vector capacity. When RGBA does not fit, PNG decoding remains
  available and the smaller BGR image may still fit. The budget excludes temporary
  conversion output, renderer buffers, model tensors, and allocator overhead.
- The engine is move-only. A moved-from instance remains destructible and
  reports `DocumentEngineState::MovedFrom`.
- Destruction is not synchronized with active calls. The owner must keep the
  engine alive until a parse returns.

## Concurrency

An engine processes at most one document at a time. Concurrent `parse()` calls
on the same instance do not enter backend code: one call proceeds and the
others return:

```text
code=engine.busy
stage=engine
retryable=true
```

Create multiple engines for parallel parsing. Each engine owns independent
backend sessions.

## States

`DocumentEngine::state()` returns:

| State | Meaning |
| --- | --- |
| `Ready` | Initialization succeeded and no parse is active. |
| `Parsing` | One parse currently owns the instance. |
| `InitializationFailed` | Backend configuration or model initialization failed. |
| `MovedFrom` | Ownership moved to another engine instance. |

`isReady()` reports whether initialization succeeded, including while a parse
is active. `initializationStatus()` is immutable for the engine lifetime.

## Page scheduling and observers

PDFium renders and extracts native text one page at a time. Each page completes
text/OCR, layout, and table recognition before the next page is rendered. Its
page-image artifact is announced as soon as rendering finishes. Cross-page table
linking, reading order, and assembly still run after the page loop.

Render, text, layout, and table each emit one `onStageStarted` and one
`onStageCompleted` on success, with monotonically increasing per-stage page
progress. Their events interleave, so consumers must track each stage separately.
For these four stages, `duration_ms` reports accumulated execution time, excluding other stages
and observer callbacks; it is not the elapsed interval between start and completion.
Warnings are emitted as encountered, while exported warnings retain stage-major order.

Legacy document backend sources remain supported through the whole-document
`renderPages` / `extractNativeText` interfaces. Their batches are loaded once per
parse; early rendering and page-level text extraction require the optional
`supportsPageRendering` / `supportsPageTextExtraction` capabilities and their
single-page methods. Recompile C++ backend implementations with the updated headers.

`RenderRequest::on_page_rendered` is an optional synchronous pixel handoff. A renderer
may call it after successfully writing the page PNG, passing that page's tightly packed
8-bit RGBA `PageBitmap` by move. The callback may consume its storage. Dimensions, page
identity, channels and buffer length must match the artifact; the cache rejects invalid
buffers and retains the PNG fallback. Renderers must not retain the callback beyond the
render call. The pipeline enables it for incremental rendering only; legacy batches keep
file-based caching. No OpenCV types are added to the document-source interface, and the
C ABI is unchanged.

Deadlines are checked before each page and between its expensive stages. An
in-flight renderer/model call or a legacy whole-document call is not interrupted.
After a failure, later pages are not scheduled and the Engine can be reused.

## Errors

Public parse failures are returned through `ParseResult::status`; backend or
observer exceptions do not cross `DocumentEngine::parse()`.

| Code | Meaning | Retryable |
| --- | --- | --- |
| `engine.not_started` | Default `ParseResult` has not been populated. | No |
| `engine.moved_from` | Parse was requested on a moved-from instance. | No |
| `engine.busy` | Another parse owns this engine. | Yes |
| `engine.parse_exception` | Unexpected C++ exception escaped pipeline code. | No |

Normal pipeline failures preserve their concrete stage and code. A successful
parse can still produce `document.status == partial`; its machine-readable
warnings and provenance explain the fallback. Hard failures have
`ParseResult::status.okStatus() == false`.

## Serialization

`JsonDocumentExporter::serialize()` validates the same Document v1 invariants
as file export and returns owned UTF-8 JSON in memory. Serialization failures
use the existing `export.*` status codes. This is the stable handoff used by
the C ABI; bindings do not need temporary files or access to C++ document
types.
