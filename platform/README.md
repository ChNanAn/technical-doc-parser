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
retry endpoint. Crash recovery redelivers that Attempt using a new fenced `execution_id`, with monotonic event
sequences across executions. Queue messages contain identifiers and a path to the canonical Job document rather than embedding
uploaded files.

## Local platform build

```bash
docker compose -f platform/deploy/docker-compose.yml up --build
```

Open `http://localhost:8080`. The first platform version uses a shared `/runtime` volume between API and Worker.
The `file://` URI is part of the protocol, so an S3/MinIO storage adapter can be introduced without changing Job,
Event, or Artifact identities. Queue payloads contain only IDs and the canonical Job path.

The included Compose file is a local/private-network deployment baseline. Its development database credentials are
not secrets, and authentication, authorization, TLS termination, rate limiting, and malware scanning must be added
before exposing it to an untrusted network.

The first startup downloads about 500 MB of pinned baseline models. The
`model-init` service verifies every file against the model-pack SHA256
manifest, replaces missing or damaged files, and becomes healthy only after
the complete pack passes verification. The Worker waits for that health gate
and mounts the same directory read-only.

The Worker image remains model-free, so rebuilding program images does not
bake model weights into image layers. Models are cached under the repository's
`models/` directory by default and subsequent starts only verify the existing
files. Set `DIE_MODEL_DIR=/absolute/model/path` to use another persistent model
directory. Run `bash scripts/setup_model_pack.sh --models-dir PATH` when models
need to be prepared manually or ahead of an offline deployment.

Docker Compose v1 users can run the equivalent command with `docker-compose`;
the model readiness gate works with both Compose v1 and v2.

The Worker is a persistent Redis Streams consumer. It processes one Run at a time, which avoids concurrent access
to non-reentrant backend state. Multiple Worker containers provide horizontal concurrency. Each Worker keeps a
bounded LRU of reusable `DocumentEngine` instances keyed by the effective backend tuple and registry configuration.
`WORKER_ENGINE_CACHE_SIZE` defaults to 2; increase it only after accounting for the memory used by each model set.

The Worker resolves the same `DOCUMENT_INTELLIGENCE_ENGINE_*` model-path and inference-tuning compatibility variables
as the CLI. Inspect the effective configuration without connecting to Redis or loading a model:

```bash
DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_MODEL_DIR=/models/paddleocr \
  ./build/platform-release/platform/worker/document_intelligence_worker --print-engine-config
```

Library users should continue to pass an explicit `EngineConfig`; environment compatibility belongs to process entry
points, not the SDK itself. At normal startup, the Worker records the resolved configuration and uses the same
configured backend registry for capability probing and every Job.

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
  `WORKER_RUNTIME_ROOT` is set—Job, input, and output paths stay inside that root. Queue and Job identities must
  agree, and output belongs to the canonical Run directory. Queue messages include the Attempt ID, allowing
  missing or malformed Job files to produce a `job_failed` event before acknowledgment.
- Creating a Run inserts both the Run and a pending delivery into PostgreSQL in one transaction. The API returns
  `202` once this commits. A background dispatcher retries delivery with a bounded delay; a Redis idempotency
  marker prevents duplicate enqueue when Redis accepted a Job but its response or the database commit was lost.
  Markers remain until the database records successful dispatch, then a recoverable cleanup removes them.
  The new `job_outbox` table and indexes are created at API startup. Previously created Runs are not re-enqueued
  automatically. Start the updated API and Worker together to use the queue's Attempt identity.
- Workers recover abandoned pending messages using a bounded, cursor-based `XPENDING` / `XCLAIM` scan. A separate
  connection renews each Job lease every third of `WORKER_JOB_LEASE_MS` (default 30,000; supported 1,000–3,600,000).
  Reclaim checks lease expiry using Redis server time, so a healthy long model call is not treated as abandoned.
  `WORKER_JOB_MAX_EXECUTIONS` defaults to 3 processing attempts; the next claim publishes
  `worker.recovery_exhausted` and acknowledges the Job without running the models. Recovery waits for an available
  Worker and reruns the page pipeline; it does not resume an interrupted inference. All Workers in a group should
  use the same settings. Each process uses a unique consumer identity even when `WORKER_ID` is reused.
