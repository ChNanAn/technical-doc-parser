# September 2026 reliability and performance work

The starting revision is `e7bf673`. This change addresses the observed delivery, event, sorting, file publication,
and intermediate-data ownership problems without changing Document Contract v1 or the C ABI.

## Evidence and implemented changes

- Fault injection previously left a `queued` database Run with no queued Job after `XADD` failed. Run creation now
  commits a delivery record in the same PostgreSQL transaction. Redis publication is idempotent until the database
  confirms it, and queue pressure leaves new Jobs in the outbox.
- Worker input/Job validation failures previously updated only an expiring Redis Hash. Queue Attempt identity now
  allows `job_failed` to reach both event streams, the cache, and the database projector before the Job is ACKed.
- The browser previously closed EventSource on any error. It now permits Last-Event-ID reconnection, deduplicates
  events, and polls authoritative Run state until completion, failure, or cancellation.
- The reading-order comparator admitted `A<C`, `C<B`, and `B<A` for three nearly aligned boxes. Coordinate sorting
  now uses exact lexicographic comparisons; geometric grouping retains its tolerances. Permutation and NaN
  regression cases cover this boundary.
- Exporters previously opened final output names for writing. JSON, Markdown, HTML, and Artifact manifests now use
  an exclusive temporary location on the same filesystem and publish each completed file with rename.
- Pipeline vectors previously copied into an assembly request, then copied again into artifacts. Pipeline now
  transfers ownership through assembly; callers passing an lvalue request still retain their original data.
- Stage browsing now derives Text/Layout/Table/Reading Order views from the document already downloaded by the UI.
  Artifact requests no longer follow every progress event. Run listing uses pagination and one Redis pipeline.

## Assembly measurement

A standalone C++17 `-O2` harness compared the original and changed assembly call paths, including construction
of the request. The synthetic input contains 500 pages, 100 lines per page, and 256 characters per line with one
corresponding span. Each page has one paragraph block. Both paths emitted 500 pages and 500 blocks with identical
text hash `11293305048059032133`.

| Measurement | Original | Changed |
| --- | ---: | ---: |
| Peak process RSS (`/usr/bin/time`) | 143,520 KiB | 69,920 KiB |
| Request construction and assembly, one run | 51 ms | 16 ms |

This is a synthetic ownership/copy measurement, not an end-to-end PDF benchmark. RSS includes input construction
and retained output. It does not include model sessions, rendering, OCR, or filesystem export. Timing is a single
observation and is not a release threshold. The source and executables used for the local comparison are under
`/tmp/tdp-optimization-benchmark.olItVL` in the development environment.

## Remaining performance and reliability work

- Measure PNG artifact encoding and model preprocessing separately now that budgeted renderer pixel handoff
  removes the first decode (see below). Keep display artifacts and independently verify any further pixel changes.
- Extend warm Engine, first-image latency, stage-duration, and disk-byte measurements to a larger corpus.
  Structured document output still waits for cross-page processing and assembly.
- Extend the Worker execution recovery and cancellation below with artifact retention automation.
  Recovery reruns a crashed execution; it does not interrupt or checkpoint an in-flight model call.
- Budget Engine cache admission by memory and measure model initialization peaks before changing eviction behavior.
- Improve orientation and degraded-scan OCR against independently annotated fixtures. Existing preprocessing is
  still a debug artifact path, and no new OCR accuracy improvement is claimed by this change.

## Bounded page image reuse (initial stage-major implementation)

The initial image-cache increment routes PaddleOCR recognition/detection/region recognition, DocLayNet, Paddle layout,
Table Transformer, and debug preprocessing through one BGR image access function. Each parse owns a separate
cache; PageArtifact carries a weak reference without exposing OpenCV types in the document/SDK headers.
Direct backend calls without a live cache keep loading from the PNG path, as does Tesseract. The pipeline releases
cached pixels after table recognition, with RAII cleanup on failures and exceptions.

