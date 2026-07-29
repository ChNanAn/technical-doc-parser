#!/usr/bin/env python3
"""Validate and create the separately versioned baseline model pack."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
import re
import sys
import tarfile
import time
import urllib.error
import urllib.request
from pathlib import Path, PurePosixPath
from typing import Any


ROOT_DIR = Path(__file__).resolve().parent.parent
DEFAULT_MANIFEST = ROOT_DIR / "packaging" / "model-pack.v1.json"
LICENSES_FILE = ROOT_DIR / "packaging" / "MODEL-LICENSES.md"
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


class ManifestError(ValueError):
    pass


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ManifestError(f"cannot read model manifest {path}: {error}") from error
    validate_manifest(value)
    return value


def require_string(container: dict[str, Any], key: str, context: str) -> str:
    value = container.get(key)
    if not isinstance(value, str) or not value:
        raise ManifestError(f"{context}.{key} must be a non-empty string")
    return value


def validate_manifest(manifest: dict[str, Any]) -> None:
    if not isinstance(manifest, dict):
        raise ManifestError("model manifest root must be an object")
    if manifest.get("schema_version") != 1:
        raise ManifestError("model manifest schema_version must be 1")
    require_string(manifest, "name", "manifest")
    require_string(manifest, "version", "manifest")

    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise ManifestError("manifest.files must be a non-empty array")

    identifiers: set[str] = set()
    paths: set[str] = set()
    for index, entry in enumerate(files):
        context = f"manifest.files[{index}]"
        if not isinstance(entry, dict):
            raise ManifestError(f"{context} must be an object")
        identifier = require_string(entry, "id", context)
        require_string(entry, "role", context)
        relative_path = require_string(entry, "path", context)
        expected_sha256 = require_string(entry, "sha256", context)
        if not SHA256_PATTERN.fullmatch(expected_sha256):
            raise ManifestError(f"{context}.sha256 must be 64 lowercase hex characters")

        parsed_path = PurePosixPath(relative_path)
        if parsed_path.is_absolute() or ".." in parsed_path.parts or relative_path != str(parsed_path):
            raise ManifestError(f"{context}.path must be a normalized relative POSIX path")
        if identifier in identifiers:
            raise ManifestError(f"duplicate model id: {identifier}")
        if relative_path in paths:
            raise ManifestError(f"duplicate model path: {relative_path}")
        identifiers.add(identifier)
        paths.add(relative_path)

        source = entry.get("source")
        if not isinstance(source, dict):
            raise ManifestError(f"{context}.source must be an object")
        require_string(source, "repository", f"{context}.source")
        require_string(source, "revision", f"{context}.source")
        source_url = require_string(source, "url", f"{context}.source")
        if not source_url.startswith("https://"):
            raise ManifestError(f"{context}.source.url must use HTTPS")

        license_record = entry.get("license")
        if not isinstance(license_record, dict):
            raise ManifestError(f"{context}.license must be an object")
        require_string(license_record, "spdx", f"{context}.license")
        license_url = require_string(license_record, "url", f"{context}.license")
        if not license_url.startswith("https://"):
            raise ManifestError(f"{context}.license.url must use HTTPS")


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_files(manifest: dict[str, Any], models_dir: Path) -> None:
    failures: list[str] = []
    for entry in manifest["files"]:
        path = models_dir / entry["path"]
        if not path.is_file() or path.stat().st_size == 0:
            failures.append(f"{entry['id']}: missing or empty file: {path}")
            continue
        actual_sha256 = file_sha256(path)
        if actual_sha256 != entry["sha256"]:
            failures.append(
                f"{entry['id']}: SHA256 mismatch for {path}: "
                f"expected {entry['sha256']}, got {actual_sha256}"
            )
    if failures:
        raise ManifestError("\n".join(failures))


def download_verified_file(
    entry: dict[str, Any],
    destination: Path,
    attempts: int = 3,
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f"{destination.name}.tmp.{os.getpid()}")
    source_url = entry["source"]["url"]
    expected_sha256 = entry["sha256"]

    for attempt in range(1, attempts + 1):
        try:
            print(f"Downloading {entry['id']} to {destination}")
            request = urllib.request.Request(
                source_url,
                headers={"User-Agent": "technical-doc-parser-model-sync/1"},
            )
            digest = hashlib.sha256()
            with urllib.request.urlopen(request, timeout=120) as response:
                with temporary.open("wb") as output:
                    for chunk in iter(lambda: response.read(1024 * 1024), b""):
                        output.write(chunk)
                        digest.update(chunk)
            actual_sha256 = digest.hexdigest()
            if actual_sha256 != expected_sha256:
                raise OSError(
                    f"SHA256 mismatch: expected {expected_sha256}, "
                    f"got {actual_sha256}"
                )
            os.replace(temporary, destination)
            return
        except (OSError, TimeoutError, urllib.error.URLError) as error:
            temporary.unlink(missing_ok=True)
            if attempt == attempts:
                raise ManifestError(
                    f"{entry['id']}: failed to download {source_url} "
                    f"after {attempts} attempts: {error}"
                ) from error
            delay_seconds = min(2 ** (attempt - 1), 4)
            print(
                f"Download attempt {attempt}/{attempts} failed for "
                f"{entry['id']}: {error}; retrying in {delay_seconds}s",
                file=sys.stderr,
            )
            time.sleep(delay_seconds)
        finally:
            temporary.unlink(missing_ok=True)


def sync_files(
    manifest: dict[str, Any],
    models_dir: Path,
    force: bool = False,
    download_attempts: int = 3,
) -> None:
    for entry in manifest["files"]:
        destination = models_dir / entry["path"]
        if not force and destination.is_file() and destination.stat().st_size > 0:
            if file_sha256(destination) == entry["sha256"]:
                print(f"Using verified file: {destination}")
                continue
            print(
                f"Existing file failed SHA256 verification; downloading it "
                f"again: {destination}",
                file=sys.stderr,
            )
        download_verified_file(entry, destination, download_attempts)
    verify_files(manifest, models_dir)


def tar_info(name: str, size: int, epoch: int, mode: int = 0o644) -> tarfile.TarInfo:
    info = tarfile.TarInfo(name)
    info.size = size
    info.mtime = epoch
    info.mode = mode
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    return info


def add_bytes(archive: tarfile.TarFile, name: str, data: bytes, epoch: int) -> None:
    archive.addfile(tar_info(name, len(data), epoch), io.BytesIO(data))


def package_models(
    manifest: dict[str, Any],
    manifest_path: Path,
    models_dir: Path,
    output_dir: Path,
    epoch: int,
) -> Path:
    verify_files(manifest, models_dir)
    if not LICENSES_FILE.is_file():
        raise ManifestError(f"model license summary is missing: {LICENSES_FILE}")

    package_root = f"technical-doc-parser-models-{manifest['version']}"
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / f"{package_root}.tar.gz"

    with output_path.open("wb") as raw_output:
        with gzip.GzipFile(fileobj=raw_output, mode="wb", filename="", mtime=epoch) as gzip_output:
            with tarfile.open(fileobj=gzip_output, mode="w") as archive:
                manifest_bytes = manifest_path.read_bytes()
                licenses_bytes = LICENSES_FILE.read_bytes()
                add_bytes(
                    archive,
                    f"{package_root}/MODEL-MANIFEST.json",
                    manifest_bytes,
                    epoch,
                )
                add_bytes(
                    archive,
                    f"{package_root}/MODEL-LICENSES.md",
                    licenses_bytes,
                    epoch,
                )
                for entry in sorted(manifest["files"], key=lambda item: item["path"]):
                    model_path = models_dir / entry["path"]
                    with model_path.open("rb") as source:
                        archive.addfile(
                            tar_info(
                                f"{package_root}/{entry['path']}",
                                model_path.stat().st_size,
                                epoch,
                            ),
                            source,
                        )
    return output_path


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument(
        "command",
        choices=("validate", "verify", "sync", "package"),
        help=(
            "validate metadata, verify or synchronize local files, "
            "or create the model pack"
        ),
    )
    result.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    result.add_argument("--models-dir", type=Path, default=ROOT_DIR / "models")
    result.add_argument("--output-dir", type=Path, default=ROOT_DIR / "dist")
    result.add_argument("--source-date-epoch", type=int, default=0)
    result.add_argument(
        "--force",
        action="store_true",
        help="re-download every file when synchronizing the model pack",
    )
    return result


def main() -> int:
    arguments = parser().parse_args()
    try:
        if arguments.source_date_epoch < 0:
            raise ManifestError("source date epoch must be non-negative")
        manifest = load_manifest(arguments.manifest)
        if arguments.command == "validate":
            print(f"Validated model pack manifest v{manifest['schema_version']}")
        elif arguments.command == "verify":
            verify_files(manifest, arguments.models_dir)
            print(f"Verified {len(manifest['files'])} model pack files")
        elif arguments.command == "sync":
            sync_files(manifest, arguments.models_dir, arguments.force)
            print(f"Synchronized {len(manifest['files'])} model pack files")
        else:
            output_path = package_models(
                manifest,
                arguments.manifest,
                arguments.models_dir,
                arguments.output_dir,
                arguments.source_date_epoch,
            )
            print(f"{file_sha256(output_path)}  {output_path.name}")
    except ManifestError as error:
        print(f"model pack error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
