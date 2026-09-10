from __future__ import annotations

import asyncio
import os
from pathlib import Path

import pytest
from pydantic import ValidationError

from app.artifact_storage import ArtifactLock, ArtifactsExpired, finish_thread, open_run_directory, remove_generated_entries
from app.retention import validate_job_path
from app.settings import Settings


@pytest.mark.parametrize("component", ["runs", "run_test"])
def test_run_directory_symlinks_are_rejected(tmp_path, component):
    outside = tmp_path / "outside"
    outside.mkdir()
    runtime = tmp_path / "runtime"
    runtime.mkdir()
    if component == "runs":
        (runtime / "runs").symlink_to(outside, target_is_directory=True)
    else:
        (runtime / "runs").mkdir()
        (runtime / "runs/run_test").symlink_to(outside, target_is_directory=True)
    with pytest.raises(OSError):
        open_run_directory(runtime, "run_test")
    assert list(outside.iterdir()) == []


def test_deletion_never_follows_symlinks_and_keeps_metadata(tmp_path):
    run = tmp_path / "runs/run_test"
    run.mkdir(parents=True)
    outside = tmp_path / "outside"
    outside.mkdir()
    valuable = outside / "keep.txt"
    valuable.write_text("untouched")
    (run / "output").symlink_to(outside, target_is_directory=True)
    (run / "executions").mkdir()
    (run / "executions/escape").symlink_to(outside, target_is_directory=True)
    (run / "job.json").write_text("{}")
    (run / "operator-note.txt").write_text("keep")
    with ArtifactLock(tmp_path, "run_test", exclusive=True) as lock:
        remove_generated_entries(lock)
        remove_generated_entries(lock)
    assert valuable.read_text() == "untouched"
    assert {p.name for p in run.iterdir()} == {"job.json", "operator-note.txt", ".artifacts.lock", ".artifacts-expired"}
    with pytest.raises(ArtifactsExpired):
        ArtifactLock(tmp_path, "run_test")


@pytest.mark.parametrize("link", ["symbolic", "hard"])
def test_lock_file_links_are_rejected(tmp_path, link):
    run = tmp_path / "runs/run_test"
    run.mkdir(parents=True)
    outside = tmp_path / "keep"
    outside.write_text("untouched")
    if link == "symbolic":
        (run / ".artifacts.lock").symlink_to(outside)
    else:
        os.link(outside, run / ".artifacts.lock")
    with pytest.raises((OSError, ValueError)):
        ArtifactLock(tmp_path, "run_test", exclusive=True)
    assert outside.read_text() == "untouched"


@pytest.mark.parametrize("identity,path", [("../outside", "job.json"), ("run_test", "outside/job.json")])
def test_cleanup_requires_exact_canonical_run_path(tmp_path, identity, path):
    with pytest.raises(ValueError):
        validate_job_path(tmp_path, {"id": identity, "job_path": str(tmp_path / path)})


def test_retention_settings_are_opt_in_and_bounded():
    assert Settings().artifact_retention_seconds == 0
    for options in [{"artifact_retention_seconds": -1}, {"artifact_cleanup_batch_size": 0},
                    {"artifact_cleanup_batch_size": 501}, {"artifact_cleanup_interval_seconds": 0}]:
        with pytest.raises(ValidationError):
            Settings(**options)


@pytest.mark.parametrize("fail", [False, True])
def test_cancelled_thread_wait_keeps_caller_resources_alive(fail):
    import threading
    started, release, finished = threading.Event(), threading.Event(), threading.Event()
    def work():
        started.set()
        assert release.wait(timeout=5)
        finished.set()
        if fail:
            raise OSError("injected filesystem error during shutdown")
    async def check():
        task = asyncio.create_task(finish_thread(work))
        await asyncio.to_thread(started.wait, 5)
        task.cancel()
        await asyncio.sleep(0)
        assert not task.done()
        task.cancel()
        await asyncio.sleep(0)
        assert not task.done()
        release.set()
        with pytest.raises(asyncio.CancelledError):
            await task
        assert finished.is_set()
    asyncio.run(check())