`DocumentParseOptions::image_cache_bytes`, the C API JSON field `image_cache_bytes`, and the CLI flag
`--image-cache-bytes` set the retained-pixel limit (default 67,108,864 bytes; `0` disables retention). Actual decoded
BGR byte sizes determine admission. Oversized pages still parse but are not retained; failed reads are not cached.
The limit excludes the current uncached image, model tensors, renderer buffers, allocator overhead, and cache
metadata. Multiple concurrent Engines each have their own budget. Returned matrices are shared, read-only input;
backends must clone before modifying pixels. Rendered files are immutable for the duration of a parse.

At this point the stages scanned the whole document separately. An ordinary LRU cache smaller than the document can evict
all useful pages during every scan. That implementation retained admitted pages for the rest of the image stages
and bypassed further admissions once the remaining byte budget could not fit them. This provided bounded reuse
without changing event ordering or cross-page table linking. The page-wise increment below replaces that scheduling.

A temporary `cv::imread` interposer recorded actual decodes (filesystem open counts can overcount OpenCV reads).
For `pdfjs-basicapi.pdf`, 200 DPI, PaddleOCR + DocLayNet + Table Transformer, with debug preprocessing enabled:

| Measurement | Before | After |
| --- | ---: | ---: |
| PNG decodes for 3 pages | 12 | 3 |
| Total measured decode time, one run | 162.969 ms | 41.143 ms |
| Retained decoded pixels | 0 | 34,818,354 bytes |
| Cache hits | 0 | 9 |

The exported debug Document JSON was byte-identical. These decode timings are single observations, include
instrumentation, and are not end-to-end speedup claims. Reproduce the separate, uninstrumented process benchmark:

```bash
python3 scripts/benchmark_page_image_cache.py \
  --engine build-ort/cpp/app/document_intelligence_engine \
  --output /tmp/page-image-benchmark-new --repeats 3 \
  tests/fixtures/pdfs/pdfjs-basicapi.pdf \
  data/raw/quality_baseline/inputs/pdf/paddleocr_book_photo.pdf \
  data/raw/quality_baseline/inputs/pdf/paddleocr_small_mixed_table.pdf \
  tests/fixtures/pdfs/pdfjs-tracemonkey.pdf
```

The script alternates cache-off/cache-on runs, uses `/usr/bin/time` for elapsed time and peak RSS, preserves raw
logs and artifacts, and checks exact Document JSON equality across variants and repetitions. It starts a fresh
process for every sample, including model initialization; it does not flush the filesystem cache. An optional
`--baseline-engine` can compare a saved executable from before the image change, without passing it the new flag.

All 24 runs (four PDFs, three repetitions per executable) against the saved pre-change CLI produced byte-identical
Document JSON. The following are the raw medians, **not performance conclusions**:

| PDF | Pages | Before elapsed | After elapsed | Before peak RSS | After peak RSS |
| --- | ---: | ---: | ---: | ---: | ---: |
| pdfjs-basicapi | 3 | 8.94 s | 9.70 s | 1,307,612 KiB | 1,319,156 KiB |
| paddleocr_book_photo | 1 | 4.56 s | 4.38 s | 1,184,032 KiB | 1,187,996 KiB |
| paddleocr_small_mixed_table | 1 | 2.43 s | 2.45 s | 986,916 KiB | 990,528 KiB |
| pdfjs-tracemonkey | 14 | 27.42 s | 44.88 s | 1,112,116 KiB | 1,161,048 KiB |

The machine was shared with unrelated conversion processes during these measurements. Inspection during the
long-document runs found a process using about 940% CPU, CPU pressure `some avg10=39.04`, load average 19.96,
and 2,521 MiB swap in use. Individual long-document times ranged from 23.40 to 55.09 seconds. These samples do
not establish an end-to-end speedup or regression; repeat them on an idle host before drawing that conclusion.
Pixel retention deliberately trades bounded memory for fewer decodes, and RSS also includes allocator/model
variation. The benchmark tool now records user/system CPU time, major page faults, and load averages to help
identify contention. The saved executables, interposer, logs and report are in
`/tmp/tdp-image-reuse.zxhAWd` in the development environment.

