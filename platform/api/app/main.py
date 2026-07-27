from __future__ import annotations

import asyncio
import hashlib
import json
import uuid
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any, AsyncIterator

from fastapi import FastAPI, File, HTTPException, Request, UploadFile
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, StreamingResponse
from redis.asyncio import Redis

from .database import Database
from .models import (
    CapabilitiesResponse,
    DocumentResponse,
    RunCreate,
    RunResponse,
    utc_now,
)
from .projector import ProjectorState, supervise_worker_event_projector
from .settings import Settings


PIPELINE_DEBUG_EXTENSION = "io.github.chnanan.technical-doc-parser.pipeline_debug"

REGISTERED_BACKENDS = {
    "document": ["auto", "pdf"],
    "ocr": ["auto", "paddle", "tesseract", "noop"],
    "layout": ["auto", "doclaynet", "paddle-layout", "text"],
    "table": ["auto", "table-transformer", "text"],
}


def _id(prefix: str) -> str:
    return f"{prefix}_{uuid.uuid4().hex}"


def _record_options(record: Any) -> RunCreate:
    value = record["options_json"]
    if isinstance(value, str):
        value = json.loads(value)
    return RunCreate.model_validate(value)


def _validate_backends(options: RunCreate) -> None:
    selected = options.backends.model_dump(exclude={"registry_config"})
    for stage, backend in selected.items():
        if backend not in REGISTERED_BACKENDS[stage]:
            raise HTTPException(422, f"unknown {stage} backend: {backend}")


def _common_worker_capabilities(workers: list[dict[str, Any]]) -> dict[str, list[str]]:
    if not workers:
        return REGISTERED_BACKENDS
    result: dict[str, list[str]] = {}
    for stage, registered in REGISTERED_BACKENDS.items():
        supported = set(registered)
        for worker in workers:
            supported.intersection_update(worker.get("capabilities", {}).get(stage, []))
        result[stage] = [backend for backend in registered if backend in supported]
    return result


async def _workers(redis: Redis) -> list[dict[str, Any]]:
    workers: list[dict[str, Any]] = []
    async for key in redis.scan_iter(match="worker:*"):
        values = await redis.hgetall(key)
        if "capabilities" in values:
            values["capabilities"] = json.loads(values["capabilities"])
        values["worker_id"] = key.removeprefix("worker:")
        workers.append(values)
    return workers


def _safe_child(root: Path, child: Path) -> Path:
    resolved_root = root.resolve()
    resolved_child = child.resolve()
    if resolved_child != resolved_root and resolved_root not in resolved_child.parents:
        raise HTTPException(404, "artifact path escapes the run directory")
    return resolved_child


async def _save_upload(upload: UploadFile, destination: Path, maximum_bytes: int) -> tuple[int, str]:
    size = 0
    digest = hashlib.sha256()
    signature = bytearray()
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        with destination.open("wb") as output:
            while chunk := await upload.read(1024 * 1024):
                size += len(chunk)
                if size > maximum_bytes:
                    raise HTTPException(413, "uploaded document exceeds the configured size limit")
                if len(signature) < 5:
                    signature.extend(chunk[: 5 - len(signature)])
                digest.update(chunk)
                output.write(chunk)
        if bytes(signature) != b"%PDF-":
            raise HTTPException(415, "uploaded content does not have a PDF file signature")
    except Exception:
        destination.unlink(missing_ok=True)
        raise
    return size, digest.hexdigest()


async def _enqueue_job(redis: Redis, stream: str, fields: dict[str, str], maximum_length: int) -> None:
    await redis.xadd(
        stream,
        fields,
        maxlen=maximum_length,
        approximate=True,
    )


def _document_stage_output(document: dict[str, Any], stage: str) -> Any:
    if stage in {"assembly", "export"}:
        return {"blocks": document.get("blocks", [])}

    pages = document.get("pages", [])
    if stage == "render":
        return [{"page_number": page["number"], "image": page.get("image")} for page in pages]

    debug_key = {"text": "text", "table": "tables"}.get(stage, stage)
    return [
        {
            "page_number": page["number"],
            "output": page.get("extensions", {}).get(PIPELINE_DEBUG_EXTENSION, {}).get(debug_key),
        }
        for page in pages
    ]


