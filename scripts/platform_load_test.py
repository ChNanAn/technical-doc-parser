#!/usr/bin/env python3
"""Run a platform API load test and write run-level capacity metrics.

The script intentionally uses only the Python standard library so it can run on
locked-down servers. It uploads each unique PDF once, creates repeated Runs for
those documents, streams worker events, and writes CSV/NDJSON artifacts.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import csv
import datetime as dt
import hashlib
import http.client
import json
import math
import os
import platform
import queue
import random
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


STAGES = [
    "configure",
    "open",
    "render",
    "text",
    "layout",
    "table",
    "reading_order",
    "assembly",
    "export",
]


RUN_CSV_FIELDS = [
    "run_label",
    "workload_index",
    "repeat_index",
    "input_path",
    "filename",
    "input_size_bytes",
    "input_sha256",
    "document_id",
    "run_id",
    "status",
    "error_code",
    "error_message",
    "submitted_at",
    "job_started_at",
    "finished_at",
    "queue_wait_s",
    "run_wall_s",
    "worker_event_duration_s",
    "page_count",
    "pages_per_second_wall",
    "pages_per_second_worker",
    "warning_count",
    "requested_backends_json",
    "resolved_backends_json",
    *[f"stage_{stage}_ms" for stage in STAGES],
]


@dataclass(frozen=True)
class UploadedDocument:
    path: Path
    document_id: str
    filename: str
    size_bytes: int
    sha256: str
    upload_seconds: float


@dataclass(frozen=True)
class WorkItem:
    index: int
    repeat_index: int
    document: UploadedDocument


@dataclass
class RunMetrics:
    run_label: str
    workload_index: int
    repeat_index: int
    document: UploadedDocument
    document_id: str
    run_id: str = ""
    status: str = "failed"
    error_code: str = ""
    error_message: str = ""
    submitted_at: str = ""
    job_started_at: str = ""
    finished_at: str = ""
    queue_wait_s: float | None = None
    run_wall_s: float = 0.0
    worker_event_duration_s: float | None = None
    page_count: int = 0
    warning_count: int = 0
    requested_backends: dict[str, Any] = field(default_factory=dict)
    resolved_backends: dict[str, Any] = field(default_factory=dict)
    stage_durations_ms: dict[str, int] = field(default_factory=dict)

    def row(self) -> dict[str, Any]:
        worker_pps = ""
        if self.worker_event_duration_s and self.worker_event_duration_s > 0 and self.page_count > 0:
            worker_pps = f"{self.page_count / self.worker_event_duration_s:.6f}"
        wall_pps = ""
        if self.run_wall_s > 0 and self.page_count > 0:
            wall_pps = f"{self.page_count / self.run_wall_s:.6f}"

        value: dict[str, Any] = {
            "run_label": self.run_label,
            "workload_index": self.workload_index,
            "repeat_index": self.repeat_index,
            "input_path": str(self.document.path),
            "filename": self.document.filename,
            "input_size_bytes": self.document.size_bytes,
            "input_sha256": self.document.sha256,
            "document_id": self.document_id,
            "run_id": self.run_id,
            "status": self.status,
            "error_code": self.error_code,
            "error_message": self.error_message,
            "submitted_at": self.submitted_at,
            "job_started_at": self.job_started_at,
            "finished_at": self.finished_at,
            "queue_wait_s": format_optional_seconds(self.queue_wait_s),
            "run_wall_s": f"{self.run_wall_s:.3f}",
            "worker_event_duration_s": format_optional_seconds(self.worker_event_duration_s),
            "page_count": self.page_count,
            "pages_per_second_wall": wall_pps,
            "pages_per_second_worker": worker_pps,
            "warning_count": self.warning_count,
            "requested_backends_json": json.dumps(self.requested_backends, sort_keys=True, separators=(",", ":")),
            "resolved_backends_json": json.dumps(self.resolved_backends, sort_keys=True, separators=(",", ":")),
        }
        for stage in STAGES:
            duration = self.stage_durations_ms.get(stage)
            value[f"stage_{stage}_ms"] = "" if duration is None else duration
        return value


class ApiError(RuntimeError):
    pass


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def parse_event_time(value: str) -> dt.datetime | None:
    try:
        return dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None


def seconds_between(start: str, end: str) -> float | None:
    start_time = parse_event_time(start) if start else None
    end_time = parse_event_time(end) if end else None
    if start_time is None or end_time is None:
        return None
    return max(0.0, (end_time - start_time).total_seconds())


def format_optional_seconds(value: float | None) -> str:
    return "" if value is None else f"{value:.3f}"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_api_url(value: str) -> str:
    return value.rstrip("/")


def endpoint(api_url: str, suffix: str) -> str:
    return f"{normalize_api_url(api_url)}{suffix}"


def read_json_response(response: http.client.HTTPResponse) -> Any:
    body = response.read()
    if response.status < 200 or response.status >= 300:
        message = body.decode("utf-8", errors="replace")
        raise ApiError(f"HTTP {response.status}: {message}")
    if not body:
        return None
    return json.loads(body.decode("utf-8"))


def connection_for(url: str, timeout: float) -> tuple[http.client.HTTPConnection, str]:
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme not in {"http", "https"}:
        raise ApiError(f"unsupported URL scheme: {parsed.scheme}")
    host = parsed.hostname
    if host is None:
        raise ApiError(f"invalid URL: {url}")
    port = parsed.port
    target = urllib.parse.urlunsplit(("", "", parsed.path or "/", parsed.query, ""))
    if parsed.scheme == "https":
        return http.client.HTTPSConnection(host, port=port, timeout=timeout), target
    return http.client.HTTPConnection(host, port=port, timeout=timeout), target


def upload_pdf(api_url: str, path: Path, timeout: float) -> UploadedDocument:
    upload_url = endpoint(api_url, "/api/v1/documents")
    boundary = f"----die-load-test-{uuid.uuid4().hex}"
    filename = path.name
    prefix = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        "Content-Type: application/pdf\r\n\r\n"
    ).encode("utf-8")
    suffix = f"\r\n--{boundary}--\r\n".encode("utf-8")
    size = path.stat().st_size
    content_length = len(prefix) + size + len(suffix)

    started = time.perf_counter()
    conn, target = connection_for(upload_url, timeout)
    try:
        conn.putrequest("POST", target, skip_host=True)
        conn.putheader("Host", urllib.parse.urlsplit(upload_url).netloc)
        conn.putheader("Content-Type", f"multipart/form-data; boundary={boundary}")
        conn.putheader("Content-Length", str(content_length))
        conn.endheaders()
        conn.send(prefix)
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                conn.send(chunk)
        conn.send(suffix)
        response = conn.getresponse()
        payload = read_json_response(response)
    finally:
        conn.close()

    elapsed = time.perf_counter() - started
    return UploadedDocument(
        path=path,
        document_id=payload["document_id"],
        filename=payload["filename"],
        size_bytes=int(payload["size_bytes"]),
        sha256=payload["sha256"],
        upload_seconds=elapsed,
    )


def request_json(method: str, url: str, payload: dict[str, Any] | None, timeout: float) -> Any:
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            body = response.read()
    except urllib.error.HTTPError as error:
        message = error.read().decode("utf-8", errors="replace")
        raise ApiError(f"HTTP {error.code}: {message}") from error
    if not body:
        return None
    return json.loads(body.decode("utf-8"))


def create_run(api_url: str, document_id: str, options: dict[str, Any], timeout: float) -> str:
    payload = request_json(
        "POST",
        endpoint(api_url, f"/api/v1/documents/{document_id}/runs"),
        options,
        timeout,
    )
    return payload["run_id"]


def stream_events(api_url: str, run_id: str, timeout: float) -> Iterable[dict[str, Any]]:
    request = urllib.request.Request(endpoint(api_url, f"/api/v1/runs/{run_id}/events"), method="GET")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            data_lines: list[str] = []
            for raw_line in response:
                line = raw_line.decode("utf-8", errors="replace").rstrip("\n")
                if line.endswith("\r"):
                    line = line[:-1]
                if not line:
                    if data_lines:
                        yield json.loads("\n".join(data_lines))
                        data_lines.clear()
                    continue
                if line.startswith(":"):
                    continue
                if line.startswith("data:"):
                    data_lines.append(line[5:].lstrip())
    except urllib.error.HTTPError as error:
        message = error.read().decode("utf-8", errors="replace")
        raise ApiError(f"HTTP {error.code}: {message}") from error


def collect_pdfs(args: argparse.Namespace) -> list[Path]:
    paths: list[Path] = []
    if args.pdf_dir:
        paths.extend(sorted(Path(args.pdf_dir).expanduser().resolve().rglob("*.pdf")))
    paths.extend(Path(value).expanduser().resolve() for value in args.pdfs)
    seen: set[Path] = set()
    result: list[Path] = []
    for path in paths:
        if path in seen:
            continue
        seen.add(path)
        if not path.is_file():
            raise SystemExit(f"PDF does not exist: {path}")
        result.append(path)
    if args.max_files:
        result = result[: args.max_files]
    if not result:
        raise SystemExit("No PDFs supplied. Use positional PDF paths or --pdf-dir.")
    return result


def make_run_options(args: argparse.Namespace) -> dict[str, Any]:
    return {
        "dpi": args.dpi,
        "debug": args.debug,
        "backends": {
            "document": args.document_backend,
            "ocr": args.ocr_backend,
            "layout": args.layout_backend,
            "table": args.table_backend,
            "registry_config": args.registry_config,
        },
        "timeout_seconds": args.timeout_seconds,
        "maximum_pages": args.maximum_pages,
    }


def update_metrics_from_event(metrics: RunMetrics, event: dict[str, Any]) -> None:
    event_type = event.get("type")
    timestamp = event.get("timestamp", "")
    if event_type == "job_started":
        metrics.job_started_at = timestamp
    elif event_type == "run_configured":
        backends = event.get("backends", {})
        metrics.requested_backends = backends.get("requested", {})
        metrics.resolved_backends = backends.get("resolved", {})
    elif event_type == "stage_progress":
        progress = event.get("progress", {})
        total = int(progress.get("total", 0) or 0)
        if total > metrics.page_count:
            metrics.page_count = total
    elif event_type == "stage_completed":
        stage = str(event.get("stage", ""))
        if stage:
            metrics.stage_durations_ms[stage] = int(event.get("duration_ms", 0) or 0)
    elif event_type == "stage_warning":
        metrics.warning_count += 1
    elif event_type in {"stage_failed", "job_failed"}:
        error = event.get("error", {})
        metrics.error_code = str(error.get("code", metrics.error_code))
        metrics.error_message = str(error.get("message", metrics.error_message))
    elif event_type in {"job_succeeded", "job_cancelled"}:
        metrics.error_code = ""
        metrics.error_message = ""


def run_work_item(
    args: argparse.Namespace,
    item: WorkItem,
    run_options: dict[str, Any],
    event_output: queue.Queue[dict[str, Any] | None],
) -> RunMetrics:
    metrics = RunMetrics(
        run_label=args.run_label,
        workload_index=item.index,
        repeat_index=item.repeat_index,
        document=item.document,
        document_id=item.document.document_id,
    )
    metrics.submitted_at = utc_now()
    wall_started = time.perf_counter()
    try:
        metrics.run_id = create_run(args.api_url, item.document.document_id, run_options, args.request_timeout)
        terminal_event_seen = False
        for event in stream_events(args.api_url, metrics.run_id, args.event_timeout):
            event_output.put({"run_id": metrics.run_id, "input_path": str(item.document.path), "event": event})
            update_metrics_from_event(metrics, event)
            event_type = event.get("type")
            if event_type in {"job_succeeded", "job_failed", "job_cancelled"}:
                metrics.status = {
                    "job_succeeded": "succeeded",
                    "job_failed": "failed",
                    "job_cancelled": "cancelled",
                }[str(event_type)]
                metrics.finished_at = str(event.get("timestamp", utc_now()))
                terminal_event_seen = True
                break
        if not terminal_event_seen:
            metrics.status = "failed"
            metrics.error_code = "load_test.event_stream_closed"
            metrics.error_message = "event stream ended before a terminal job event"
            metrics.finished_at = utc_now()
    except Exception as error:  # noqa: BLE001 - keep load test running and record failed jobs.
        metrics.status = "failed"
        metrics.error_code = error.__class__.__name__
        metrics.error_message = str(error)
        metrics.finished_at = utc_now()
    finally:
        metrics.run_wall_s = time.perf_counter() - wall_started

    metrics.queue_wait_s = seconds_between(metrics.submitted_at, metrics.job_started_at)
    metrics.worker_event_duration_s = seconds_between(metrics.job_started_at, metrics.finished_at)
    return metrics


def write_events(path: Path, event_queue: queue.Queue[dict[str, Any] | None]) -> None:
    with path.open("a", encoding="utf-8") as output:
        while True:
            item = event_queue.get()
            try:
                if item is None:
                    return
                output.write(json.dumps(item, ensure_ascii=False, sort_keys=True) + "\n")
                output.flush()
            finally:
                event_queue.task_done()


def percentile(values: list[float], percent: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    rank = (len(ordered) - 1) * percent
    lower = math.floor(rank)
    upper = math.ceil(rank)
    if lower == upper:
        return ordered[int(rank)]
    return ordered[lower] * (upper - rank) + ordered[upper] * (rank - lower)


def build_summary(
    args: argparse.Namespace,
    capabilities: Any,
    documents: list[UploadedDocument],
    results: list[RunMetrics],
    measured_wall_s: float,
) -> dict[str, Any]:
    succeeded = [result for result in results if result.status == "succeeded"]
    failed = [result for result in results if result.status != "succeeded"]
    pages = sum(result.page_count for result in succeeded)
    run_wall_values = [result.run_wall_s for result in succeeded]
    worker_duration_values = [
        result.worker_event_duration_s
        for result in succeeded
        if result.worker_event_duration_s is not None and result.worker_event_duration_s > 0
    ]
    page_rates = [
        result.page_count / result.worker_event_duration_s
        for result in succeeded
        if result.worker_event_duration_s is not None and result.worker_event_duration_s > 0 and result.page_count > 0
    ]
    stage_summary = {}
    for stage in STAGES:
        values = [float(result.stage_durations_ms[stage]) for result in succeeded if stage in result.stage_durations_ms]
        stage_summary[stage] = {
            "count": len(values),
            "p50_ms": percentile(values, 0.50),
            "p95_ms": percentile(values, 0.95),
            "sum_ms": sum(values),
        }

    return {
        "run_label": args.run_label,
        "api_url": args.api_url,
        "created_at": utc_now(),
        "host": {
            "hostname": platform.node(),
            "platform": platform.platform(),
            "cpu_count": os.cpu_count(),
        },
        "options": {
            "concurrency": args.concurrency,
            "repeat": args.repeat,
            "dpi": args.dpi,
            "debug": args.debug,
            "timeout_seconds": args.timeout_seconds,
            "maximum_pages": args.maximum_pages,
            "backends": make_run_options(args)["backends"],
        },
        "capabilities": capabilities,
        "documents": [
            {
                "path": str(document.path),
                "document_id": document.document_id,
                "filename": document.filename,
                "size_bytes": document.size_bytes,
                "sha256": document.sha256,
                "upload_seconds": document.upload_seconds,
            }
            for document in documents
        ],
        "results": {
            "total_runs": len(results),
            "succeeded_runs": len(succeeded),
            "failed_runs": len(failed),
            "total_success_pages": pages,
            "measured_wall_s": measured_wall_s,
            "aggregate_pages_per_second": pages / measured_wall_s if measured_wall_s > 0 else None,
            "single_run_wall_p50_s": percentile(run_wall_values, 0.50),
            "single_run_wall_p95_s": percentile(run_wall_values, 0.95),
            "worker_event_duration_p50_s": percentile(worker_duration_values, 0.50),
            "worker_event_duration_p95_s": percentile(worker_duration_values, 0.95),
            "per_run_pages_per_second_p50": percentile(page_rates, 0.50),
            "per_run_pages_per_second_p95": percentile(page_rates, 0.95),
            "stage_summary": stage_summary,
            "failed_run_ids": [result.run_id for result in failed if result.run_id],
        },
    }


def write_documents_csv(path: Path, documents: list[UploadedDocument]) -> None:
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(
            output,
            fieldnames=["path", "document_id", "filename", "size_bytes", "sha256", "upload_seconds"],
        )
        writer.writeheader()
        for document in documents:
            writer.writerow(
                {
                    "path": str(document.path),
                    "document_id": document.document_id,
                    "filename": document.filename,
                    "size_bytes": document.size_bytes,
                    "sha256": document.sha256,
                    "upload_seconds": f"{document.upload_seconds:.3f}",
                }
            )


def run_batch(
    args: argparse.Namespace,
    items: list[WorkItem],
    run_options: dict[str, Any],
    event_output: queue.Queue[dict[str, Any] | None],
) -> tuple[list[RunMetrics], float]:
    random.shuffle(items)
    started = time.perf_counter()
    results: list[RunMetrics] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrency) as executor:
        futures = [executor.submit(run_work_item, args, item, run_options, event_output) for item in items]
        for completed, future in enumerate(concurrent.futures.as_completed(futures), start=1):
            result = future.result()
            results.append(result)
            print(
                f"[{completed}/{len(futures)}] {result.status} run={result.run_id or '-'} "
                f"file={result.document.filename} pages={result.page_count} wall={result.run_wall_s:.1f}s",
                flush=True,
            )
    return results, time.perf_counter() - started


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Load test the Document Intelligence Platform API.")
    parser.add_argument("pdfs", nargs="*", help="PDF files to include in the workload.")
    parser.add_argument("--pdf-dir", help="Recursively include PDFs from this directory.")
    parser.add_argument("--max-files", type=int, default=0, help="Limit the number of discovered PDFs.")
    parser.add_argument("--api-url", default="http://127.0.0.1:8000", help="Platform API base URL.")
    parser.add_argument("--out", default="", help="Output directory. Defaults to runtime/load-tests/<run-label>.")
    parser.add_argument("--run-label", default=dt.datetime.now().strftime("%Y%m%d-%H%M%S"))
    parser.add_argument("--concurrency", type=int, default=1, help="Number of in-flight Runs.")
    parser.add_argument("--repeat", type=int, default=1, help="Number of measured Runs per uploaded PDF.")
    parser.add_argument("--warmup-runs", type=int, default=0, help="Sequential warmup Runs excluded from summary.")
    parser.add_argument("--dpi", type=int, default=200)
    parser.add_argument("--debug", action="store_true", help="Enable debug artifacts. Default is false for capacity.")
    parser.add_argument("--timeout-seconds", type=int, default=86400)
    parser.add_argument("--maximum-pages", type=int, default=10000)
    parser.add_argument("--document-backend", default="pdf")
    parser.add_argument("--ocr-backend", default="auto")
    parser.add_argument("--layout-backend", default="auto")
    parser.add_argument("--table-backend", default="auto")
    parser.add_argument("--registry-config", default="")
    parser.add_argument("--request-timeout", type=float, default=60.0)
    parser.add_argument("--upload-timeout", type=float, default=3600.0)
    parser.add_argument("--event-timeout", type=float, default=90000.0)
    args = parser.parse_args(argv)
    if args.concurrency <= 0:
        raise SystemExit("--concurrency must be positive")
    if args.repeat <= 0:
        raise SystemExit("--repeat must be positive")
    if args.warmup_runs < 0:
        raise SystemExit("--warmup-runs cannot be negative")
    args.api_url = normalize_api_url(args.api_url)
    return args


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    pdf_paths = collect_pdfs(args)
    output_dir = Path(args.out).expanduser().resolve() if args.out else Path("runtime/load-tests") / args.run_label
    output_dir.mkdir(parents=True, exist_ok=True)

    capabilities = request_json("GET", endpoint(args.api_url, "/api/v1/capabilities"), None, args.request_timeout)
    print(f"capabilities: {json.dumps(capabilities, ensure_ascii=False)}", flush=True)

    documents: list[UploadedDocument] = []
    print(f"uploading {len(pdf_paths)} PDFs...", flush=True)
    for index, path in enumerate(pdf_paths, start=1):
        document = upload_pdf(args.api_url, path, args.upload_timeout)
        documents.append(document)
        print(
            f"[upload {index}/{len(pdf_paths)}] {document.filename} "
            f"{document.size_bytes} bytes in {document.upload_seconds:.1f}s",
            flush=True,
        )
    write_documents_csv(output_dir / "documents.csv", documents)

    run_options = make_run_options(args)
    event_queue: queue.Queue[dict[str, Any] | None] = queue.Queue()
    event_thread = threading.Thread(target=write_events, args=(output_dir / "events.ndjson", event_queue), daemon=True)
    event_thread.start()

    try:
        if args.warmup_runs:
            warmup_items = [
                WorkItem(index=index, repeat_index=0, document=documents[index % len(documents)])
                for index in range(args.warmup_runs)
            ]
            print(f"running {len(warmup_items)} warmup Runs...", flush=True)
            warmup_args = argparse.Namespace(**vars(args))
            warmup_args.concurrency = 1
            warmup_args.run_label = f"{args.run_label}-warmup"
            run_batch(warmup_args, warmup_items, run_options, event_queue)

        items = [
            WorkItem(index=index, repeat_index=repeat_index, document=document)
            for repeat_index in range(1, args.repeat + 1)
            for index, document in enumerate(documents, start=1)
        ]
        print(f"running measured workload: runs={len(items)} concurrency={args.concurrency}", flush=True)
        results, measured_wall_s = run_batch(args, items, run_options, event_queue)
    finally:
        event_queue.put(None)
        event_queue.join()
        event_thread.join(timeout=5)

    with (output_dir / "runs.csv").open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=RUN_CSV_FIELDS)
        writer.writeheader()
        for result in sorted(results, key=lambda value: (value.repeat_index, value.workload_index, value.run_id)):
            writer.writerow(result.row())

    summary = build_summary(args, capabilities, documents, results, measured_wall_s)
    (output_dir / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print(f"wrote: {output_dir / 'documents.csv'}", flush=True)
    print(f"wrote: {output_dir / 'events.ndjson'}", flush=True)
    print(f"wrote: {output_dir / 'runs.csv'}", flush=True)
    print(f"wrote: {output_dir / 'summary.json'}", flush=True)
    aggregate = summary["results"]["aggregate_pages_per_second"]
    print(
        "summary: "
        f"runs={summary['results']['total_runs']} "
        f"succeeded={summary['results']['succeeded_runs']} "
        f"failed={summary['results']['failed_runs']} "
        f"pages={summary['results']['total_success_pages']} "
        f"aggregate_pages_per_second={aggregate:.6f}" if aggregate is not None else "aggregate_pages_per_second=n/a",
        flush=True,
    )
    return 0 if summary["results"]["failed_runs"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