A further single pair using the same new executable with retention disabled/enabled also returned identical
14-page JSON: elapsed time was 36.02/28.85 s, user+system CPU time 32.32/27.05 s, peak RSS
1,120,980/1,163,104 KiB, and major page faults 0/0. The one-minute load average fell from 11.58 to 9.50 over
that pair; it verifies the extended benchmark fields and disabled-cache path, but does not resolve the need for
an idle-host performance measurement.

Validation: all 158 configured CTest cases passed, including model/pipeline quality benchmarks, C ABI checks,
and the installed SDK consumer. Nine image-cache tests cover BGR/gray/alpha decoding, scan retention, byte limits,
oversized images, disabled retention, failed reads, concurrent readers, debug pixel preservation, and Engine reuse
with exception cleanup. Builds also succeeded with preprocessing/PDFium disabled but ONNX enabled, and with
PDFium/OpenCV/ONNX all disabled. No full external 1,403-page olmOCR evaluation was run.

## Page-wise pipeline

The next increment uses PDFium's single-page rendering and native-text extraction. The pipeline now runs
render/debug preprocessing, text/OCR, layout, and table recognition for one page, clears its cached images,
then advances to the next page. Pixel retention therefore scales with the current page, subject to the same
byte budget, rather than the document length. Structured text/layout/table results are still retained for
cross-page table linking, reading order, and assembly. Rendering still produces PNG artifacts, and image
consumers decode a page on first access. Oversized pages and a zero budget keep the uncached path.

Document backends may opt into `supportsPageRendering()` / `renderPage()` and
`supportsPageTextExtraction()` / `extractPageNativeText()`. Existing backend source implementations need no
overrides: their whole-document methods run once per parse, and the resulting batches feed the page loop.
These legacy calls cannot offer early rendering or page-level interruption. The existing vector stage APIs
and new single-page APIs share the same quality policy and postprocessing logic.

Page-image artifacts are announced immediately after rendering. Render/text/layout/table stage lifecycles
overlap, with one start and completion per document and monotonically increasing per-stage page progress.
Their `duration_ms` values accumulate execution time, excluding other stages and observer callbacks. Warnings
are published immediately but collected in the original stage-major order for final JSON/provenance. The
Worker and browser already track events by stage. Deadline checks now occur before every page and between
its expensive stages; they do not interrupt an in-flight model call. Failed runs stop scheduling later pages.

An instrumented comparison against the immediately preceding cache implementation used `pdfjs-tracemonkey.pdf`
(14 pages, 200 DPI, Paddle OCR / DocLayNet / Table Transformer, debug enabled):

| Measurement | Whole-document stages | Page-wise stages |
| --- | ---: | ---: |
| PNG decodes | 32 | 14 |
| Cache hits | 10 | 28 |
| Peak retained pixels | 56,100,000 bytes | 11,220,000 bytes |
| Retained pixels after table stage | 56,100,000 bytes, then released | 0 |
| Instrumented decode time, one run | 756.728 ms | 317.142 ms |

The complete debug JSON was byte-identical. Additional CLI comparisons on the three-page `pdfjs-basicapi.pdf`,
scanned `paddleocr_book_photo.pdf`, and `paddleocr_small_mixed_table.pdf` also produced byte-identical JSON.
Their single-sample elapsed times were 7.92/8.00 s, 4.30/4.39 s, and 2.32/2.27 s respectively; no short-document
speedup is claimed.

A separate SDK observer harness compared the saved pre-change and updated installed libraries. Each process
initialized one Engine and parsed the 14-page PDF three times without debug preprocessing or final JSON export.
The harness records model initialization separately and measures first-image events from the start of `parse()`:

