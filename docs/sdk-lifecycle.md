# C++ SDK Lifecycle and Error Contract

This document defines the lifecycle and failure semantics that language
bindings and embedded applications may rely on from v0.1.

## Ownership

- `DocumentEngine` requires an explicit `EngineConfig`.
- One engine owns one initialized set of backend and model sessions.
- Repeated sequential `parse()` calls reuse those sessions.
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
