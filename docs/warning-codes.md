# Warning Code Registry

Document v1 uses warnings for usable results that were produced through a degraded path. The machine-readable `code`
is stable; the human-readable `message` may improve without changing the code.

The engine's code-level registry is
[`cpp/common/warning_codes.h`](../cpp/common/warning_codes.h). Document v1 intentionally does not define these values
as a closed JSON Schema enum: compatible v1 releases may add codes, and embedding applications may add namespaced
extension codes.

## Engine Codes

| Code | Stage | Meaning | Stable details |
| --- | --- | --- | --- |
| `OCR_ENHANCEMENT_FAILED` | `text` | OCR requested to improve usable native text failed, so the native text was retained. | `fallback=native_text`; `reason` identifies the native-text quality decision. |
| `LAYOUT_BACKEND_FALLBACK` | `layout` | A layout backend failed at runtime and the next configured backend produced the page result. | `failed_backend`, `fallback_backend`, `reason=inference_failed`. |
| `TABLE_BACKEND_FALLBACK` | `table` | A table backend failed at runtime and the next configured backend produced the page result. | `failed_backend`, `fallback_backend`, `reason=inference_failed`. |

## Compatibility Rules

- Existing codes keep their stage and meaning for the lifetime of Document v1.
- New codes are additive. Consumers must tolerate unknown codes.
- `message` is for people and must not be parsed.
- Registered detail keys keep their meaning, but additional detail keys may be added.
- Application-defined codes should use a collision-resistant prefix, such as `ACME_OCR_REVIEW_REQUIRED`.

An engine-produced warning makes the document `partial`. A fatal stage failure is returned as `Status` and does not
produce a usable Document v1 result.

## Aggregation

Equivalent warnings are grouped in the final Document result by `code`, `stage`, `message`, `block_id`, and `details`.
`occurrence_count` records how many diagnostics the warning represents. A warning affecting multiple pages uses the
ordered, unique `page_ids` array; a single-page warning keeps `page_id`. This bounds the number of warning objects
without discarding affected-page evidence. Pipeline events remain per occurrence for operational tracing.

Consumers must use `occurrence_count`, not the length of `warnings`, when counting degradations. Both
`occurrence_count` and `page_ids` are optional additive Document v1 fields; their absence means one occurrence and
the optional singleton `page_id` scope.