| Measurement | Whole-document stages | Page-wise stages |
| --- | ---: | ---: |
| Model initialization, one process | 687 ms | 771 ms |
| First-image event, median of three parses | 2,071 ms | 146 ms |
| First parse duration | 29,248 ms | 26,762 ms |
| Parse duration, median of three parses | 25,366 ms | 21,774 ms |
| Parse duration, median of the two later parses | 23,865 ms | 21,703.5 ms |
| Process peak RSS across three parses | 1,158,836 KiB | 1,119,340 KiB |

These are local observations on one native-text PDF. First-image timing excludes model initialization, HTTP/SSE
delivery, and browser rendering; final structured results still wait for assembly. The first parse also populates
model execution arenas, so it is listed separately from later parses. CPU scheduling and filesystem cache state
can affect the numbers, and neither the RSS bound nor this timing change is a general inference-speed guarantee.
The complete artifacts, logs, saved SDKs, and harness source are under `/tmp/tdp-page-pipeline.rfho3k`.

Validation: 164 CTest cases passed, including the model/pipeline quality benchmarks, C ABI, and installed SDK
consumer. Added scheduling regressions cover first-image publication before rendering page two, per-stage event
lifecycles/progress, cross-three-page table links, warning order, legacy backend equivalence, failure/reuse,
page identity/count validation, empty documents, and deadline handling. The image-cache integration test also
verifies one decode per page when only a single page fits the budget. Builds passed with ONNX enabled but
PDFium/preprocessing disabled, and with all three optional dependencies disabled.

## Lazy renderer pixel handoff

The next baseline is `cee2806`, rebuilt after committing the previous work so both
executables record the same producer revision. An actual `cv::imread` trace confirmed
that the page-wise pipeline still decoded every page PNG once: PDFium's RGBA buffer
was discarded after PNG writing, before the first image consumer could reuse it.

`RenderRequest::on_page_rendered` now provides a backend-neutral, optional synchronous
handoff after successful PNG writing. PDFium moves its tightly packed RGBA vector into
the per-parse cache if its allocated capacity fits the remaining budget. On first image
access, the cache converts RGBA to shared BGR and releases RGBA. Grayscale values and
RGB channels are preserved; alpha is discarded exactly as with `cv::IMREAD_COLOR`.
Pages that never reach an image consumer require no color conversion. No OpenCV types
cross into the document-source interface, and file artifacts remain available to
Tesseract, standalone consumers, and legacy renderers.

Admission validates page identity, dimensions, channel count, buffer length and overflow.
Invalid/oversized buffers do not enter the cache. RGBA requires four bytes per pixel
(or more if its vector has spare capacity), while decoded BGR needs three: a budget that
fits only BGR falls back to one PNG decode, then reuses that BGR. Zero budget disables
retention. The existing per-page clear, weak artifact ownership, and exception cleanup
also release unconverted RGBA. Conversion temporarily holds both RGBA and BGR; the byte
budget covers retained buffers, not this transient output, PDFium rendering buffers,
allocator overhead or model tensors. It is not a process RSS cap.

Logs now distinguish `rendered_admissions`, `rendered_rejections`, `conversions` and
`conversion_us` from PNG `decodes` / `decode_us`. A read served from admitted RGBA counts
as a cache hit, including the first conversion. The pipeline supplies the callback only
for incremental renderers, preserving legacy batch behavior.

Validation: all 170 CTest cases passed, including model/pipeline quality, the C ABI,
packaging and an installed SDK consumer. Six new regressions cover lazy RGBA conversion
with gray/color/alpha pixels, unused-buffer release, actual allocation capacity and
remaining budget, invalid buffers, concurrent conversion, and PNG-publication-before-handoff.
The existing pipeline integration test now also compares real rendered pixels against PNG,
checks exact JSON equivalence under RGBA/BGR-only budgets, and verifies disabled retention,
text-only consumers, failed runs and Engine reuse. ONNX-only and fully minimal builds passed.

For `pdfjs-tracemonkey.pdf` (14 pages, 200 DPI, Paddle OCR / DocLayNet / Table Transformer,
debug enabled), an interposer and cache counters measured:

