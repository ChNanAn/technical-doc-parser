#!/usr/bin/env python3
"""Validate engine-produced documents against the public document contract."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

try:
    import jsonschema
except ModuleNotFoundError:
    print(
        "error: jsonschema is required; install tests/contract/requirements.txt",
        file=sys.stderr,
    )
    raise SystemExit(2)


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"failed to read JSON from {path}: {error}") from error
    if not isinstance(value, dict):
        raise RuntimeError(f"expected a JSON object in {path}")
    return value


def format_path(parts: Any) -> str:
    path = "$"
    for part in parts:
        path += f"[{part}]" if isinstance(part, int) else f".{part}"
    return path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema", required=True, type=Path)
    parser.add_argument("documents", nargs="+", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        schema = load_json(args.schema)
        jsonschema.Draft202012Validator.check_schema(schema)
        validator = jsonschema.Draft202012Validator(schema, format_checker=jsonschema.FormatChecker())
        failed = False
        for document_path in args.documents:
            document = load_json(document_path)
            errors = sorted(
                validator.iter_errors(document),
                key=lambda error: tuple(str(part) for part in error.absolute_path),
            )
            for error in errors:
                failed = True
                print(f"{document_path}:{format_path(error.absolute_path)}: {error.message}", file=sys.stderr)
        return 1 if failed else 0
    except (RuntimeError, jsonschema.SchemaError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
