from __future__ import annotations

import json
from pathlib import Path

import jsonschema
import pytest

from app.projector import SUPPORTED_EVENT_TYPES

PROTOCOL_ROOT = Path(__file__).resolve().parents[2] / "protocol"


def test_job_example_matches_v1_schema() -> None:
    schema = json.loads((PROTOCOL_ROOT / "schemas/job.v1.schema.json").read_text(encoding="utf-8"))
    example = json.loads((PROTOCOL_ROOT / "examples/job.v1.json").read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator(schema).validate(example)


def test_event_examples_cover_projector_types_and_match_v1_schema() -> None:
    schema = json.loads((PROTOCOL_ROOT / "schemas/event.v1.schema.json").read_text(encoding="utf-8"))
    validator = jsonschema.Draft202012Validator(schema, format_checker=jsonschema.FormatChecker())
    event_examples = sorted(
        path
        for path in (PROTOCOL_ROOT / "examples").glob("*.v1.json")
        if path.name not in {"job.v1.json", "artifact.v1.json"}
    )
    event_types: set[str] = set()
    for path in event_examples:
        example = json.loads(path.read_text(encoding="utf-8"))
        validator.validate(example)
        event_types.add(example["type"])
    assert event_types == SUPPORTED_EVENT_TYPES


def test_event_schema_rejects_payload_fields_from_another_event_type() -> None:
    schema = json.loads((PROTOCOL_ROOT / "schemas/event.v1.schema.json").read_text(encoding="utf-8"))
    example = json.loads((PROTOCOL_ROOT / "examples/job-started.v1.json").read_text(encoding="utf-8"))
    example["warning"] = {"code": "EXAMPLE", "message": "wrong event payload", "details": {}}

    with pytest.raises(jsonschema.ValidationError):
        jsonschema.Draft202012Validator(schema).validate(example)


def test_artifact_example_matches_v1_schema() -> None:
    schema = json.loads((PROTOCOL_ROOT / "schemas/artifact.v1.schema.json").read_text(encoding="utf-8"))
    example = json.loads((PROTOCOL_ROOT / "examples/artifact.v1.json").read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator(schema).validate(example)
