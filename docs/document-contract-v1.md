# Document Contract v1

[中文说明](document-contract-v1.zh-CN.md)

Document v1 is the consumer-facing result contract for the engine. Its machine-readable Schema is
[`schemas/document.v1.schema.json`](../schemas/document.v1.schema.json), with an example at
[`schemas/examples/document.v1.example.json`](../schemas/examples/document.v1.example.json).

The contract is a release candidate. The current `document.json` exporter still uses the legacy shape; the public
C++ model and exporter should migrate together so JSON and future SDK consumers share the same semantics.

## Design Principle

Build a permissive but interpretable result envelope, then use three-layer evaluation to improve actual quality.
Do not try to encode every future capability or quality requirement in one rigid Schema.

Document v1 therefore freezes the meanings that consumers must rely on:

- Contract version, source identity, producer identity, and result status.
- Page identity and one unambiguous coordinate convention.
- The reading-order meaning of the `blocks` array.
- Block and relation reference scope.
- Machine-readable warnings for usable but degraded results.

It deliberately does not freeze model taxonomies, backend names, every possible block attribute, or a mandatory
provenance graph. Those areas need room to evolve as the engine is measured on more documents.

Schema validity means that a result can be interpreted. It does not mean that OCR, layout, tables, reading order, or
RAG citations are accurate. Those claims belong to the [three-layer evaluation](evaluation.md).

## Input Scope

The contract is not limited to PDF. `source.media_type` uses a MIME type, so the same envelope can represent a PDF,
page image, office document, or another source supported in the future.

The current implementation starts with PDF because PDFium is the only complete document-source adapter today.
Adding an input format is an engine capability change, not a Document v1 breaking change, as long as it produces the
same page-grounded result semantics.

## Required Envelope

| Field | Stable meaning |
| --- | --- |
| `$schema` | Canonical URI of Document v1. |
| `schema_version` | Major contract version, fixed to `1`. |
| `document_id` | Identifier within the producer's scope; never a filesystem path. |
| `status` | `complete` or usable-but-degraded `partial`. |
| `source` | Source MIME type and optional safe identity fields. |
| `producer` | Engine name and version; may include revision and run ID. |
| `coordinate_space` | Shared page coordinate semantics. |
| `pages` | Pages available for grounding and rendering. |
| `blocks` | Final blocks in document reading order. |
| `warnings` | Stable degradation codes and human-readable context. |

`relations`, `metadata`, and `extensions` are optional. Unknown fields are allowed for forward compatibility. New
experimental or provider-specific data should still be placed under a namespaced `extensions` key, for example
`extensions["org.example.layout"]`, to avoid accidental collisions.

## Coordinates

All public bboxes use one convention:

```text
unit: pixel
origin: top_left
bbox_format: xyxy
bbox: [x0, y0, x1, y1]
```

Coordinates may be integers or decimals. A bbox must have positive area and remain inside its referenced page:

```text
0 <= x0 < x1 <= page.width
0 <= y0 < y1 <= page.height
```

Page numbers are one-based. References use page IDs rather than array offsets. Coordinates describe the emitted,
orientation-normalized page image, not an implicit PDF coordinate system.

This is the strictest part of v1 because changing coordinate meaning would silently break viewers, citations, and
training-data generation.

## Blocks and Reading Order

Only `id` and `type` are required on every block. `text`, `page_id`, `bbox`, `score`, `source_refs`, `table`,
`metadata`, and `extensions` are optional because not every backend can produce them reliably.

The top-level `blocks` array is the final global reading order. Consumers should not sort it again by bbox unless
they intentionally replace the engine's reading order.

`type` is an open string vocabulary. Producers should prefer common values such as `title`, `heading`, `paragraph`,
`list`, `table`, `figure`, `caption`, `formula`, `header`, `footer`, and `unknown`, but the Schema does not reject new
types. Consumers must preserve or tolerate unfamiliar values.

Optional fields are not a promise that missing information is good enough. For example, a block without a page
anchor is structurally valid but lowers source-grounding and RAG citation metrics.

## Tables

A table block may carry ordered rows and cells. Cell `text` is the only required cell field. Row/column indices,
spans, header flags, bboxes, scores, and source references are optional so a weak backend can publish an honest
partial result instead of inventing structure.

When present:

- Row and column indices are zero-based.
- Missing spans mean `1`.
- `is_header` is explicit; consumers must not assume the first row is a header.
- A cell bbox uses the page of its containing block.
- Cross-page fragments may share `continuation_group_id`.

Table structure quality and table text accuracy are measured separately. Passing the Schema is not a table-quality
claim.

## Explainability Boundary

Document v1 keeps the explanation needed by ordinary consumers:

- `producer` identifies which engine build emitted the result.
- Page IDs and bboxes locate output in the visible source.
- `source_refs` may retain finer source regions or text evidence.
- `warnings` explain partial output through stable codes.
- `extensions` may expose additional provider detail without changing the public core.

The complete execution history belongs in a separate Pipeline Trace contract: stage attempts, requested and resolved
backends, fallback reasons, timings, model identities, errors, and debug artifacts. Keeping that trace outside
Document v1 prevents worker internals from becoming permanent SDK fields.

## Compatibility Policy

Compatible v1 changes include adding optional fields, warning codes, block types, relation types, or extension data.
Consumers should read only fields they need and ignore unfamiliar optional fields.

The following require v2:

- Removing or renaming a required envelope field.
- Changing coordinate units, origin, bbox order, or page-reference scope.
- Changing `blocks` from reading order to another order.
- Reinterpreting `complete` or `partial`.
- Changing an existing field's meaning incompatibly.

Flexible vocabulary is intentional, not absence of governance. Widely used extension fields can graduate into the
documented v1 vocabulary only after fixtures, consumers, and evaluation show that their semantics are stable.

## Contract Tests

JSON Schema checks the public shape. Semantic tests additionally verify:

- Unique page and block IDs.
- Resolved page, block, warning, relation, and source references when those references are present.
- Positive bboxes contained by their page.
- Table cell bboxes contained by the table block's page.
- A `partial` result has at least one warning.
- Source filenames and artifact URIs do not leak absolute local paths.

Contract fixtures should cover at least native text, scanned OCR, complex tables, and multi-column reading order.
Snapshots protect intentional public output changes; the quality corpus determines whether the content improved.

## Adoption Plan

1. Validate the four contract fixture classes and representative non-PDF media types.
2. Map the public C++ document model to this envelope without exporter-specific fields.
3. Make JSON, Markdown, HTML, and future RAG exporters consume the same assembled document.
4. Add Pipeline Trace v1 for backend selection, fallback, timings, and failures.
5. Gate releases with the versioned Quality Report instead of expanding required Schema fields.

