"""Cooperative filesystem locks shared by API readers, Workers and retention.

The platform uses a shared POSIX filesystem. Never replace/unlink the lock file:
its inode must stay stable for paused Workers and in-flight downloads.
"""
from __future__ import annotations

import asyncio
import fcntl
import os
import re
import shutil
import stat
from pathlib import Path

from fastapi.responses import FileResponse, JSONResponse

from .database import Database


GENERATED_ENTRIES = ("output", "artifacts", "executions", "events.ndjson")
EXPIRED_MARKER = ".artifacts-expired"


class ArtifactsExpired(Exception):
    pass


async def finish_thread(function, *args):
    # Repeated shutdown cancellation must not release a lock while filesystem
    # work still runs in its thread. Retrieve failures even after cancellation.
    task = asyncio.create_task(asyncio.to_thread(function, *args))
    cancelled = False
    while not task.done():
        try:
            await asyncio.shield(task)
        except asyncio.CancelledError:
            cancelled = True
        except Exception:
            break  # Retrieve the thread failure below, preserving shutdown cancellation.
    try:
        return task.result()
    finally:
        if cancelled:
            raise asyncio.CancelledError()


def run_directory(runtime_root: Path, run_id: str) -> Path:
    if not re.fullmatch(r"[A-Za-z0-9_-]+", run_id):
        raise ValueError("invalid Run identity for artifact storage")
    return runtime_root.resolve() / "runs" / run_id


def open_run_directory(runtime_root: Path, run_id: str) -> int:
    run_directory(runtime_root, run_id)  # Validate before using the name with dir_fd.
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
    root_fd = os.open(runtime_root.resolve(), flags)
    try:
        runs_fd = os.open("runs", flags, dir_fd=root_fd)
        try:
            return os.open(run_id, flags, dir_fd=runs_fd)
        finally:
            os.close(runs_fd)
    finally:
        os.close(root_fd)


class ArtifactLock:
    def __init__(self, runtime_root: Path, run_id: str, *, exclusive: bool = False):
        self.directory_fd = open_run_directory(runtime_root, run_id)
        self.lock_fd = -1
        try:
            self.lock_fd = os.open(".artifacts.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_CLOEXEC,
                                   0o600, dir_fd=self.directory_fd)
            info = os.fstat(self.lock_fd)
            if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
                raise ValueError("artifact lock must be a single-link regular file")
            fcntl.flock(self.lock_fd, (fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH) | fcntl.LOCK_NB)
            if not exclusive:
                try:
                    os.stat(EXPIRED_MARKER, dir_fd=self.directory_fd, follow_symlinks=False)
                except FileNotFoundError:
                    pass
                else:
                    raise ArtifactsExpired()
        except BaseException:
            self.close()
            raise

    def close(self) -> None:
        if self.lock_fd >= 0:
            os.close(self.lock_fd)
            self.lock_fd = -1
        if self.directory_fd >= 0:
            os.close(self.directory_fd)
            self.directory_fd = -1

    def __enter__(self) -> "ArtifactLock":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


def remove_generated_entries(lock: ArtifactLock) -> None:
    if not shutil.rmtree.avoids_symlink_attacks:
        raise RuntimeError("artifact cleanup requires fd-based rmtree support")
    marker = os.open(EXPIRED_MARKER, os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK,
                     0o600, dir_fd=lock.directory_fd)
    try:
        info = os.fstat(marker)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ValueError("artifact expiry marker must be a single-link regular file")
        os.fsync(marker)
    finally:
        os.close(marker)
    os.fsync(lock.directory_fd)
    for name in GENERATED_ENTRIES:
        try:
            info = os.stat(name, dir_fd=lock.directory_fd, follow_symlinks=False)
        except FileNotFoundError:
            continue
        if stat.S_ISDIR(info.st_mode):
            shutil.rmtree(name, dir_fd=lock.directory_fd)
        else:
            # Symlinks are removed as links; their targets are never followed.
            os.unlink(name, dir_fd=lock.directory_fd)
    os.fsync(lock.directory_fd)


class ArtifactFileResponse(FileResponse):
    def __init__(self, *args, runtime_root: Path, run_id: str, database: Database, **kwargs):
        super().__init__(*args, **kwargs)
        self.runtime_root = runtime_root
        self.run_id = run_id
        self.database = database

    async def __call__(self, scope, receive, send) -> None:
        # Endpoint validation finishes before FileResponse opens/streams the file.
        # Recheck under a lock held until the entire response finishes/disconnects.
        try:
            lock = ArtifactLock(self.runtime_root, self.run_id)
        except ArtifactsExpired:
            await JSONResponse({"detail": "Run artifacts have expired"}, status_code=410)(scope, receive, send)
            return
        except BlockingIOError:
            await JSONResponse({"detail": "Run artifacts are being cleaned"}, status_code=503,
                               headers={"Retry-After": "1"})(scope, receive, send)
            return
        with lock:
            # A previous cleanup may have committed expiry but failed before it
            # could write the filesystem marker (for example, a full disk).
            record = await self.database.get_run(self.run_id)
            if record is None or record["artifacts_expired_at"] is not None:
                await JSONResponse({"detail": "Run artifacts have expired"}, status_code=410)(scope, receive, send)
                return
            await super().__call__(scope, receive, send)