- Every event checks the execution generation, consumer and unexpired lease in Redis. A terminal event updates
  both streams and the Run cache and acknowledges the Job in one script, so a lost success reply cannot trigger
  another execution. Execution metadata has no TTL while pending (preserving sequence/fencing) and is deleted
  atomically on terminal acknowledgment. Existing valid queued Jobs with identity fields can be recovered; malformed
  legacy messages without a usable identity remain pending for operator diagnosis.
- Output, manifests and local event logs live under `runs/<run>/executions/execution_<n>/`. A superseded Worker
  may finish an in-flight computation, but can only write its own directory and cannot publish newer state.
  The API resolves the current execution from Redis or PostgreSQL (`runs.execution_id`, migrated on startup).
  Artifact downloads can pin `execution_id`; a superseded pin returns 409. Legacy completed Runs still use their
  original directories. The browser resets stage/artifact views on execution changes. Upgrade API, Worker and Web
  together, draining/stopping old Workers first: old binaries do not enforce the new fencing. Superseded directories
  are retained for inspection and follow the deployment's artifact retention policy.
- User-requested cancellation is still deferred. The API event projector replays its own pending
  events, reclaims events abandoned by a previous projector, and restarts after transient Redis or database failures.
  Restart delay uses bounded exponential backoff with jitter; configure the initial delay, maximum delay, and stable
  reset interval with `DIE_PROJECTOR_RESTART_DELAY_SECONDS`, `DIE_PROJECTOR_RESTART_MAX_DELAY_SECONDS`, and
  `DIE_PROJECTOR_RESTART_RESET_SECONDS`.
  Only Job terminal events finalize Run status; `stage_failed` records the stage error while waiting for the
  fenced `job_failed` event. Process crashes between those events remain recoverable.
- The Job stream defaults to a 10,000-entry limit (`DIE_JOB_STREAM_MAX_LENGTH`). Dispatch trims only entries no
  consumer group still needs; a full stream leaves new deliveries in the durable outbox. Per-Run and global event streams
  retain their approximate `MAXLEN` caps of 2,000 and 100,000; size these above the expected unconsumed event backlog.
  Worker event publication updates both streams and the Run cache in one Redis script.
  The Worker refreshes a seven-day TTL on `run:{id}` and `run-events:{id}` after every event; configure
  `RUN_RETENTION_SECONDS` for the desired post-run inspection window. Postgres and the artifact store remain the
  durable sources.
- The first storage adapter uses a shared filesystem volume. MinIO/S3 can replace it later without changing
  Document, Run, Job, Event, or Artifact identities. API and Worker intentionally share numeric UID `10001` in this
  deployment so both can access the same Run directory.
- Page images and final JSON/Markdown/HTML are emitted as immediate Artifacts. Text, Layout, Table, and reading-order
  inspection comes from debug fields in the final `document.json`. Each exported result file and Artifact manifest
  is written privately and renamed into place after a successful close; this provides atomic file visibility,
  not an atomic multi-file Attempt commit or a power-loss durability guarantee. Recovery relies on Redis retaining
  queue/lease/event state; total Redis data loss and cross-store disaster recovery are outside this mechanism.
- The browser keeps SSE reconnection enabled and polls Run state every three seconds until a terminal status.
  Artifact lists refresh on Artifact/Run changes; stage inspection reuses the downloaded document instead of
  fetching and parsing the full document again for each stage. Run listing supports `limit` (default 100,
  maximum 500) and `offset`, and fetches Redis state in one pipeline. Durable terminal states take precedence
  over cached state; Run queries fall back to PostgreSQL during Redis outages.

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

Delivery integration tests use real, disposable PostgreSQL and Redis services. They create a temporary database
schema and unique queue/Run keys; the optional Worker tests spawn the executable and exercise successful export,
missing input, missing Job files, malformed JSON, SIGKILL, SIGSTOP/resume, lease renewal, retry exhaustion,
claim-scan progress and a lost terminal Redis reply. These checks also run in CI:

```bash
export DIE_TEST_DATABASE_URL=postgresql://document:document@127.0.0.1:5432/document
export DIE_TEST_REDIS_URL=redis://127.0.0.1:6379/0
export DIE_TEST_WORKER="$PWD/build/platform-release/platform/worker/document_intelligence_worker"
PYTHONPATH=platform/api pytest -s platform/api/tests/test_delivery_integration.py platform/api/tests/test_worker_recovery_integration.py
```