| Measurement | Page-wise PNG cache | Renderer handoff |
| --- | ---: | ---: |
| PNG decodes | 14 | 0 |
| RGBA-to-BGR conversions | 0 | 14 |
| Instrumented PNG decode time, one run | 302.711 ms | 0 ms |
| Measured RGBA-to-BGR time, one run | 0 ms | 10.181 ms |
| Peak retained pixel buffers | 11,220,000 bytes | 14,960,000 bytes |
| Retained pixels after table stage | 0 | 0 |
| Process peak RSS | 1,125,744 KiB | 1,132,340 KiB |

The complete output directories, including JSON, page PNGs and debug images, were
byte-identical. This proves removal of the decode, with a bounded increase in retained
pixels before conversion. Instrumented process elapsed time was 24.26/22.06 seconds,
but single observations cannot attribute that entire difference to this change or
establish an overall inference speedup. The before/after executables, logs, output
directories, and CLI comparison report are under `/tmp/tdp-render-pixels.LvpOA3` in the
development environment; the existing `benchmark_page_image_cache.py --baseline-engine`
workflow reproduces uninstrumented comparisons.

Four additional uninstrumented CLI before/after pairs all produced byte-identical
Document JSON. Single-sample elapsed times were 7.62/7.63 s (`pdfjs-basicapi`), 4.10/4.04 s
(`paddleocr_book_photo`), 2.21/2.26 s (`paddleocr_small_mixed_table`), and 22.68/22.09 s
(`pdfjs-tracemonkey`). These include model initialization and support output equivalence;
they do not establish a short-document performance improvement.

## Worker execution recovery

Starting from `4d225bb`, fault injection placed a real Job in a dead consumer's pending
list. The Worker only called `XREADGROUP ... >`; after eight seconds the Job still had
one pending entry and no terminal event. Code inspection also showed that restarting an
execution reset event sequence to 1 and reused the same output directory, which would
break projection and permit a paused original Worker to overwrite replacement results.

The Worker now scans pending entries with a bounded, advancing cursor and claims only
expired execution leases. A separate Redis connection renews ownership during model
calls. Redis server time, a generation number and a unique process consumer identify
the current execution; every event checks this ownership atomically. Sequence numbers
continue from the last accepted event. Terminal publication updates both streams, Run
state and XACK in one Redis script, removing the previous success/acknowledgment gap.
Pending execution metadata has no TTL and disappears when terminal acknowledgment commits.

The existing Run, Job and Attempt identities remain stable on crash redelivery. Each
execution gets `execution_<n>` and its own output, manifest and event-log directory.
API projection persists the active execution; artifact/stage reads resolve that directory,
including when Redis state expires. The browser resets stage views on an execution change
and pins artifact URLs to the listed execution. A resumed stale process can finish an
in-flight call in its private directory but cannot publish accepted events or touch the
winner's output. A stage error alone is no longer a terminal Run state; the subsequent
Job terminal event finalizes it, keeping the crash gap recoverable. Explicitly cleared
cache fields no longer inherit the previous execution's stage/error while projection lags.

Defaults are a 30-second lease renewed every 10 seconds, and at most three processing
executions. A further claim records `worker.recovery_exhausted` without running models.
Healthy leases prevent another Worker from stealing a long-running Job. Recovery requires
an available Worker and retained Redis queue/lease/event state; it does not checkpoint
inference, forcibly cancel model calls, or recover total Redis data loss. Old Workers must
be stopped/drained before upgrading API/Worker/Web together, since old binaries do not
honor fencing. Superseded execution directories remain available for operational inspection
and need the deployment's normal artifact retention policy.

Validation used disposable Redis 7.4.2 and PostgreSQL 16.6 plus the real C++ Worker:

