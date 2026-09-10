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
- The API event projector replays its own pending
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
  `RUN_RETENTION_SECONDS` for the desired post-run event/cache window. This setting does not delete disk files;
  generated files have a separate opt-in retention policy below. Postgres keeps Run history after artifact expiry.
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

## Cancelling a Run

The Web workbench exposes **取消任务** for queued/running Runs. API clients can send:

```http
POST /api/v1/runs/{run_id}/cancel
```

The idempotent endpoint returns `202` with a Run response containing `cancel_requested`.
This flag records a durable request, not a terminal outcome. The UI displays **正在取消**
and continues SSE/status polling until `job_cancelled`, `job_succeeded`, or `job_failed`.
An unknown Run returns `404`; repeating the request on a completed Run preserves its result.

Cancellation is persisted in PostgreSQL before delivery to Redis. API startup adds
`cancel_requested_at`, `cancel_delivered_at`, `cancel_cleaned` and their partial indexes.
The dispatcher retries missing/uncertain delivery after a Redis outage. Cancellation
markers include the Attempt identity and have no TTL until terminal state reaches
PostgreSQL; cleanup is serialized with delivery and retries safely after failures.
The cancellation marker follows a Run across crash-recovery executions.

The Worker checks cancellation atomically with every fenced event publication. Queued
Jobs skip parsing when the request has reached Redis before their first callback.
Running Jobs stop on their next observer callback (page/stage boundaries); an in-flight
model, renderer or export call may finish first. Lease ownership remains required, and
`job_cancelled`, Run cache state and queue acknowledgment commit in one Redis operation.
If success/failure commits before cancellation delivery, that existing terminal result wins.
There is no force-kill, page checkpoint, SDK/C ABI cancellation entry point, or guaranteed
stop latency during a backend call. Queued requests still require dispatch capacity and
an available Worker to reach terminal state. Files already produced remain inspectable;
this endpoint does not delete Run artifacts.

Upgrade API, Worker and Web together after draining/stopping old Workers; older Workers
do not check the cancellation marker. See [failure-injection evidence](../docs/optimization-2026-09.md).

## Artifact retention

Automatic disk cleanup is **disabled by default**. The API can expire generated files for
Runs whose `succeeded`, `failed` or `cancelled` status has been persisted in PostgreSQL for
the configured interval. It uses the durable Run `updated_at`, not filesystem timestamps,
Redis TTLs or a cancellation request. Queued/running Runs are always excluded.

| API environment setting | Default | Meaning |
| --- | --- | --- |
| `DIE_ARTIFACT_RETENTION_SECONDS` | `0` | Disabled; set a positive minimum age to enable automatic deletion |
| `DIE_ARTIFACT_CLEANUP_INTERVAL_SECONDS` | `3600` | Delay between sweeps; each API process starts a sweep on startup |
| `DIE_ARTIFACT_CLEANUP_BATCH_SIZE` | `50` | Candidates per batch, from 1 to 500; busy/failed Runs do not block later batches |

Upgrade and stop/drain all old API/Worker processes before enabling retention. The new
versions share POSIX `flock` locks on `.artifacts.lock` in each Run directory. The filesystem
must support these locks across all API/Worker processes, using the same numeric UID; the
Compose shared local volume does. Older binaries and external file tools do not honor this
protocol. Never remove/replace the lock files or expiry markers manually while services run.

Preview eligible directories with the API package installed, the same `DIE_DATABASE_URL`
and `DIE_RUNTIME_ROOT` as the deployment, and the upgraded database schema:

```bash
# JSON output only; no database migrations, marker creation or deletion.
PYTHONPATH=platform/api python -m app.retention --retention-seconds 604800

# Permanently remove the generated files for eligible Runs; rechecks every candidate.
PYTHONPATH=platform/api python -m app.retention --retention-seconds 604800 --apply
```

For automatic seven-day retention in Compose, set `DIE_ARTIFACT_RETENTION_SECONDS=604800`
in the environment used to recreate the API container. A busy Run is retried on the next
sweep. A preview is a point-in-time candidate list, not a reservation or an exact prediction
of the later deletion set; the apply command rechecks age, terminal state, paths and locks.
CLI errors produce a nonzero exit code, and automatic failures are logged and retried.

Cleanup deletes only `output/`, `artifacts/`, `executions/` (including superseded executions),
and `events.ndjson` beneath the validated canonical Run directory. It preserves `job.json`,
the original uploaded PDF, Run/database history, lock/expiry markers and other operator files.
Unknown directories, missing Run roots, mismatched Job paths and root symlinks are left for
operator diagnosis. Symlinks inside generated output are removed without following targets.
Deletion is permanent; retain backups if outputs must be recoverable. The retained PDF can
be parsed again as a new Run.

Workers hold a shared directory lock throughout execution, including terminal publication;
downloads hold one until the response ends. Cleanup requires an exclusive lock, so a paused
superseded Worker or slow download postpones deletion even after a replacement finishes.
On expiry, `artifacts_expired_at` is committed before deletion. Artifact list, download and
stage endpoints return `410` for expired content; temporary lock contention returns `503`
with `Retry-After: 1`. Run queries retain their terminal status and expose the expiry time.
The Web explains expiry and allows a new parse instead of waiting indefinitely for output.

Partial deletion or a failed completion write remains eligible for retry, including after
an API restart or a longer retention setting. `artifacts_cleaned_at` records completion;
it is separate from the time content became unavailable. This policy does not delete input
uploads, orphan directories, DB history, or Redis queue/event data, and it does not guarantee
a cleanup deadline for files held by a running/paused process. Setting retention back to `0`
stops future automatic work, including retries of incomplete cleanup.

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
claim-scan progress, lost replies, durable cancellation, cancel/success races, retention failures,
download locks and paused-writer cleanup protection. These checks also run in CI:

```bash
export DIE_TEST_DATABASE_URL=postgresql://document:document@127.0.0.1:5432/document
export DIE_TEST_REDIS_URL=redis://127.0.0.1:6379/0
export DIE_TEST_WORKER="$PWD/build/platform-release/platform/worker/document_intelligence_worker"
PYTHONPATH=platform/api pytest -s platform/api/tests/test_delivery_integration.py platform/api/tests/test_worker_recovery_integration.py platform/api/tests/test_cancellation_integration.py platform/api/tests/test_retention_integration.py
```
