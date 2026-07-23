# Document Intelligence Platform

This directory contains the optional product platform around the standalone C++ parsing engine:

- `protocol/`: versioned Job, Event, and Artifact contracts shared by every process.
- `api/`: FastAPI upload, run orchestration, status, event, and artifact API.
- `worker/`: optional persistent C++ Redis Streams worker.
- `web/`: React interface for backend selection and stage inspection.
- `deploy/`: Docker Compose and service images.

The root C++ engine remains independently buildable. Runtime documents and results belong under the ignored
`runtime/` directory or a deployment volume; they are never committed to the repository.

One uploaded `Document` may have many immutable `Run` records with different backend combinations. Run, Attempt, and
Job identities are separate in the protocol; version 1 creates exactly one Attempt per Run and does not yet expose a
retry endpoint. Queue messages contain identifiers and a path to the canonical Job document rather than embedding
uploaded files.

## Local platform build

```bash
cmake --preset platform-release
cmake --build --preset platform-release --target document_intelligence_worker
docker compose -f platform/deploy/docker-compose.yml up --build
```

Open `http://localhost:8080`. The first platform version uses a shared `/runtime` volume between API and Worker.
The `file://` URI is part of the protocol, so an S3/MinIO storage adapter can be introduced without changing Job,
Event, or Artifact identities. Queue payloads contain only IDs and the canonical Job path.

The included Compose file is a local/private-network deployment baseline. Its development database credentials are
not secrets, and authentication, authorization, TLS termination, rate limiting, and malware scanning must be added
before exposing it to an untrusted network.

The Worker is a persistent Redis Streams consumer. It processes one Run at a time, which avoids concurrent access
to non-reentrant backend state. Multiple Worker containers provide horizontal concurrency. Model-session caching is
the next runtime optimization; the protocol and process lifecycle do not depend on it.

## Product boundary

The repository has two deliberately separate deliverables:

1. The default product is the standalone C++ engine and CLI. A normal root CMake build does not require Redis,
   PostgreSQL, Python, Node.js, or a browser build.
2. This directory is an optional deployment and inspection platform. It adds asynchronous jobs, persistence,
   live stage events, artifact browsing, and independent backend selection for each Run.

The root CMake file only declares the optional Worker target because the Worker links the in-process C++ pipeline.
It is disabled by default and is enabled by the `platform-release` preset. API and Web are built by their own package
tools and do not enter the CMake dependency graph.

```text
browser -> FastAPI -> PostgreSQL
                    -> Redis Streams -> persistent C++ Worker -> shared artifact storage
browser <- SSE events <--------------- Redis Streams
```

`StageObserver` is a narrow callback boundary in the core pipeline. The CLI installs a no-op observer; the Worker
installs an observer that turns the same callbacks into versioned Events and Artifact manifests. It does not decide
which backend runs. Backend choice remains an immutable property of each Run, so the same uploaded Document can be
run repeatedly with different OCR, Layout, and Table combinations.

## Version 1 operational semantics

- Worker heartbeat uses a dedicated Redis connection and is refreshed every 10 seconds, including during long model
  inference.
- Worker capabilities are probed from the C++ backend registry at startup. Because v1 uses one shared queue, the API
  only offers the capability intersection across live Workers; this keeps every consumer able to execute every Job.
- `maximum_pages` is enforced immediately after opening the document. `timeout_seconds` is a cooperative deadline
  checked between pipeline stages; it does not forcibly interrupt a backend call already in progress.
- Worker validates that the input is a regular PDF, its byte size matches the Job metadata, and—when
  `WORKER_RUNTIME_ROOT` is set—input and output paths stay inside that root.
- Redis Streams provides durable delivery, but abandoned pending-message recovery (`XAUTOCLAIM`) and user-requested
  cancellation are intentionally deferred. Do not advertise either capability in this version.
- The first storage adapter uses a shared filesystem volume. MinIO/S3 can replace it later without changing
  Document, Run, Job, Event, or Artifact identities. API and Worker intentionally share numeric UID `10001` in this
  deployment so both can access the same Run directory.
- Page images and final JSON/Markdown/HTML are emitted as immediate Artifacts. Text, Layout, Table, and reading-order
  inspection currently comes from debug fields in the final `document.json`.

## Local verification

```bash
python -m pip install -e './platform/api[test]'
pytest platform/api/tests
npm ci --prefix platform/web
npm audit --prefix platform/web
npm run build --prefix platform/web
cmake --preset platform-release
cmake --build --preset platform-release --target document_intelligence_worker --parallel
```