| Injected condition | Verified result |
| --- | --- |
| Dead consumer before execution | Another Worker completes the Job and clears pending state |
| SIGKILL after first page artifact | Replacement succeeds with a new execution and increasing sequence |
| SIGSTOP, replacement completes, then SIGCONT | Old Worker exits; accepted events and winner file hashes stay unchanged |
| Healthy processing exceeds the lease interval | Renewal holds ownership; competing Worker does not duplicate execution |
| Two consecutive crashes with a limit of two | Next claim emits `worker.recovery_exhausted` and terminal ACK |
| More than 32 earlier pending entries with live leases | Cursor advances to the abandoned entry without stealing healthy work |
| Proxy drops the terminal Redis reply after commit | One success event, no pending Job, no rerun after restart |
| Run cache removed after recovery | PostgreSQL selects the winning artifacts and stage output; old execution pin returns 409 |

All 48 Python/API/protocol/integration tests, the 9-test C++ Worker suite, and 12 frontend
tests passed; the frontend production build passed. After adding an assertion for the
cache/projection lag, its reproduction failed with `execution_2 layout interrupted` and
the corrected response passed with `execution_2 None None`. Logs are retained locally
under `/tmp/tdp-worker-recovery.x5gE4L`. CI runs the crash tests alongside delivery tests.
Core model and parsing code did not change in this increment.

## Durable cooperative Run cancellation

Starting from `de5d419`, a real API request confirmed that the cancellation route returned
404, and tracing Worker publication showed no cancellation check. The new idempotent
`POST /api/v1/runs/{run_id}/cancel` records a request in PostgreSQL and returns 202 with
`cancel_requested`. A dispatcher retries delivery to a Run/Attempt-specific Redis marker,
including an accepted write whose response was lost. Delivery and cleanup share a row lock;
cleanup follows durable terminal projection and never expires an outstanding request by time.

The existing fenced event script now checks this marker before accepting a non-cancellation
event. The Worker converts the signal into `job_cancelled` and commits both event streams,
Run cache state and XACK together. This uses existing observer callbacks across page, stage
and export boundaries; it adds no SDK/C ABI cancellation interface or backend interrupt.
An active call may finish first. Queued Jobs skip parsing when cancellation arrives before
their first callback; crash redelivery retains the marker and cancels without starting
replacement inference. Lease loss still prevents a superseded Worker from publishing.
Core Engine exception cleanup permits the same Worker to process subsequent Runs.

If success/failure commits in Redis before cancellation delivery, that terminal outcome
wins. A durable cancellation request alone does not assert the final status. The Web keeps
polling/listening while showing cancellation pending, handles request errors, and waits for
a terminal event or authoritative Run response. Existing produced artifacts are retained.
Dispatch/Worker availability and Redis state retention are still required; total Redis loss,
forced backend termination and automatic artifact deletion are outside this change.

Tests used disposable PostgreSQL 16.6, Redis 7.4.2 and the actual C++ Worker. Queued,
outbox and abandoned Jobs emitted one cancellation event with no processing output and
zero pending messages. Pausing a running Worker, delivering cancellation and resuming it
produced no further accepted progress/artifact events; the same Worker then completed a
new Run. Killing it instead verified cancellation through the next execution. A RESP proxy
held success publication on either side of the Redis commit: cancel-first emitted only
`job_cancelled`, success-first only `job_succeeded`, and repeated cancel calls preserved
the terminal result. Fault injection also covered unavailable Redis, uncertain cancellation
delivery, durable projection and marker cleanup. Logs are under `/tmp/tdp-cancel.rL3Pcf`.

All 57 Python/API/protocol/integration tests, the 9-test C++ Worker suite, and 14 frontend
tests passed; the frontend production build passed. Six additional Chromium checks with
mocked API/SSE responses covered responsive actions at 1180/760/390px, pending cancellation,
retry after request failure, stale responses after switching Runs, and late cancel responses
after a terminal failure. Before correction, a late 202 erased the failure banner and a
late 503 replaced it with a cancellation request error. Cancellation action errors now have
separate state, preserving the terminal failure in both cases. The action group also spans
the controls grid at narrow widths. The browser check script and output are retained with
the other local validation logs. Core model and parsing code did not change in this increment.