def create_app(settings: Settings | None = None) -> FastAPI:
    resolved = settings or Settings()

    @asynccontextmanager
    async def lifespan(app: FastAPI) -> AsyncIterator[None]:
        resolved.runtime_root.mkdir(parents=True, exist_ok=True)
        database = Database(resolved.database_url)
        await database.connect()
        redis = Redis.from_url(resolved.redis_url, decode_responses=True)
        await redis.ping()
        projector_state = ProjectorState()
        projector = asyncio.create_task(
            supervise_worker_event_projector(
                redis,
                database,
                f"api-{uuid.uuid4().hex}",
                projector_state,
                resolved.projector_claim_idle_milliseconds,
                resolved.projector_restart_delay_seconds,
            ),
            name="worker-event-projector",
        )
        app.state.settings = resolved
        app.state.database = database
        app.state.redis = redis
        app.state.projector = projector
        app.state.projector_state = projector_state
        await asyncio.sleep(0)
        yield
        projector.cancel()
        try:
            await projector
        except asyncio.CancelledError:
            pass
        await redis.aclose()
        await database.close()

    app = FastAPI(title="Document Intelligence Platform API", version="0.1.0", lifespan=lifespan)
    app.add_middleware(
        CORSMiddleware,
        allow_origins=resolved.allowed_origins,
        allow_credentials=True,
        allow_methods=["*"],
        allow_headers=["*"],
    )

    @app.get("/health")
    async def health(request: Request) -> dict[str, str]:
        await request.app.state.redis.ping()
        projector: asyncio.Task[None] = request.app.state.projector
        projector_state: ProjectorState = request.app.state.projector_state
        if projector.done() or not projector_state.active:
            raise HTTPException(503, "worker event projector is not active")
        return {"status": "ok"}

    @app.get("/api/v1/capabilities", response_model=CapabilitiesResponse)
    async def capabilities(request: Request) -> CapabilitiesResponse:
        redis: Redis = request.app.state.redis
        workers = await _workers(redis)
        return CapabilitiesResponse(
            registered=REGISTERED_BACKENDS,
            available=_common_worker_capabilities(workers),
            workers=workers,
        )

    @app.post("/api/v1/documents", response_model=DocumentResponse, status_code=201)
    async def upload_document(request: Request, file: UploadFile = File(...)) -> DocumentResponse:
        if file.content_type != "application/pdf":
            raise HTTPException(415, "only application/pdf uploads are accepted")
        document_id = _id("doc")
        input_path = resolved.runtime_root / "documents" / document_id / "input.pdf"
        size, sha256 = await _save_upload(file, input_path, resolved.maximum_upload_bytes)
        record = await request.app.state.database.create_document({
            "id": document_id,
            "filename": Path(file.filename or "input.pdf").name,
            "media_type": "application/pdf",
            "size_bytes": size,
            "sha256": sha256,
            "input_path": str(input_path.resolve()),
        })
        return DocumentResponse(
            document_id=record["id"], filename=record["filename"], media_type=record["media_type"],
            size_bytes=record["size_bytes"], sha256=record["sha256"], created_at=record["created_at"],
        )

    @app.post("/api/v1/documents/{document_id}/runs", response_model=RunResponse, status_code=202)
    async def create_run(document_id: str, options: RunCreate, request: Request) -> RunResponse:
        _validate_backends(options)
        workers = await _workers(request.app.state.redis)
        available = _common_worker_capabilities(workers)
        for stage, backend in options.backends.model_dump(exclude={"registry_config"}).items():
            if backend not in available[stage]:
                raise HTTPException(409, f"no common worker capability for {stage} backend: {backend}")
        database: Database = request.app.state.database
        document = await database.get_document(document_id)
        if document is None:
            raise HTTPException(404, "document not found")
        run_id = _id("run")
        job_id = _id("job")
        attempt_id = _id("attempt")
        run_directory = resolved.runtime_root / "runs" / run_id
        output_directory = run_directory / "output"
        output_directory.mkdir(parents=True, exist_ok=True)
        job = {
            "schema_version": 1,
            "job_id": job_id,
            "document_id": document_id,
            "run_id": run_id,
            "attempt_id": attempt_id,
            "created_at": utc_now().isoformat(),
            "input": {
                "uri": f"file://{document['input_path']}",
                "filename": document["filename"],
                "media_type": document["media_type"],
                "sha256": document["sha256"],
                "size_bytes": document["size_bytes"],
            },
            "output_directory": str(output_directory.resolve()),
            "pipeline": {
                "dpi": options.dpi,
                "debug": options.debug,
                "backends": options.backends.model_dump(),
            },
            "limits": {
                "timeout_seconds": options.timeout_seconds,
                "maximum_pages": options.maximum_pages,
            },
        }
        job_path = run_directory / "job.json"
        job_path.write_text(json.dumps(job, indent=2), encoding="utf-8")
        record = await database.create_run({
            "id": run_id,
            "document_id": document_id,
            "attempt_id": attempt_id,
            "options": options.model_dump(),
            "job_path": str(job_path.resolve()),
        })
        redis: Redis = request.app.state.redis
        await redis.hset(f"run:{run_id}", mapping={"status": "queued", "stage": ""})
        await _enqueue_job(
            redis,
            resolved.job_stream,
            {"job_id": job_id, "run_id": run_id, "job_path": str(job_path.resolve())},
            resolved.job_stream_max_length,
        )
        return RunResponse(
            run_id=run_id,
            document_id=document_id,
            attempt_id=attempt_id,
            status="queued",
            options=_record_options(record),
            created_at=record["created_at"],
        )

    async def run_response(run_id: str, request: Request) -> RunResponse:
        record = await request.app.state.database.get_run(run_id)
        if record is None:
            raise HTTPException(404, "run not found")
        state = await request.app.state.redis.hgetall(f"run:{run_id}")
        return RunResponse(
            run_id=record["id"], document_id=record["document_id"], attempt_id=record["attempt_id"],
            status=state.get("status", record["status"]), stage=state.get("stage") or record["stage"],
            options=_record_options(record), created_at=record["created_at"],
            updated_at=state.get("updated_at") or record["updated_at"].isoformat(),
            error=state.get("error") or record["error"],
        )

    @app.get("/api/v1/runs/{run_id}", response_model=RunResponse)
    async def get_run(run_id: str, request: Request) -> RunResponse:
        return await run_response(run_id, request)

    @app.get("/api/v1/documents/{document_id}/runs", response_model=list[RunResponse])
    async def list_runs(document_id: str, request: Request) -> list[RunResponse]:
        records = await request.app.state.database.list_runs(document_id)
        return [await run_response(record["id"], request) for record in records]

    @app.get("/api/v1/runs/{run_id}/events")
    async def stream_events(run_id: str, request: Request, last_event_id: str | None = None) -> StreamingResponse:
        if await request.app.state.database.get_run(run_id) is None:
            raise HTTPException(404, "run not found")
        redis: Redis = request.app.state.redis

        async def generate() -> AsyncIterator[str]:
            cursor = request.headers.get("last-event-id") or last_event_id or "0-0"
            while True:
                if await request.is_disconnected():
                    return
                messages = await redis.xread({f"run-events:{run_id}": cursor}, block=15000, count=50)
                if not messages:
                    yield ": keep-alive\n\n"
                    continue
                for _, entries in messages:
                    for message_id, fields in entries:
                        cursor = message_id
                        encoded = fields["event"]
                        yield f"id: {message_id}\ndata: {encoded}\n\n"
                        if json.loads(encoded)["type"] in {"job_succeeded", "job_failed", "job_cancelled"}:
                            return

        return StreamingResponse(generate(), media_type="text/event-stream")

    @app.get("/api/v1/runs/{run_id}/artifacts")
    async def list_artifacts(run_id: str, request: Request) -> list[dict[str, Any]]:
        if await request.app.state.database.get_run(run_id) is None:
            raise HTTPException(404, "run not found")
        manifests = resolved.runtime_root / "runs" / run_id / "artifacts"
        if not manifests.exists():
            return []
        return [json.loads(path.read_text(encoding="utf-8")) for path in sorted(manifests.glob("*.json"))]

    @app.get("/api/v1/runs/{run_id}/artifacts/{artifact_id}")
    async def download_artifact(run_id: str, artifact_id: str, request: Request) -> FileResponse:
        if await request.app.state.database.get_run(run_id) is None:
            raise HTTPException(404, "run not found")
        run_root = resolved.runtime_root / "runs" / run_id
        manifest_path = _safe_child(run_root, run_root / "artifacts" / f"{artifact_id}.json")
        if not manifest_path.is_file():
            raise HTTPException(404, "artifact not found")
        artifact = json.loads(manifest_path.read_text(encoding="utf-8"))
        uri = artifact["uri"]
        if not uri.startswith("file://"):
            raise HTTPException(501, "this API instance cannot proxy non-file artifacts")
        artifact_path = _safe_child(run_root, Path(uri.removeprefix("file://")))
        if not artifact_path.is_file():
            raise HTTPException(404, "artifact file is missing")
        return FileResponse(artifact_path, media_type=artifact["media_type"], filename=artifact_path.name)

    @app.get("/api/v1/runs/{run_id}/stages/{stage}")
    async def stage_output(run_id: str, stage: str, request: Request) -> Any:
        if stage not in {"render", "text", "layout", "table", "reading_order", "assembly", "export"}:
            raise HTTPException(404, "unknown stage")
        if await request.app.state.database.get_run(run_id) is None:
            raise HTTPException(404, "run not found")
        output = resolved.runtime_root / "runs" / run_id / "output" / "document.json"
        if not output.is_file():
            raise HTTPException(409, "stage output is not available yet")
        document = json.loads(output.read_text(encoding="utf-8"))
        return _document_stage_output(document, stage)

    return app


app = create_app()
