from __future__ import annotations

import json
from pathlib import Path

import jsonschema


PROTOCOL_ROOT = Path(__file__).resolve().parents[2] / "protocol"


def test_job_example_matches_v1_schema() -> None:
    schema = json.loads((PROTOCOL_ROOT / "schemas/job.v1.schema.json").read_text(encoding="utf-8"))
    example = json.loads((PROTOCOL_ROOT / "examples/job.v1.json").read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator(schema).validate(example)


def test_event_example_matches_v1_schema() -> None:
    schema = json.loads((PROTOCOL_ROOT / "schemas/event.v1.schema.json").read_text(encoding="utf-8"))
    example = json.loads((PROTOCOL_ROOT / "examples/stage-progress.v1.json").read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator(schema).validate(example)


def test_artifact_example_matches_v1_schema() -> None:
    schema = json.loads((PROTOCOL_ROOT / "schemas/artifact.v1.schema.json").read_text(encoding="utf-8"))
    example = json.loads((PROTOCOL_ROOT / "examples/artifact.v1.json").read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator(schema).validate(example)
